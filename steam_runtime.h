// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// The part of Steam's flat C API this extension uses, loaded at runtime from Steam's runtime
// library (steam_api64.dll, libsteam_api.so or libsteam_api.dylib) next to the extension.
// Declared here so the extension builds without the Steamworks SDK. The layouts follow
// Steamworks SDK 1.65; the runtime library must be 1.65 or a later release that keeps
// SteamRemotePlay_v004 and SteamFriends_v018.

#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace steam {

#ifdef _WIN32
#define STEAM_CALL __cdecl
#else
#define STEAM_CALL
#endif

using HSteamPipe = std::int32_t;
using HSteamUser = std::int32_t;
using SteamId = std::uint64_t;
using RemotePlaySessionId = std::uint32_t;
using ErrorMessage = char[1024];

enum InitResult : int {
    InitOk = 0,
    InitFailedGeneric = 1,
    InitNoSteamClient = 2,
    InitVersionMismatch = 3,
};

enum RemotePlayInputType : int {
    InputKeyDown = 5,
    InputKeyUp = 6,
};

// Callback ids: k_iSteamRemotePlayCallbacks (5700) + n.
constexpr int SessionConnectedId = 5701;
constexpr int SessionDisconnectedId = 5702;
constexpr int GuestInviteId = 5703;

// Steam packs its callback and input structures to 8 bytes on Windows, 4 elsewhere.
#ifdef _WIN32
#pragma pack(push, 8)
#else
#pragma pack(push, 4)
#endif

struct CallbackMsg {
    HSteamUser user;
    int callback;
    std::uint8_t* param;
    int param_size;
};

struct RemotePlayInputKey {
    int scancode; // USB HID usage, Keyboard/Keypad page
    std::uint32_t modifiers;
    std::uint32_t keycode;
};

struct RemotePlayInput {
    RemotePlaySessionId session;
    RemotePlayInputType type;
    union {
        RemotePlayInputKey key;
        char padding[56];
    };
};
static_assert(sizeof(RemotePlayInput) == 64);

struct SessionConnected {
    RemotePlaySessionId session;
};

struct SessionDisconnected {
    RemotePlaySessionId session;
};

struct GuestInvite {
    char connect_url[1024];
};

#pragma pack(pop)

/// Interface versions checked at init, as SteamAPI_InitEx does with the SDK headers.
constexpr char RequiredInterfaces[] = "SteamFriends018\0STEAMREMOTEPLAY_INTERFACE_VERSION004\0";

#ifdef _WIN32
constexpr const char* LibraryName = "steam_api64.dll";
#elif defined(__APPLE__)
constexpr const char* LibraryName = "libsteam_api.dylib";
#else
constexpr const char* LibraryName = "libsteam_api.so";
#endif

struct Api {
    InitResult(STEAM_CALL* Init)(const char* interface_versions, ErrorMessage* error);
    void(STEAM_CALL* Shutdown)();
    HSteamPipe(STEAM_CALL* GetHSteamPipe)();
    void(STEAM_CALL* ManualDispatch_Init)();
    void(STEAM_CALL* ManualDispatch_RunFrame)(HSteamPipe pipe);
    bool(STEAM_CALL* ManualDispatch_GetNextCallback)(HSteamPipe pipe, CallbackMsg* message);
    void(STEAM_CALL* ManualDispatch_FreeLastCallback)(HSteamPipe pipe);

    void*(STEAM_CALL* SteamFriends)();
    const char*(STEAM_CALL* GetFriendPersonaName)(void* self, SteamId friend_id);

    void*(STEAM_CALL* SteamRemotePlay)();
    std::uint32_t(STEAM_CALL* GetSessionCount)(void* self);
    RemotePlaySessionId(STEAM_CALL* GetSessionID)(void* self, int index);
    SteamId(STEAM_CALL* GetSessionSteamID)(void* self, RemotePlaySessionId session);
    const char*(STEAM_CALL* GetSessionClientName)(void* self, RemotePlaySessionId session);
    bool(STEAM_CALL* ShowRemotePlayTogetherUI)(void* self);
    bool(STEAM_CALL* BSendRemotePlayTogetherInvite)(void* self, SteamId friend_id);
    bool(STEAM_CALL* BEnableRemotePlayTogetherDirectInput)(void* self);
    void(STEAM_CALL* DisableRemotePlayTogetherDirectInput)(void* self);
    std::uint32_t(STEAM_CALL* GetInput)(void* self, RemotePlayInput* events, std::uint32_t max);
    void(STEAM_CALL* SetMouseVisibility)(void* self, RemotePlaySessionId session, bool visible);
};

