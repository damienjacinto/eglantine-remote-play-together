// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Steam Remote Play Together as an Eglantine extension.
//
// Steam streams the window and audio itself; the emulator only has to run under a Steam App ID
// and turn guests into players. Each guest gets an extension keyboard fed from the Remote Play
// input API, and the Steam virtual gamepad that appears for them is assigned to a free player.
// Steam does not say which virtual gamepad belongs to which guest, so new ones go to guests that
// have none yet, in the order they joined. Virtual gamepads present while nobody is connected are
// the host's and are left alone.

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <eglantine/extension.h>
#include <steam/steam_api.h>

namespace {

constexpr const char* DefaultAppId = "480";
constexpr const char* MenuPrefix = "Remote Play Together: ";
constexpr std::uint16_t SteamVendorId = 0x28DE;
constexpr std::uint16_t SteamVirtualGamepadId = 0x11FF;
constexpr std::uint32_t TickIntervalMs = 16;
constexpr std::uint32_t TicksPerSessionSync = 60;
constexpr std::uint32_t MaxDevices = 64;

const EglHost* host;
const EglCoreApi* core;
const EglUiApi* ui;
const EglInputApi* input;

bool steam_ready;
ESteamAPIInitResult steam_result{k_ESteamAPIInitResult_OK};
std::string steam_error;
std::string app_id;

void Log(EglLogLevel level, const std::string& message) {
    core->log(host->context, level, message.c_str());
}

std::string GetSetting(const EglCoreApi& api, const EglHost& owner, const char* key,
                       const char* fallback) {
    char buffer[256];
    if (api.get_setting(owner.context, key, buffer, sizeof(buffer)) < 0) {
        return fallback;
    }
    return buffer;
}

void SetEnvIfUnset(const char* name, const std::string& value) {
    if (std::getenv(name) != nullptr) {
        return;
    }
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 0);
#endif
}

