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
- Steam running and signed in, with an account that **owns the App ID** Eglantine runs as.

The [release archives](../../releases) include everything else, including Steam's runtime library
(`steam_api64.dll`, `libsteam_api.so` or `libsteam_api.dylib`, from Steamworks SDK 1.65).

## Build

Building needs only CMake and a C++20 compiler: the Steamworks SDK is not used. The extension
declares the few functions of Steam's flat C API it calls ([steam_runtime.h](steam_runtime.h)) and
loads Steam's runtime library from its own folder when Eglantine starts.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSTEAM_API_LIBRARY=/path/to/libsteam_api.so
cmake --build build
```

This produces `build/dev.eglantine.remote-play-together/` with the extension, its
`extension.json` and, when `STEAM_API_LIBRARY` is set, Steam's runtime library. That library must
come from Steamworks SDK 1.65 or later: take it from a release archive, from the SDK's
`redistributable_bin/` (or pass `-DSTEAMWORKS_SDK_DIR=/path/to/sdk` instead), or from the
[Steamworks.NET 2025.165.0](https://github.com/rlabrecque/Steamworks.NET/releases/tag/2025.165.0)
standalone archive, whose copies are identical to Valve's. Without it the build still succeeds,
and the extension reports the library as missing at startup.

Placed as `extensions/remote_play_together` inside an Eglantine source tree, it is built with
Eglantine instead and copied next to the executable.

## GitHub Releases (tags)

Pushing a tag builds the extension for Linux, Windows and macOS and publishes a GitHub Release
with one archive per platform. No secret is needed: the workflow downloads Steam's runtime library
from the pinned Steamworks.NET release and checks it against the SHA-256 of Valve's SDK 1.65 copy
before bundling it.

## Install and use

1. Copy the `dev.eglantine.remote-play-together` folder into Eglantine's extensions folder:
   `~/.local/share/eglantine/extensions/` on Linux, `%APPDATA%\eglantine\extensions\` on Windows
   (`user/extensions/` for portable builds).
2. In Eglantine, open **Extensions > Manage Extensions...**, enable **Steam Remote Play Together**,
   and restart: the extension starts Steam before the main window, so the overlay can attach.
3. Set the game to run as with **Multiplayer > Remote Play Together: Steam App ID...** and restart
   again.
   Use a Steam App ID that you own or are expressly authorized to use, configured for Remote Play
   Together. Prefer a game with multiplayer support; for example, RetroArch (`1118310`).
4. Use **Multiplayer > Remote Play Together: Invite Friends...** (Steam overlay) or
   **Copy Guest Link** (a link guests open in a browser or Steam Link, no friendship needed).

The status bar shows how many guests are connected. A guest's Steam virtual gamepad is assigned to
the first free player; a guest who only uses a keyboard gets a player at their first key press.
These assignments are temporary: they never change your saved controller settings.

## Troubleshooting

When Steam cannot be used, the extension says why at startup, and **Remote Play Together: Steam is
not available, details...** in the Multiplayer menu shows it again.

- **"Steam's runtime library is missing or too old"**: the folder lacks `steam_api64.dll` /
  `libsteam_api.so` / `libsteam_api.dylib`, or it predates SDK 1.65. Reinstall the folder from a
  release archive.
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
public extension header. Steam's runtime libraries are Valve's: they are not part of this
repository, only bundled in the release archives as Steamworks' redistributables.