namespace detail {

/// The folder holding this extension, with a trailing separator, or empty if unknown.
inline std::string OwnFolder() {
#ifdef _WIN32
    HMODULE self = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(&OwnFolder), &self)) {
        return {};
    }
    char path[MAX_PATH];
    const DWORD size = GetModuleFileNameA(self, path, MAX_PATH);
    if (size == 0 || size == MAX_PATH) {
        return {};
    }
    std::string folder{path, size};
    return folder.substr(0, folder.find_last_of("\\/") + 1);
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&OwnFolder), &info) == 0 || info.dli_fname == nullptr) {
        return {};
    }
    const std::string path{info.dli_fname};
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash + 1);
#endif
}

inline void* Open(const std::string& path) {
#ifdef _WIN32
    return LoadLibraryExA(path.c_str(), nullptr,
                          path.find_first_of("\\/") != std::string::npos
                              ? LOAD_WITH_ALTERED_SEARCH_PATH
                              : 0);
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

inline void* Symbol(void* library, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

} // namespace detail

/// Loads Steam's runtime library, preferring the copy next to this extension. On failure,
/// returns false and says why in `error`. The library stays loaded for the process lifetime:
/// Steam's overlay and threads do not survive it being unloaded.
inline bool Load(Api& api, std::string& error) {
    const auto local = detail::OwnFolder() + LibraryName;
    void* library = detail::Open(local);
    if (library == nullptr) {
        library = detail::Open(LibraryName);
    }
    if (library == nullptr) {
        error = std::string{"could not load "} + LibraryName + " (looked for " + local + ")";
        return false;
    }

    bool complete = true;
    const auto bind = [&](auto& function, const char* name) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
            detail::Symbol(library, name));
        if (function == nullptr && complete) {
            complete = false;
            error = std::string{LibraryName} + " has no " + name +
                    ": it is older than Steamworks SDK 1.65";
        }
    };
    bind(api.Init, "SteamInternal_SteamAPI_Init");
    bind(api.Shutdown, "SteamAPI_Shutdown");
    bind(api.GetHSteamPipe, "SteamAPI_GetHSteamPipe");
    bind(api.ManualDispatch_Init, "SteamAPI_ManualDispatch_Init");
    bind(api.ManualDispatch_RunFrame, "SteamAPI_ManualDispatch_RunFrame");
    bind(api.ManualDispatch_GetNextCallback, "SteamAPI_ManualDispatch_GetNextCallback");
    bind(api.ManualDispatch_FreeLastCallback, "SteamAPI_ManualDispatch_FreeLastCallback");
    bind(api.SteamFriends, "SteamAPI_SteamFriends_v018");
    bind(api.GetFriendPersonaName, "SteamAPI_ISteamFriends_GetFriendPersonaName");
    bind(api.SteamRemotePlay, "SteamAPI_SteamRemotePlay_v004");
    bind(api.GetSessionCount, "SteamAPI_ISteamRemotePlay_GetSessionCount");
    bind(api.GetSessionID, "SteamAPI_ISteamRemotePlay_GetSessionID");
    bind(api.GetSessionSteamID, "SteamAPI_ISteamRemotePlay_GetSessionSteamID");
    bind(api.GetSessionClientName, "SteamAPI_ISteamRemotePlay_GetSessionClientName");
    bind(api.ShowRemotePlayTogetherUI, "SteamAPI_ISteamRemotePlay_ShowRemotePlayTogetherUI");
    bind(api.BSendRemotePlayTogetherInvite,
         "SteamAPI_ISteamRemotePlay_BSendRemotePlayTogetherInvite");
    bind(api.BEnableRemotePlayTogetherDirectInput,
         "SteamAPI_ISteamRemotePlay_BEnableRemotePlayTogetherDirectInput");
    bind(api.DisableRemotePlayTogetherDirectInput,
         "SteamAPI_ISteamRemotePlay_DisableRemotePlayTogetherDirectInput");
    bind(api.GetInput, "SteamAPI_ISteamRemotePlay_GetInput");
    bind(api.SetMouseVisibility, "SteamAPI_ISteamRemotePlay_SetMouseVisibility");
    return complete;
}

} // namespace steam