bool IsAppId(const std::string& value) {
    return !value.empty() && value.size() <= 10 &&
           std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::vector<EglDeviceInfo> ListDevices() {
    std::vector<EglDeviceInfo> devices(MaxDevices);
    for (auto& device : devices) {
        device.struct_size = sizeof(EglDeviceInfo);
    }
    const auto count = input->list_devices(host->context, devices.data(), MaxDevices);
    devices.resize(std::min(count, MaxDevices));
    return devices;
}

struct Guest {
    std::string name;
    std::uint64_t order{};
    std::uint64_t keyboard{};
    std::int32_t keyboard_player{-1};
    std::string pad;
    std::int32_t pad_player{-1};
};

class Session {
public:
    Session() {
        direct_input = SteamRemotePlay()->BEnableRemotePlayTogetherDirectInput();
        if (!direct_input) {
            Log(EGL_LOG_WARNING, "Steam refused direct input: guest keyboards will type on the "
                                 "host's keyboard");
        }
        AssignPads();
        SyncSessions();
    }

    ~Session() {
        while (!guests.empty()) {
            RemoveGuest(guests.begin()->first);
        }
        if (direct_input) {
            SteamRemotePlay()->DisableRemotePlayTogetherDirectInput();
        }
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void Tick() {
        SteamAPI_RunCallbacks();
        ReadInput();
        if (++ticks % TicksPerSessionSync == 0) {
            SyncSessions();
        }
    }

    void Invite() {
        if (!SteamRemotePlay()->ShowRemotePlayTogetherUI()) {
            ui->show_message(host->context, EGL_MESSAGE_WARNING, "Remote Play Together",
                             "The Steam overlay could not be opened. Check that the overlay is "
                             "enabled in Steam, or copy a guest link instead.");
        }
    }

    void RequestLink() {
        // An invalid friend id asks Steam for a link anyone can open.
        if (!SteamRemotePlay()->BSendRemotePlayTogetherInvite(CSteamID())) {
            ui->show_message(host->context, EGL_MESSAGE_WARNING, "Remote Play Together",
                             "Steam did not create a guest link.");
        }
    }

    void AssignPads() {
        std::vector<std::string> pads;
        for (const auto& device : ListDevices()) {
            if (device.vendor_id == SteamVendorId && device.product_id == SteamVirtualGamepadId) {
                pads.emplace_back(device.id);
            }
        }
        if (guests.empty()) {
            host_pads = std::set<std::string>(pads.begin(), pads.end());
            return;
        }

        std::set<std::string> claimed;
        for (auto& [session, guest] : guests) {
            if (!guest.pad.empty() && std::find(pads.begin(), pads.end(), guest.pad) == pads.end()) {
                guest.pad.clear();
                guest.pad_player = -1;
            }
            if (!guest.pad.empty()) {
                claimed.insert(guest.pad);
            }
        }
        for (const auto& pad : pads) {
            if (host_pads.contains(pad) || claimed.contains(pad)) {
                continue;
            }
            Guest* waiting = nullptr;
            for (auto& [session, guest] : guests) {
                if (guest.pad.empty() && (waiting == nullptr || guest.order < waiting->order)) {
                    waiting = &guest;
                }
            }
            if (waiting == nullptr) {
                continue;
            }
            // A guest who started on the keyboard keeps their player.
            std::int32_t player = -1;
            if (waiting->keyboard_player >= 0) {
                player = waiting->keyboard_player;
                input->release_player(host->context, player);
                waiting->keyboard_player = -1;
            }
            waiting->pad = pad;
            waiting->pad_player = input->assign_device(host->context, pad.c_str(), player,
                                                       waiting->name.c_str());
            claimed.insert(pad);
            Log(EGL_LOG_INFO, waiting->pad_player >= 0
                                  ? waiting->name + " plays as Player " +
                                        std::to_string(waiting->pad_player + 1)
                                  : waiting->name + " has a gamepad but no player is free");
        }
        UpdateStatus();
    }

    void OnPlayerReleased(std::int32_t player) {
        for (auto& [session, guest] : guests) {
            if (guest.pad_player == player) {
                guest.pad_player = -1;
                guest.pad.clear();
            }
            if (guest.keyboard_player == player) {
                guest.keyboard_player = -1;
            }
        }
        UpdateStatus();
    }

    void SetStatusItem(std::uint64_t item) {
        status_item = item;
        UpdateStatus();
    }

private:
    STEAM_CALLBACK(Session, OnConnected, SteamRemotePlaySessionConnected_t);
    STEAM_CALLBACK(Session, OnDisconnected, SteamRemotePlaySessionDisconnected_t);
    STEAM_CALLBACK(Session, OnGuestInvite, SteamRemotePlayTogetherGuestInvite_t);

    void AddGuest(RemotePlaySessionID_t session) {
        if (guests.contains(session)) {
            return;
        }
        Guest guest;
        guest.order = next_order++;
        const char* persona =
            SteamFriends()->GetFriendPersonaName(SteamRemotePlay()->GetSessionSteamID(session));
        const char* client = SteamRemotePlay()->GetSessionClientName(session);
        guest.name = persona != nullptr && persona[0] != '\0' ? persona
                     : client != nullptr                     ? client
                                                             : "Guest";
        const auto keyboard_name =
            "Remote Play keyboard " + std::to_string(session) + " (" + guest.name + ")";
        guest.keyboard = input->create_device(host->context, EGL_DEVICE_KEYBOARD,
                                              keyboard_name.c_str());
        // The guest plays with the emulated controller, not with a host mouse cursor.
        SteamRemotePlay()->SetMouseVisibility(session, false);
        Log(EGL_LOG_INFO, guest.name + " joined");
        guests.emplace(session, std::move(guest));
        AssignPads();
    }

    void RemoveGuest(RemotePlaySessionID_t session) {
        const auto it = guests.find(session);
        if (it == guests.end()) {
            return;
        }
        auto& guest = it->second;
        if (guest.pad_player >= 0) {
            input->release_player(host->context, guest.pad_player);
        }
        if (guest.keyboard_player >= 0) {
            input->release_player(host->context, guest.keyboard_player);
        }
        if (guest.keyboard != 0) {
            input->destroy_device(host->context, guest.keyboard);
        }
        Log(EGL_LOG_INFO, guest.name + " left");
        guests.erase(it);
        UpdateStatus();
    }

    void SyncSessions() {
        std::set<RemotePlaySessionID_t> current;
        const auto count = SteamRemotePlay()->GetSessionCount();
        for (std::uint32_t i = 0; i < count; ++i) {
            current.insert(SteamRemotePlay()->GetSessionID(static_cast<int>(i)));
        }
        std::vector<RemotePlaySessionID_t> gone;
        for (const auto& [session, guest] : guests) {
            if (!current.contains(session)) {
                gone.push_back(session);
            }
        }
        for (const auto session : gone) {
            RemoveGuest(session);
        }
        for (const auto session : current) {
            AddGuest(session);
        }
    }

    void ReadInput() {
        RemotePlayInput_t events[32];
        while (true) {
            const auto count = SteamRemotePlay()->GetInput(events, 32);
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto& event = events[i];
                const bool down = event.m_eType == k_ERemotePlayInputKeyDown;
                if (!down && event.m_eType != k_ERemotePlayInputKeyUp) {
                    continue;
                }
                const auto it = guests.find(event.m_unSessionID);
                if (it == guests.end() || it->second.keyboard == 0) {
                    continue;
                }
                auto& guest = it->second;
                // Remote Play scancodes are USB HID usages.
                input->set_key(host->context, guest.keyboard,
                               static_cast<std::uint32_t>(event.m_Key.m_eScancode), down ? 1 : 0);
                if (down && guest.keyboard_player < 0 && guest.pad_player < 0) {
                    AssignKeyboard(guest);
                }
            }
            if (count < 32) {
                break;
            }
        }
    }

    void AssignKeyboard(Guest& guest) {
        for (const auto& device : ListDevices()) {
            if ((device.flags & EGL_DEVICE_FLAG_OWN) != 0 && device.handle == guest.keyboard) {
                guest.keyboard_player =
                    input->assign_device(host->context, device.id, -1, guest.name.c_str());
                break;
            }
        }
        UpdateStatus();
    }

    void UpdateStatus() {
        if (status_item == 0) {
            return;
        }
        std::string text = "Remote Play: ";
        if (guests.empty()) {
            text += "no guests";
        } else {
            text += std::to_string(guests.size()) + (guests.size() == 1 ? " guest" : " guests");
        }
        ui->set_status_item_text(host->context, status_item, text.c_str());
    }

    std::map<RemotePlaySessionID_t, Guest> guests;
    std::set<std::string> host_pads;
    std::uint64_t status_item{};
    std::uint64_t next_order{};
    std::uint32_t ticks{};
    bool direct_input{};
};

void Session::OnConnected(SteamRemotePlaySessionConnected_t* event) {
    AddGuest(event->m_unSessionID);
}

void Session::OnDisconnected(SteamRemotePlaySessionDisconnected_t* event) {
    RemoveGuest(event->m_unSessionID);
}

void Session::OnGuestInvite(SteamRemotePlayTogetherGuestInvite_t* event) {
    ui->set_clipboard_text(host->context, event->m_szConnectURL);
    const std::string text = std::string{"The guest link was copied to the clipboard:\n\n"} +
                             event->m_szConnectURL;
    ui->show_message(host->context, EGL_MESSAGE_INFO, "Remote Play Together", text.c_str());
}

std::unique_ptr<Session> session;

void OnChangeAppId(void*) {
    const auto saved = GetSetting(*core, *host, "app_id", DefaultAppId);
    ui->prompt_text(
        host->context, "Steam App ID",
        "Steam App ID to run as. Guests need to own this game or have it free. Takes effect "
        "after a restart.",
        saved.c_str(),
        [](void*, const char* value) {
            const std::string text{value};
            if (!IsAppId(text)) {
                ui->show_message(host->context, EGL_MESSAGE_WARNING, "Steam App ID",
                                 "An App ID is a number, for example 480.");
                return;
            }
            core->set_setting(host->context, "app_id", text.c_str());
            ui->show_message(host->context, EGL_MESSAGE_INFO, "Steam App ID",
                             "Restart Eglantine to use the new App ID.");
        },
        nullptr);
}

/// Why Steam cannot be used, in words a user can act on.
std::string ExplainFailure() {
    std::string text;
    if (steam_result == k_ESteamAPIInitResult_NoSteamClient) {
        text = "Steam is not running. Start Steam, sign in, then restart Eglantine.\n\n"
               "On Linux, use the Steam package from Valve or your distribution: the Snap and "
               "Flatpak versions run in a sandbox that other programs cannot reach.";
    } else if (steam_result == k_ESteamAPIInitResult_VersionMismatch) {
        text = "Steam is out of date. Let Steam update, then restart Eglantine.";
    } else if (steam_result == k_ESteamAPIInitResult_OK) {
        text = "This Steam version has no Remote Play support. Update Steam, then restart "
               "Eglantine.";
    } else {
        text = "Steam refused to run Eglantine as App ID " + app_id +
               ". Your Steam account must own that game: add it to your library (free games "
               "too, with Play or Install on its store page), or choose another App ID that "
               "supports Remote Play Together with Multiplayer > Remote Play Together: Steam App "
               "ID. Then restart Eglantine.";
    }
    if (!steam_error.empty()) {
        text += "\n\nSteam said: " + steam_error;
    }
    return text;
}

void ShowFailure(void*) {
    ui->show_message(host->context, EGL_MESSAGE_WARNING, "Steam Remote Play Together",
                     ExplainFailure().c_str());
}

void AddMenus() {
    ui->add_menu_separator(host->context, EGL_MENU_MULTIPLAYER);
    if (steam_ready) {
        ui->add_menu_item(
            host->context, EGL_MENU_MULTIPLAYER,
            (std::string{MenuPrefix} + "Invite Friends...").c_str(),
            [](void*) { session->Invite(); }, nullptr);
        ui->add_menu_item(
            host->context, EGL_MENU_MULTIPLAYER,
            (std::string{MenuPrefix} + "Copy Guest Link").c_str(),
            [](void*) { session->RequestLink(); }, nullptr);
    } else {
        ui->add_menu_item(host->context, EGL_MENU_MULTIPLAYER,
                          (std::string{MenuPrefix} + "Steam is not available, details...").c_str(),
                          ShowFailure, nullptr);
    }
    ui->add_menu_item(
        host->context, EGL_MENU_MULTIPLAYER,
        (std::string{MenuPrefix} + "Steam App ID (" + app_id + ")...").c_str(), OnChangeAppId,
        nullptr);
}

} // namespace

