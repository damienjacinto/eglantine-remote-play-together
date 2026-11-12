# Steam Remote Play Together

Lets Steam friends join local multiplayer games over Remote Play Together. Steam streams the
window and audio; each guest's gamepad and keyboard become a player, from Player 2 to Player 8.

> **Experimental.** Loading, Steam start-up and error reporting are tested on Linux. A full
> session with a guest playing, and the Windows build, are not yet.

## Requirements

Steam running and signed in, with an account that **owns the App ID** Eglantine runs as (480 by
default). Steam's runtime library must be in this extension's folder.

## Use

1. Enable the extension and restart: it starts Steam before the main window, so the overlay can
   attach.
2. Set the App ID with **Multiplayer > Remote Play Together: Steam App ID...** and restart again.
3. Invite people with **Multiplayer > Remote Play Together: Invite Friends...** (Steam overlay), or
   **Copy Guest Link** (a link guests open in a browser or Steam Link, no friendship needed).

The status bar shows how many guests are connected. A guest's virtual gamepad is assigned to the
first free player; a keyboard-only guest gets a player at their first key press. These assignments
are temporary and never change your saved controller settings.

## Troubleshooting

- **"Steam's runtime library is missing or too old"**: reinstall the folder from a release archive.
- **"ConnectToGlobalUser failed"**: the account does not own the App ID. Add the game to the
  library, restart Steam, then Eglantine.
- **Steam not found on Linux**: the Snap and Flatpak versions of Steam cannot be reached. Install
  Steam from Valve or your distribution.
- **Guests see "Steam not launched with -pipewire"** (Linux, Wayland): start Steam with
  `steam -pipewire` (or `-pipewire-dmabuf` if guests get a black screen).

More at [github.com/damienjacinto/eglantine-remote-play-together](https://github.com/damienjacinto/eglantine-remote-play-together).
