/* SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
 * SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * Eglantine extension API.
 *
 * An extension is a shared library next to an extension.json manifest in its own folder under
 * <data dir>/extensions. It exports:
 *
 *   uint32_t egl_extension_api_version(void);                  required, return EGL_API_VERSION
 *   int      egl_extension_init(const EglHost* host);           required, return 0 on success
 *   int      egl_extension_preinit(const EglHost* host);        optional, needs "early-start"
 *   void     egl_extension_shutdown(void);                      optional
 *
 * preinit runs before the windowing, graphics and input subsystems start. Only the logging,
 * folder and settings functions of egl.core work there. init runs once the main window exists.
 * shutdown runs when the application closes. Libraries are never unloaded while it runs.
 *
 * Everything the host calls into the extension, and every host function except post_to_ui, runs
 * on the UI thread. Strings are UTF-8 and only valid for the duration of the call.
 *
 * Functionality is grouped in interfaces fetched with EglHost::get_interface. An interface only
 * gains functions at the end of its struct; check struct_size before using one added later.
 */

#ifndef EGLANTINE_EXTENSION_H
#define EGLANTINE_EXTENSION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EGL_API_VERSION 1

#if defined(_WIN32)
#define EGL_EXPORT __declspec(dllexport)
#else
#define EGL_EXPORT __attribute__((visibility("default")))
#endif

#define EGL_CORE_INTERFACE "egl.core"
#define EGL_UI_INTERFACE "egl.ui"
#define EGL_INPUT_INTERFACE "egl.input"

typedef void (*EglCallback)(void* user);
typedef void (*EglStringCallback)(void* user, const char* value);

typedef struct EglHost {
    uint32_t struct_size;
    uint32_t api_version;
    /* Opaque, passed back as the first argument of every interface function. */
    void* context;
    /* Returns NULL if the interface or version is unknown, not declared in the manifest's
     * permissions, or not available in the current phase. */
    const void* (*get_interface)(void* context, const char* name, uint32_t version);
} EglHost;

/* ---------------------------------------------------------------------------------------------
 * egl.core, version 1. Always available.
 */

typedef enum EglLogLevel {
    EGL_LOG_DEBUG = 0,
    EGL_LOG_INFO = 1,
    EGL_LOG_WARNING = 2,
    EGL_LOG_ERROR = 3,
} EglLogLevel;

typedef enum EglEventType {
    /* The application is closing. Sent before egl_extension_shutdown. */
    EGL_EVENT_APP_CLOSING = 1,
    /* A game was loaded and is about to run. program_id and title are set. */
    EGL_EVENT_EMULATION_STARTED = 2,
    /* The running game is about to stop. */
    EGL_EVENT_EMULATION_STOPPING = 3,
    /* An input device was connected or disconnected. Use egl.input list_devices. */
    EGL_EVENT_INPUT_DEVICES_CHANGED = 4,
    /* A player assignment made by this extension ended without release_player being called,
     * for example because its device disconnected. player is set. */
    EGL_EVENT_PLAYER_RELEASED = 5,
} EglEventType;

typedef struct EglEvent {
    uint32_t struct_size;
    EglEventType type;
    uint64_t program_id;
    const char* title;
    int32_t player;
} EglEvent;

typedef void (*EglEventCallback)(void* user, const EglEvent* event);

typedef enum EglEmulationState {
    EGL_EMULATION_STOPPED = 0,
    EGL_EMULATION_RUNNING = 1,
    EGL_EMULATION_PAUSED = 2,
} EglEmulationState;

typedef struct EglEmulationInfo {
    uint32_t struct_size;
    EglEmulationState state;
    uint64_t program_id;
    /* Written into the caller's buffer, truncated and always NUL terminated. */
    char title[256];
} EglEmulationInfo;