EGL_EXPORT uint32_t egl_extension_api_version(void) {
    return EGL_API_VERSION;
}

EGL_EXPORT int egl_extension_preinit(const EglHost* early_host) {
    const auto* early_core =
        static_cast<const EglCoreApi*>(early_host->get_interface(early_host->context,
                                                                 EGL_CORE_INTERFACE, 1));
    if (early_core == nullptr) {
        return 1;
    }
    // A launch from Steam sets the App ID already.
    if (const char* from_steam = std::getenv("SteamAppId"); from_steam != nullptr) {
        app_id = from_steam;
    } else {
        app_id = GetSetting(*early_core, *early_host, "app_id", DefaultAppId);
        SetEnvIfUnset("SteamAppId", app_id);
        SetEnvIfUnset("SteamGameId", app_id);
    }
    // SDL hides Steam's virtual gamepads unless told otherwise, and those are the guests' pads.
    SetEnvIfUnset("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1");

    // Before any graphics device exists, so the Steam overlay can attach to the renderer.
    SteamErrMsg error{};
    steam_result = SteamAPI_InitEx(&error);
    if (steam_result == k_ESteamAPIInitResult_OK && SteamRemotePlay() != nullptr) {
        steam_ready = true;
        early_core->log(early_host->context, EGL_LOG_INFO,
                        ("Steam started as App ID " + app_id).c_str());
    } else {
        steam_error = error;
        early_core->log(early_host->context, EGL_LOG_WARNING,
                        ("Steam is not available: " + steam_error).c_str());
    }
    return 0;
}

