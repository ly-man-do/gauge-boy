# Changelog

All notable changes to GaugeBoy are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [Semantic Versioning](https://semver.org/). Milestone releases: M0 = 0.0.x, M1 = 0.1.x, ... M7 = 1.0.0.

## [Unreleased]

### Added

- **LIVE screen** (text dashboard; the toy-like gauges come in M3): the LIVE card on HOME polls the chosen Mode 01 PIDs the car supports and shows each value in US units with its refresh rate, plus the adapter's battery voltage and manifold vacuum (barometric pressure from PID 33, or MAP read with the engine off, minus MAP). Defaults: RPM, speed, coolant, intake air, throttle, load, MAP, timing, MAF. The bottom screen has a toggle for every supported PID (19 decoded, including fuel trims for both banks), START / STOP and BACK; the choice is saved as `pids` under `[live]` in `config.ini`. PIDs that keep answering NO DATA are dropped; a lost connection is reconnected while polling.

## [0.1.0] - 2026-11-15

M1 release: GaugeBoy now reads the car. DIAG shows the check engine light, readiness monitors, stored / pending / permanent codes with plain-English descriptions (generic codes, plus manufacturer codes for 32 makes) and the VIN, and can clear codes behind a confirmation. Tested on both test cars over ISO 9141-2: a 2008 Honda Ridgeline (VIN read) and a 1996 Geo Tracker (no VIN over OBD, so the make is chosen in SETUP), with the Vgate iCar Pro Wi-Fi adapter (`V-LINK`). The LIVE, TRIM, FUN and LOG screens come in later versions.

### Added

- **New HOME screen**: drawn with graphics instead of text, with cream cards on a pale yellow background, color icons and the Nunito font (SIL OFL). Two status cards on top show the check engine light (amber when on; tap for DIAG) and whether the adapter is connected (tap for the Wi-Fi guide); six cards below, LIVE (Dash gauges), DIAG (Codes & CEL), TRIM (Fuel trims), FUN (Fun modes), LOG (Record drives) and SETUP (Settings), with a green outline on the last one tapped; a bar at the bottom shows START Close (tap it, or press START, to close GaugeBoy) or a short notice. METER is renamed LIVE.
- **Car make** in SETUP, for cars that don't report a VIN over OBD (such as the 1996 Geo Tracker test car): a picker with Auto and the 32 makes that have their own code meanings. The chosen make gives manufacturer codes their meanings, shows on DIAG as "Geo (set in SETUP)" and goes into the code history; a make read from the VIN always wins. Saved as `make` under `[vehicle]` in `config.ini`. The adapter test screen moved to an ADAPTER button next to BACK.
- **Wi-Fi setup guide** (SETUP > WI-FI GUIDE): five steps for saving the adapter's network with the manual IP and DNS GaugeBoy needs, with the values worked out from the adapter's address and shown the way the 3DS takes them (192.168.000.011). CHECK reports whether a slot is chosen and named, whether its IP and DNS match, whether the 3DS is on Wi-Fi and whether the adapter answers.
- **Code history**: every scan and every clear is appended to `sdmc:/3ds/gaugeboy/code-history.csv` (date, VIN, car, check engine light, type, code, description; one row per code, a "none" row for a clean scan), so codes are kept after they're cleared.
- **Clear codes** on the DIAG screen: CLEAR opens a question that explains it turns off the check engine light and resets the readiness monitors; only the red CLEAR CODES button clears (no button shortcut). GaugeBoy refuses if the car reports a speed above 0, then sends the clear and scans again, and reports when the car refuses (e.g. engine running).
- **HOME and DIAG screens** (the app now opens on HOME, a grid of six big buttons: LIVE, DIAG, TRIM, FUN, LOG, SETUP; DIAG and SETUP work in this version). DIAG: tabs STORED / PENDING / PERM / READY, a paged list of codes or readiness monitors (tap a row for its full description on the top screen), BACK, page < > and SCAN. SCAN connects to the car and reads the check engine light, readiness monitors, stored / pending / permanent codes (with descriptions for the car's make) and the VIN in one go; the top screen shows the facts (car, VIN, protocol, CEL, readiness, codes) and progress while scanning. Amber CEL lamp on the bottom screen. SETUP is a list like DIAG's: Wi-Fi slot (tap to change), check the slot's IP and DNS, save this session's log, and the adapter test screen, with BACK. B, X, L and R are shortcuts for BACK, SCAN and paging.
- The adapter test app's ATSP0+0100 button runs the full protocol search: after `0100` it reads the protocol (`ATDPN`) and the further supported-PID ranges (`0120`, `0140`, ...), and logs the protocol, how many ECUs answered and the list of supported PIDs for the compatibility list.
- The adapter test app's 0902 VIN button decodes the VIN for every protocol (multi-line ISO 9141-2 / J1850 / KWP replies and CAN) and logs it with the maker (from the first three characters) and model year, and warns if the check digit doesn't match.
- Plain-English descriptions for 9,342 generic trouble codes (P0, P2, P34-P3F, B0, B3, C0, C3, U0, U3), plus manufacturer-specific codes (P1, B1, C1, U1 and so on) for 32 makes, shown when the car's make is known. Other manufacturer codes show "Manufacturer-specific code". Descriptions come from [Wal33D/dtc-database](https://github.com/Wal33D/dtc-database) (MIT); see `CREDITS.md`.
- QR codes for installing with FBI (*Remote Install* > *Scan QR Code*): one at the top of the README that always installs the newest release, and one per release (`gaugeboy-qr.png`, also shown in the release notes) for that exact version. Made with `tools/make_qr.py`.
- Developer: ELM327 emulator scenarios for the non-CAN test cars (`ridgeline` and `tracker` on ISO 9141-2, `j1850` on J1850 VPW with codes set), now the default instead of the CAN `car`; `make emulator-check` runs a full DIAG scan on any protocol, and `tools/check-scenarios.sh` checks every scenario.

## [0.0.2] - 2026-11-15

Last M0 release: the 3DS now talks to a real Wi-Fi OBD-II adapter. Tested with a Vgate iCar Pro Wi-Fi adapter (ELM327 v2.3; its Wi-Fi network is called `V-LINK`) in a 2008 Honda Ridgeline. The 3DS normally drops a Wi-Fi network that has no internet, so GaugeBoy answers the 3DS's connection test itself. The adapter's Wi-Fi slot needs a manual IP and DNS; see `docs/testing-on-3ds.md` section 3.

### Added

- SETUP SLOT button on the adapter test screen: reads the chosen Wi-Fi slot's IP and DNS settings and, if they aren't what GaugeBoy needs, shows the current and needed values with where to enter them in System Settings. It does not change any settings: writing them from the app was tried and removed after it crashed the 3DS. The buttons are now in three columns.
- The adapter test app's start-up log shows each saved Wi-Fi slot's IP settings (automatic or manual IP, mask, gateway, DNS, MTU), read-only.
- SLOT button on the adapter test screen: choose which System Settings connection slot (any, 1, 2 or 3) CONNECT uses, so another saved network in range (such as home Wi-Fi) doesn't take over. Saved as `wifi_slot` in `config.ini`; the start-up log lists each slot's network name. Confirmed on a 3DS: it moves the 3DS from home Wi-Fi to the adapter's network in a few seconds.
- SAVE LOG button on the adapter test screen (between OLDER and NEWER): saves this session's log to `sdmc:/3ds/gaugeboy/logs/gaugeboy-log-<date>-<time>.txt` for sharing.
- GaugeBoy answers the 3DS's internet connection test itself: a background thread runs a tiny DNS server and the test page on the 3DS, so the 3DS keeps a Wi-Fi OBD-II adapter's network without a laptop or other device. The adapter's connection slot needs a manual IP with Primary DNS set to that same IP (see `docs/testing-on-3ds.md`). Confirmed on a 3DS with the Vgate iCar Pro adapter.
- `tools/fake_conntest.py`: a small DNS and web server that answers the 3DS's internet connection test, so a 3DS can stay on a Wi-Fi OBD-II adapter's network that has no internet. Dev tool for proving the fix; test steps in `tools/README.md`.
- CONNECT (and the automatic reconnect) first asks the 3DS to join a saved Wi-Fi connection if it isn't on one, the way Luma3DS's "Force wireless connection" does, to reach adapters that have no internet. The result, including the system's error codes on failure, is logged.
- Connection errors name the call that failed, for example `errno 115 at socket()`.
- The adapter test app logs a `Network:` line at start-up and before every connect attempt: whether the 3DS is on Wi-Fi, the network name and saved-connection slot, and the 3DS's own IP address (or "no IP address").

### Fixed

- GaugeBoy's connection-test web server answered every web request on the network (a file-transfer app polling the 3DS filled the log). It now serves the test page only to the 3DS's connection test, answers anything else with 404 and logs that once.
- CONNECT no longer asks the 3DS to join Wi-Fi when it is already on a network (the system's own join counts), which had made the 3DS rejoin and drop the adapter connection about 25 seconds later.
- Connecting to the adapter took the full 3-second timeout on the 3DS even when the adapter answered at once; a pending connect is now re-checked every 100 ms.
- "Wi-Fi state unknown (ac 0xE0A09D2E)" in the `Network:` line now reads "Wi-Fi on (joined by the system)" or "Wi-Fi not connected".
- On the 3DS, a TCP connect that was still in progress was reported as `errno 26: Text file busy` and abandoned. libctru returns the 3DS's raw socket error there (26 means "in progress"); GaugeBoy now translates it and keeps waiting until the connect timeout, then reports "Adapter did not answer".
- The `Network:` line numbered connection slots one too high, and "Wi-Fi state unknown" now includes the system's error code.
- Connection errors showed a negative errno with no description (for example `errno -115` when the 3DS had no network). They now show the positive value and its meaning (`errno 115: Network interface is not configured`).

## [0.0.1] - 2026-11-14

First test release of the M0 spike: an adapter test app for checking whether a New 3DS can talk to a Wi-Fi ELM327 adapter. It is not a finished diagnostics app yet.

### Added

- Project brief (`docs/gaugeboy-brief.md`) and agent instructions (`CLAUDE.md`).
- Beads issue tracking with milestone epics M0 to M7 and the M0 issue plan.
- README with the unofficial / not-affiliated statement and safety notice, MIT license, changelog and contributing guide.
- Project skeleton: `make` builds `gaugeboy.3dsx`, which enables New 3DS speedup mode, shows the startup safety notice, and shows an "unsupported device" screen and exits on Old 3DS models.
- Host unit tests: `make test` builds and runs `tests/` with the host C compiler using a small built-in assert harness.
- ELM327 driver (`source/elm327/`): sends commands, reads replies up to the `>` prompt with a timeout, cleans up echo, blank lines, `SEARCHING...`, garbage bytes and lower-case clone replies, and reports adapter errors (`?`, `NO DATA`, `UNABLE TO CONNECT`, `BUS INIT: ...ERROR`, `CAN ERROR`, `STOPPED` and others). Includes the `ATZ` / `ATE0` / `ATL0` / `ATSP0` start-up sequence and `ATRV` / `ATDPN` readers.
- TCP transport (`source/transport/`): connects to the adapter (default `192.168.0.10:35000`) with a connect timeout, reads and writes with timeouts so nothing blocks indefinitely, detects dropped connections and reconnects, backing off from 0.5 s up to 8 s while the adapter is unreachable. Starts the 3DS network service.
- M0 spike app: after the safety notice, the top screen shows a timestamped log and the bottom screen has touch buttons for CONNECT, ATZ, ATSP0+0100, ATDPN, ATRV and 0902 (VIN), plus log scrolling. Commands run on a worker thread; the log is also saved to `sdmc:/3ds/gaugeboy/spike-log.txt`.
- Settings file `sdmc:/3ds/gaugeboy/config.ini` with the adapter address (`[adapter]` `host` and `port`), created with defaults on first launch.
- `make cia` builds `gaugeboy.cia` (title ID `0004000003DEDF00`, product code `CTR-P-GBOY`) with an original gauge icon, banner and banner chime; the `.3dsx` uses the same icon. CI builds and uploads the `.cia` too.
- Guide for installing and testing on a real New 3DS (`docs/testing-on-3ds.md`).
- Release workflow: pushing a `v*` tag runs the tests, builds `gaugeboy.3dsx` and `gaugeboy.cia` and publishes them on a GitHub release under those fixed names, so the `releases/latest/download/` links always get the newest version. It stops if the tag, `source/version.h` and `CHANGELOG.md` disagree.

[Unreleased]: https://github.com/ly-man-do/gauge-boy/compare/v0.0.2...HEAD
[0.0.2]: https://github.com/ly-man-do/gauge-boy/compare/v0.0.1...v0.0.2
[0.0.1]: https://github.com/ly-man-do/gauge-boy/releases/tag/v0.0.1
