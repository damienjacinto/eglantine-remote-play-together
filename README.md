# Steam Remote Play Together for Eglantine

An [Eglantine](https://github.com/damienjacinto/Eglantine) extension that lets Steam friends join
local multiplayer games over Steam Remote Play Together. Steam streams the window and audio; each
guest's gamepad and keyboard become a player, from Player 2 to Player 8. Eglantine itself contains no
Steam code: this extension is loaded at runtime through Eglantine's extension API.

> **Status:** experimental. Loading, Steam start-up and error reporting are tested on Linux. A full
> session with a guest playing, and the Windows build, are not yet.

## Requirements

- Eglantine built with extension support (`ENABLE_EXTENSIONS`, the `extensions` branch of
  [damienjacinto/Eglantine](https://github.com/damienjacinto/Eglantine)).
- The [Steamworks SDK](https://partner.steamgames.com/downloads/list) 1.62 or later, to build. It
  is not included here: Valve does not allow redistributing it.
- Steam running and signed in, with an account that **owns the App ID** Eglantine runs as.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSTEAMWORKS_SDK_DIR=/path/to/steamworks_sdk/sdk
cmake --build build
```

This produces `build/dev.eglantine.remote-play-together/` with the extension, its
`extension.json` and Steam's runtime library (`libsteam_api.so`, `steam_api64.dll` or
`libsteam_api.dylib`).

Placed as `extensions/remote_play_together` inside an Eglantine source tree, it is built with
Eglantine instead and copied next to the executable.

## GitHub Releases (tags)

This repository includes a GitHub Actions workflow that builds release artifacts for Linux, Windows
and macOS when a tag is pushed, then publishes a GitHub Release with those artifacts.

Because Valve does not allow redistributing the Steamworks SDK, the workflow expects a repository
secret named `STEAMWORKS_SDK_URL` that points to a downloadable `.zip`, `.tar.gz` or `.tgz`
archive containing the Steamworks SDK (the folder that has `public/` and
`redistributable_bin/`).

## Install and use

1. Copy the `dev.eglantine.remote-play-together` folder into Eglantine's extensions folder:
   `~/.local/share/eglantine/extensions/` on Linux, `%APPDATA%\eglantine\extensions\` on Windows
   (`user/extensions/` for portable builds).
2. In Eglantine, open **Extensions > Manage Extensions...**, enable **Steam Remote Play Together**,
   and restart: the extension starts Steam before the main window, so the overlay can attach.
3. Set the game to run as with **Multiplayer > Remote Play Together: Steam App ID...** (480 by
   default) and restart again.
4. Use **Multiplayer > Remote Play Together: Invite Friends...** (Steam overlay) or
   **Copy Guest Link** (a link guests open in a browser or Steam Link, no friendship needed).

The status bar shows how many guests are connected. A guest's Steam virtual gamepad is assigned to
the first free player; a guest who only uses a keyboard gets a player at their first key press.
These assignments are temporary: they never change your saved controller settings.

## Troubleshooting

When Steam cannot be used, the extension says why at startup, and **Remote Play Together: Steam is
not available, details...** in the Multiplayer menu shows it again.

- **"ConnectToGlobalUser failed"**: the account does not own the App ID. Add the game to the
  library (free games too, with Play or Install on the store page), restart Steam, then Eglantine.
- **Steam not found on Linux**: the Snap and Flatpak versions of Steam are sandboxed and cannot be
  reached. Install Steam from Valve or your distribution (`steam-installer` on Ubuntu).
- **Guests see "Steam not launched with -pipewire"** (Linux, Wayland): start the host's Steam with
  `steam -pipewire` (try `-pipewire-dmabuf` if guests get a black screen).
- Steam does not say which virtual gamepad belongs to which guest. New Steam virtual gamepads go to
  guests without one, in the order they joined; the ones present while nobody is connected are the
  host's.

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). `include/eglantine/extension.h` is a copy of Eglantine's
public extension header. The Steamworks SDK and Steam's runtime libraries are Valve's and are not
part of this repository.
