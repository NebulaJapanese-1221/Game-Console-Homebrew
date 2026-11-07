# VitaDucoMiner

Unofficial DuinoCoin miner homebrew for the PlayStation Vita (PS Vita / PSTV).

## What is this?

VitaDucoMiner is a GUI-based DuinoCoin mining application for the PSVita, built
with vita2d and the official VitaSDK. It is **not** affiliated with or endorsed by
the DuinoCoin project — it is a community homebrew implementation.

## Features

- **Basic GUI** — Dashboard, Settings, About, Donation, Activity Log screens
- **DUCO-S1 (SHA-1) proof-of-work mining** with real-time difficulty tracking
- **Live stats** — hashrate (H/s), accepted/rejected shares, difficulty, nonce, uptime
- **Connection status** — online/offline indicator, server:port, local IP
- **Activity log** — recent events and reject reasons (wrong difficulty, stale job, bad nonce, etc.)
- **Touchscreen controls** — tap buttons directly on the front touch panel
- **Controller controls** — D-pad navigate, X select/start-stop, Square menu/keyboard, Circle back
- **Native Vita keyboard** — use the system IME to enter server, port, account, and key settings
- **Persistent configuration** — settings saved to `ux0:/vitaducominer/config.json` (server, port, username, worker, mining key, donation address, auto-start)
- **Auto-start mining** — optionally begin mining immediately on launch
- **Worker name support** — mine as `username.worker`
- **Mining key** — optional DuinoCoin mining key for authenticated shares
- **Automatic pool discovery** — fetch pool from `http://server.duinocoin.com/getPool` (threaded, non-blocking)
- **Reconnection resilience** — exponential backoff auto-reconnect on connection loss
- **Donation screen** — display your DuinoCoin donation address
- **CMake-only build system** — no Makefiles, no SDL2

## UI Controls

| Button | Action |
|--------|--------|
| Touch | Tap buttons directly on front touch panel |
| D-pad Up/Down | Navigate menu items |
| D-pad Left/Right | Move cursor in text field (edit mode) |
| Square (edit mode) | Open the native Vita keyboard |
| X | Select field / Start-stop mining / Apply edited value |
| Square | Open menu (from dashboard) |
| Circle | Back / Cancel (from menus) |

## Build Requirements

- [VitaSDK](https://github.com/VitaSDK/VitaSDK) installed (`VITASDK` env var set)
- CMake 3.16+

## Building

```sh
export VITASDK=/path/to/vitasdk
./build.sh          # configure + build -> build/vitaducominer.vpk
./build.sh clean    # remove build directory (sce_sys is preserved)
./verify-build.sh   # clean build + verify output artifacts
```

The output `build/vitaducominer.vpk` can be installed on a PSVita (e.g. via
VitaShell or QCMA).

## Usage

1. Install the VPK on your PSVita.
2. Launch `VitaDucoMiner` from LiveArea.
3. On the **Dashboard**, tap **START MINING** or press **X** to begin mining.
4. Tap **SETTINGS** or press **Square** to open settings.
5. Tap a field to edit it, then tap **SAVE** to persist to `ux0:/vitaducominer/config.json`.
6. Tap **FETCH POOL** to auto-discover a DuinoCoin pool from `getPool`.
7. Tap **DONATE** to view your donation address.
8. Tap **QUIT** or press **Circle** to exit.

## Project Structure

```
CMakeLists.txt       # CMake-only build (Vita toolchain, SceNet/SceHttp/SceIo/SceCtrl stubs, self+vpk)
build.sh             # Build/clean wrapper
verify-build.sh      # Clean build verification script
readme.md            # This file
src/ui/main.c          # GUI main loop, vita2d rendering, config loading, controller + touch input
src/miner/miner.c      # Miner state, PoW loop, TCP client, reconnection with backoff
src/net/net.c          # SceNet TCP connect/send/recv + local IP via SceNetCtl
src/net/http/http.c    # SceHttp GET client
src/crypto/duco-s1.c   # SHA-1 + DUCO-S1 proof-of-work
src/util/json.c        # Minimal JSON key/value parser
src/pool/pool.c        # getPool discovery client (sync + threaded async)
src/config/config.c    # Persistent config save/load via SceIo (ux0:/vitaducominer/config.json)
sce_sys/             # VPK icons and LiveArea assets
```

## License

This project is unofficial DuinoCoin software. DuinoCoin and its logo are
trademarks of their respective owners. This homebrew is provided as-is with no
warranty. Use at your own risk.
