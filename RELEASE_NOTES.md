## What's new

- **Fix:** QR code login failed with "Login timed out" before the QR appeared, because the remote-auth handshake used the wrong reply field (`nonce` instead of `proof`). Scanning to log in now works.

- **QR code login:** sign in by scanning a QR code with the Discord mobile app (Discord's official remote-auth flow), instead of finding and pasting your token. Pasting a token still works.

- **ACTIVE QUESTS tab:** paste your Discord token once and the app loads your current quests with their progress.
- **Automatic game detection:** each quest is matched to its game via Discord's detectable games list; games missing from it are looked up through Discord's application API, and as a last resort the executable name is derived from the game title.
- **One-click START/STOP** of the fake game process right from the quest list (SELECT lets you pick a different executable).
- The token is stored encrypted with Windows DPAPI in `%APPDATA%\KayzeLauncher` and is sent only to discord.com.
- Auto-updater now follows this fork's releases.

## Download

Grab `KayzeLauncher.exe` below and run it (no installation needed). Windows 10/11 x64.