typedef struct EglCoreApi {
    uint32_t struct_size;

    void (*log)(void* context, EglLogLevel level, const char* message);

    /* The folder holding extension.json and the library. Treat it as read only. */
    const char* (*get_extension_dir)(void* context);
    /* A writable folder owned by this extension. It is created on first use. */
    const char* (*get_data_dir)(void* context);

    /* Settings are strings stored per extension and saved when the application closes.
     * get_setting copies the value into buffer (truncated, NUL terminated) and returns its full
     * length, or -1 if the key is not set. set_setting with value NULL removes the key. */
    int32_t (*get_setting)(void* context, const char* key, char* buffer, size_t buffer_size);
    void (*set_setting)(void* context, const char* key, const char* value);

    /* Repeating timer. Returns 0 on failure. */
    uint64_t (*add_timer)(void* context, uint32_t interval_ms, EglCallback callback, void* user);
    void (*remove_timer)(void* context, uint64_t timer);

    /* The only function that may be called from any thread. The callback runs on the UI thread
     * later, and is dropped if the application is closing by then. */
    void (*post_to_ui)(void* context, EglCallback callback, void* user);

    /* Returns 0 on failure. */
    uint64_t (*subscribe)(void* context, EglEventType type, EglEventCallback callback, void* user);
    void (*unsubscribe)(void* context, uint64_t subscription);

    /* Returns 0 on success. */
    int32_t (*get_emulation_info)(void* context, EglEmulationInfo* info);
} EglCoreApi;

/* ---------------------------------------------------------------------------------------------
 * egl.ui, version 1. Needs the "ui" permission.
 */

typedef enum EglMenu {
    /* The Extensions menu, created for extensions. */
    EGL_MENU_EXTENSIONS = 0,
    EGL_MENU_FILE = 1,
    EGL_MENU_EMULATION = 2,
    EGL_MENU_VIEW = 3,
    EGL_MENU_MULTIPLAYER = 4,
    EGL_MENU_TOOLS = 5,
    EGL_MENU_HELP = 6,
} EglMenu;

typedef enum EglMessageKind {
    EGL_MESSAGE_INFO = 0,
    EGL_MESSAGE_WARNING = 1,
    EGL_MESSAGE_ERROR = 2,
} EglMessageKind;

typedef struct EglUiApi {
    uint32_t struct_size;

    /* Menu items are appended to the end of the menu. Ids are never 0. */
    uint64_t (*add_menu_item)(void* context, EglMenu menu, const char* label, EglCallback on_click,
                              void* user);
    uint64_t (*add_menu_separator)(void* context, EglMenu menu);
    void (*set_menu_item_label)(void* context, uint64_t item, const char* label);
    void (*set_menu_item_enabled)(void* context, uint64_t item, int32_t enabled);
    /* Makes the item checkable and sets its state. */
    void (*set_menu_item_checked)(void* context, uint64_t item, int32_t checked);
    void (*remove_menu_item)(void* context, uint64_t item);

    /* Permanent text in the status bar. */
    uint64_t (*add_status_item)(void* context, const char* text);
    void (*set_status_item_text)(void* context, uint64_t item, const char* text);
    void (*remove_status_item)(void* context, uint64_t item);

    /* Modal dialogs. They return once the user closed them. */
    void (*show_message)(void* context, EglMessageKind kind, const char* title, const char* text);
    /* Returns 1 for yes, 0 for no. */
    int32_t (*ask_question)(void* context, const char* title, const char* text);
    /* Calls on_accept with the entered text before returning, unless the user cancelled.
     * Returns 1 if accepted. */
    int32_t (*prompt_text)(void* context, const char* title, const char* label,
                           const char* initial_value, EglStringCallback on_accept, void* user);
    /* filter uses the Qt syntax, for example "Images (*.png *.jpg)". It may be NULL. */
    int32_t (*pick_open_file)(void* context, const char* title, const char* filter,
                              EglStringCallback on_accept, void* user);
    int32_t (*pick_save_file)(void* context, const char* title, const char* filter,
                              EglStringCallback on_accept, void* user);

    void (*set_clipboard_text)(void* context, const char* text);
    void (*open_url)(void* context, const char* url);
} EglUiApi;

/* ---------------------------------------------------------------------------------------------
 * egl.input, version 1. Needs the "input" permission.
 */

typedef enum EglDeviceKind {
    EGL_DEVICE_GAMEPAD = 1,
    EGL_DEVICE_KEYBOARD = 2,
} EglDeviceKind;