EGL_EXPORT int egl_extension_init(const EglHost* host_) {
    host = host_;
    core = static_cast<const EglCoreApi*>(
        host->get_interface(host->context, EGL_CORE_INTERFACE, 1));
    ui = static_cast<const EglUiApi*>(host->get_interface(host->context, EGL_UI_INTERFACE, 1));
    input = static_cast<const EglInputApi*>(
        host->get_interface(host->context, EGL_INPUT_INTERFACE, 1));
    if (core == nullptr || ui == nullptr || input == nullptr) {
        return 1;
    }
    if (app_id.empty()) {
        app_id = GetSetting(*core, *host, "app_id", DefaultAppId);
    }

    if (steam_ready) {
        session = std::make_unique<Session>();
        session->SetStatusItem(ui->add_status_item(host->context, ""));
        core->add_timer(host->context, TickIntervalMs, [](void*) { session->Tick(); }, nullptr);
        core->subscribe(
            host->context, EGL_EVENT_INPUT_DEVICES_CHANGED,
            [](void*, const EglEvent*) { session->AssignPads(); }, nullptr);
        core->subscribe(
            host->context, EGL_EVENT_PLAYER_RELEASED,
            [](void*, const EglEvent* event) { session->OnPlayerReleased(event->player); },
            nullptr);
    }
    AddMenus();
    if (!steam_ready) {
        // Tell the user once per cause, after the main window is up, not on every launch.
        const auto cause = std::to_string(steam_result) + ':' + app_id + ':' + steam_error;
        if (GetSetting(*core, *host, "reported_failure", "") != cause) {
            core->set_setting(host->context, "reported_failure", cause.c_str());
            core->post_to_ui(host->context, ShowFailure, nullptr);
        }
    } else {
        core->set_setting(host->context, "reported_failure", nullptr);
    }
    return 0;
}

EGL_EXPORT void egl_extension_shutdown(void) {
    session.reset();
    if (steam_result == k_ESteamAPIInitResult_OK) {
        SteamAPI_Shutdown();
    }
    steam_ready = false;
}