typedef enum EglButton {
    EGL_BUTTON_A = 0,
    EGL_BUTTON_B,
    EGL_BUTTON_X,
    EGL_BUTTON_Y,
    EGL_BUTTON_LSTICK,
    EGL_BUTTON_RSTICK,
    EGL_BUTTON_L,
    EGL_BUTTON_R,
    EGL_BUTTON_ZL,
    EGL_BUTTON_ZR,
    EGL_BUTTON_PLUS,
    EGL_BUTTON_MINUS,
    EGL_BUTTON_DPAD_LEFT,
    EGL_BUTTON_DPAD_UP,
    EGL_BUTTON_DPAD_RIGHT,
    EGL_BUTTON_DPAD_DOWN,
    EGL_BUTTON_SL,
    EGL_BUTTON_SR,
    EGL_BUTTON_HOME,
    EGL_BUTTON_CAPTURE,
} EglButton;

typedef enum EglStick {
    EGL_STICK_LEFT = 0,
    EGL_STICK_RIGHT = 1,
} EglStick;

#define EGL_DEVICE_FLAG_OWN 0x1u /* Created by the calling extension. */

typedef struct EglDeviceInfo {
    uint32_t struct_size;
    uint32_t flags;
    /* Stable while the device stays connected. Pass it to assign_device. */
    char id[256];
    char name[128];
    /* The backend, for example "sdl", "keyboard" or "extension". */
    char engine[32];
    /* USB ids when the backend knows them, 0 otherwise. */
    uint16_t vendor_id;
    uint16_t product_id;
    /* For devices created with create_device, the handle. 0 otherwise. */
    uint64_t handle;
} EglDeviceInfo;

typedef struct EglPlayerInfo {
    uint32_t struct_size;
    int32_t connected;
    /* 1 if the player is assigned by an extension. */
    int32_t assigned;
    /* 1 if the calling extension made that assignment. */
    int32_t assigned_by_me;
    /* The label of the assignment, or empty. */
    char label[64];
} EglPlayerInfo;

typedef struct EglInputApi {
    uint32_t struct_size;

    /* Devices created here appear next to the physical ones. Returns a handle, 0 on failure.
     * name should be unique within the extension: it identifies the device across sessions. */
    uint64_t (*create_device)(void* context, EglDeviceKind kind, const char* name);
    void (*destroy_device)(void* context, uint64_t device);
    void (*set_button)(void* context, uint64_t device, EglButton button, int32_t pressed);
    /* x grows to the right and y upwards, both in [-1, 1]. */
    void (*set_stick)(void* context, uint64_t device, EglStick stick, float x, float y);
    /* Gyro in revolutions per second, acceleration in g, delta_us since the previous sample. */
    void (*set_motion)(void* context, uint64_t device, float gyro_x, float gyro_y, float gyro_z,
                       float accel_x, float accel_y, float accel_z, uint64_t delta_us);
    /* usage is a USB HID keyboard usage id (page 0x07), for example 0x04 for A. */
    void (*set_key)(void* context, uint64_t device, uint32_t usage, int32_t pressed);

    /* Fills up to capacity entries and returns the number of devices. */
    uint32_t (*list_devices)(void* context, EglDeviceInfo* devices, uint32_t capacity);

    /* Players are numbered from 0 (Player 1). */
    uint32_t (*get_player_count)(void* context);
    int32_t (*get_player_info)(void* context, uint32_t player, EglPlayerInfo* info);

    /* Maps and connects a player to a device without touching the saved controller settings.
     * player -1 picks the first player from Player 2 that is neither connected nor assigned.
     * The assignment ends with release_player, when the device disconnects, or on shutdown, and
     * the player then goes back to its saved settings. Returns the player, or -1 on failure. */
    int32_t (*assign_device)(void* context, const char* device_id, int32_t player,
                             const char* label);
    void (*release_player)(void* context, int32_t player);
} EglInputApi;

/* ---------------------------------------------------------------------------------------------
 * Entry points, declared so the compiler checks an extension's definitions against them.
 */

EGL_EXPORT uint32_t egl_extension_api_version(void);
EGL_EXPORT int egl_extension_init(const EglHost* host);
EGL_EXPORT int egl_extension_preinit(const EglHost* host);
EGL_EXPORT void egl_extension_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* EGLANTINE_EXTENSION_H */
