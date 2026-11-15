# Project Brief: GaugeBoy: OBD-II Deluxe

**Version:** 1.5.0
**Product name:** GaugeBoy: OBD-II Deluxe (short name: GaugeBoy)
**Date:** 2026-11-14
**Status:** Final (baseline). Later changes get a new version and a changelog entry.
**Team:** Solo developer (project owner), with code written by Claude Code
**Task tracking:** Beads (`bd`)
**Timeline:** None set; milestones are completed in order, with no target dates

---

## 1. Summary

Build GaugeBoy: OBD-II Deluxe, a homebrew Nintendo 3DS application that connects to a car's OBD-II port through a wireless adapter. It reads live engine data and diagnostic trouble codes (DTCs) and shows them on the 3DS's two screens. The goal is a real tool that works in a real car, not a mock-up.

## 2. Goals

- Connect a 3DS to a vehicle's OBD-II system with off-the-shelf or low-cost hardware.
- Pull check engine light (CEL) codes: stored, pending and permanent codes, CEL on/off status and readiness monitors; clear codes with confirmation (see section 5d).
- Show fuel trims for both banks, short-term and long-term, as live gauges and a dedicated trim screen (see section 5d).
- Show live sensor data (Mode 01 PIDs): RPM, speed, coolant temp, throttle position, engine load, intake air temp and manifold vacuum, plus battery voltage (read from the adapter with `ATRV`, since PID 0x42 is often unsupported on older ECUs).
- Use the dual screens well: gauges on the top screen, touch controls and menus on the bottom.
- Make the app look and feel like a Nintendo 3DS game: bright, colorful, chunky low-poly models with thin dark outlines, toy-like gauges and a teal mascot car, with full stereoscopic 3D support on the top screen (see section 5b).
- All menu navigation happens by touch on the bottom screen.
- Include fun modes (timers, a G-meter, achievements and more) that make the app enjoyable beyond diagnostics (see section 5e).
- Include original music and sound effects in the style of a 3DS game (see section 5f).
- Let users install the app by scanning a QR code on the 3DS (see section 5c).
- Log sessions to the SD card as CSV for later review.
- Distribute as a `.3dsx` (Homebrew Launcher) and an installable `.cia`.
- Release as open source on a public Git repository (MIT license, provisional).

## 3. Non-Goals (v1)

- Manufacturer-specific enhanced PIDs, module coding or ECU flashing.
- Bluetooth adapters directly (the 3DS has no Bluetooth radio).
- Official Nintendo eShop release (homebrew only).
- Use while driving by the driver. The app should show a safety notice and favor glanceable displays.

## 4. Key Technical Constraint: Connectivity

The 3DS has no Bluetooth and no USB host mode, so the connection has to go over Wi-Fi.

| Option | How it works | Pros | Cons |
|---|---|---|---|
| **A. Wi-Fi ELM327 adapter (primary)** | Adapter creates its own Wi-Fi network. The 3DS joins it and opens a TCP socket (commonly `192.168.0.10:35000`) | Cheap, no custom hardware | Many cheap adapters are ad-hoc or use odd network settings the 3DS may refuse; clone quality varies |
| **B. ESP32 bridge (fallback)** | ESP32 runs a standard WPA2 access point and talks to the car through a CAN transceiver, or to a Bluetooth ELM327 | Full control, reliable 3DS-compatible Wi-Fi | Needs hardware build and firmware |
| C. IR port | 3DS infrared to a custom receiver | Novel | Very low bandwidth, poor documentation; not recommended |

**Decision for v1:** Option A only. Option B (ESP32 bridge) is a contingency, not v1 scope: it gets built only if the M0 spike shows that no tested Wi-Fi adapter works reliably with the New 3DS. The transport layer stays abstract so the bridge could be swapped in later with no UI changes.

**Go / no-go rule (end of M0):** If at least one widely available Wi-Fi ELM327 adapter connects and answers `ATZ` reliably, continue with Option A. If none do, pause the milestones and start the ESP32 bridge before M1.

**Early risk spike:** Test 2 to 3 Wi-Fi ELM327 adapters for 3DS compatibility before building the UI.

**M0 Wi-Fi checklist (per adapter):**
- The 3DS radio is 2.4 GHz 802.11b/g only and cannot join ad-hoc networks; note each adapter's mode and security (open / WEP / WPA2).
- Many adapters run no DHCP server. Set a manual IP on the 3DS in the adapter's subnet (e.g. `192.168.0.11`, mask `255.255.255.0`).
- The 3DS connection test will fail because the adapter has no internet. Confirm the connection slot stays saved anyway and the 3DS rejoins it.
- The 3DS has only 3 connection slots; record which slot the adapter uses.
- Close and reopen the lid (sleep) and confirm the app reconnects.

## 5. Technical Approach

- **Toolchain:** devkitPro / devkitARM, libctru, citro2d / citro3d for graphics.
- **Networking:** libctru `soc` service (BSD-style sockets) over TCP.
- **Architecture layers:**
  1. Transport (TCP socket; later ESP32 variant)
  2. ELM327 driver (AT command setup: `ATZ`, `ATE0`, `ATL0`, `ATSP0`, response parsing, timeouts)
  3. OBD-II protocol (PID requests, decoding formulas, DTC parsing)
  4. Data model (latest values, history buffer, logger)
  5. UI (top screen gauges, bottom screen touch menus)
- **Threading:** Polling runs on its own thread so the UI stays smooth.
- **Target hardware:** New 3DS family only. The app enables the 804 MHz clock and L2 cache (`osSetSpeedupEnable`) and may use the extra memory and CPU core. On an Old 3DS it shows an "unsupported device" message and exits.
- **Config:** Saved to `sdmc:/3ds/gaugeboy/config.ini` (adapter IP/port, units, selected PIDs, per-vehicle settings such as engine displacement).
- **Connection loss:** Wi-Fi drops when the 3DS sleeps (lid closed); the transport reconnects on wake without restarting the app.

## 5a. Open Source Setup

- **License:** MIT (provisional; easy to change before the first public release). It is compatible with libctru (zlib) and citro2d/citro3d (zlib).
- **Repository:** Public GitHub repo with `README.md`, `LICENSE`, `CHANGELOG.md`, and `CONTRIBUTING.md`.
- **Versioning:** Semantic versioning (`MAJOR.MINOR.PATCH`), with tagged releases matching the milestones in section 6.
- **Builds:** GitHub Actions using the official devkitPro Docker image to build `.3dsx` and `.cia` on every tag.
- **Community input:** An issue template for adapter and vehicle compatibility reports, feeding the compatibility list.
- **No proprietary data:** DTC descriptions limited to the generic SAE J2012 codes (P0xxx etc.); no copied manufacturer databases.

## 5b. Visual Direction: 3DS Game

**Mood:** GaugeBoy should look and feel like a game made for the Nintendo 3DS: bright, clean and playful, with chunky low-poly 3D models, colorful textures and big, friendly touch menus.

**Reference points (for mood only):**
- 3DS games' menus, HOME menu banners and vehicle models, studied only for size and structure (triangle counts, texture sizes, how models are split into parts)
- The owner's asset style guide (kept outside the repo), which lists every asset the app needs with sizes and budgets

**Style: 3DS-game 3D.**
- **Models:** Chunky, slightly toy-like low-poly models with colorful textures and vertex colors, lit from the top left, with an optional shine highlight. Every model has thin dark outlines. Budgets: about 1,000 to 1,500 triangles for the mascot car (plus a low-detail version of about 300), about 200 to 400 per gauge, and under about 10,000 for the whole top-screen scene; textures usually 256x256 or smaller.
- **Gauges:** Chunky, toy-like dials in the style of a 3DS game, not realistic tuner gauges: thick rounded bezels, bold numerals, a big needle and a glossy glass highlight.
- **Digital readouts:** Bold, rounded digits on simple LCD-style panels.
- **Controls (bottom screen):** Big, rounded, colorful buttons that visibly press in, each with an icon, plus toggles and sliders in the same style.
- **Lighting:** Consistent light source (top-left) across models and UI art.

**Mascot car:** A teal toy version of a 1995 Suzuki Sidekick (a compact SUV with a boxy body, upright windshield, black fender flares and bumpers, and a spare tire on the tailgate), based on the shape of the real car: a chunky low-poly model with thin dark outlines and no maker names, badges or logos. It appears on the HOME menu icon and banner, spins in 3D on the top-screen title, drives through the attract mode and is the garage card picture until the user takes a photo.

**Design language:**
- **Colors:** Warm tan-orange backdrop (`#C58B4C`) with a darker tan (`#876B39`) for patterns and panel edges; near-black ink (`#1A1610`) for outlines and linework; lime green (`#8FD42A`, highlight `#C6F27A`, shadow `#5E9A1A`) as the main UI accent; teal (`#2BAE9F`, highlight `#7ADBCD`, shadow `#1A7A70`) for the mascot car; navy-to-blue glass (`#323868` to `#6C7AC8`); light neutrals for trim and panels. Signal colors stay for function only: amber for the CEL warning, red for redline, green for live values.
- **Type:** A rounded, bold display font for headings; a clean UI font for menus and lists; bold digit styles for live values. Labels are in English only; there are no Japanese sub-labels.
- **Gauges:** Mix of chunky analog dials, bar graphs and digital readouts; peak-hold markers; redline flash.
- **Motion:** Bouncy, springy transitions; startup "sweep" where needles go to max and back; the selected item pulses.
- **Sound:** Full music and sound effects; see section 5f.

**Stereoscopic 3D (v1 scope):**
- The top screen supports the 3D slider; the bottom screen is always 2D (hardware limit).
- Each gauge is built as layers at different depths: housing and bezel at the back, gauge face, needle above the face, glass and highlight on top. The backdrop is split into depth layers too.
- Pop-up alerts (redline, warning codes) appear slightly in front of the screen.
- Depth scales with the slider; slider at zero gives a clean 2D view.
- Option in SETUP to cap the maximum depth for comfort.
- Rendered with citro3d stereo (separate left and right eye images); the New 3DS speedup mode keeps both eyes at 60 fps.

**Navigation (touch only):**
- All menus are selected by tapping the bottom screen. Buttons are large (at least 40x40 px) so they work with a finger, not just the stylus.
- Pressed buttons show a press-in animation, a click sound and a light-up state.
- The top screen is display only; it never needs input.
- Physical buttons are optional shortcuts only (for example B for back, L/R to switch gauge layouts); every function can be reached by touch.

**Screen layout:**
- **Top screen (400x240, 3D):** Main gauge cluster on the orange backdrop; selectable layouts (single big tach, 4-gauge grid, data list).
- **Bottom screen (320x240, touch):** Touch panel of big rounded buttons for METER / DIAG (CEL codes) / TRIM (fuel trims) / FUN (fun modes) / LOG / SETUP, with sub-menus on the same panel. An amber CEL warning lamp on the panel lights when the CEL is on.

**Brand and IP rule:** The app looks and feels like a 3DS game but uses no Nintendo or third-party characters, logos, trademarks, brand names, fonts, UI art, models, textures, music or sounds. Ripped game assets may be studied for size and structure only; they are never shipped, traced or converted. All artwork, models, icons and the app logo are original. Fonts must be open-licensed (e.g. SIL OFL) so they can ship in the open-source repo.

**Theme system:** Colors and gauge styles live in a theme file on the SD card, so alternate color schemes can be added without code changes.

## 5c. Distribution and QR Code Install

**User flow:**
1. User opens FBI (the standard homebrew title manager) on their 3DS.
2. Chooses *Remote Install* then *Scan QR Code*.
3. Points the 3DS camera at the QR code from the project's README or release page.
4. FBI downloads and installs the `.cia`; the app appears on the HOME Menu.

**Setup:**
- **Stable link:** QR code points to a permanent "latest release" URL (GitHub's `releases/latest/download/<file>.cia` form), so the same QR code always installs the newest version. This only works if release asset filenames never change, so they carry no version number (`gaugeboy.cia`, `gaugeboy.3dsx`).
- **Title ID:** The `.cia` uses a unique title ID chosen once in M0 (owner decision) and never changed, so updates install over the previous version. Built with `makerom` and `bannertool`.
- **Per-release QR:** CI also creates a QR code for each tagged version, attached to the release, for users who want a specific version.
- **Placement:** QR code shown at the top of the README and on each release page.
- **Fallback:** Manual download instructions for `.cia` and `.3dsx` (copy to SD card).
- **Updates (stretch):** Listing in Universal-Updater, so users can update from the 3DS directly.

**Risk to test early:** The 3DS's built-in networking supports only older TLS versions and limited certificates, so some HTTPS download links may fail in FBI. Test the GitHub link during M0. If it fails, use a URL shortener or a plain-HTTP mirror that redirects to the release file.

## 5d. CEL Codes and Fuel Trims

### CEL codes (DIAG screen)

| Data | OBD-II request | Notes |
|---|---|---|
| CEL on/off and number of codes | Mode 01, PID 01 | Drives the amber CEL lamp on the bottom panel |
| Stored (confirmed) codes | Mode 03 | The codes that turned the CEL on |
| Pending codes | Mode 07 | Faults seen once but not yet confirmed; early warning |
| Permanent codes | Mode 0A | Required on 2010+ US vehicles; likely not supported on the 2008 Ridgeline, so the app hides this tab if the car doesn't answer |
| Readiness monitors | Mode 01, PID 01 | Complete / incomplete status for catalyst, O2 sensor, EVAP, etc.; useful before an emissions test |
| Clear codes | Mode 04 | Confirmation dialog explains it turns off the CEL and resets readiness monitors |

- **Display:** Each code shown with its number (e.g. P0171), type (stored / pending / permanent), and a plain-English description from the generic SAE J2012 list. Codes without a generic description (manufacturer-specific, e.g. P1xxx) are shown as "Manufacturer-specific code".
- **Top screen:** "Fault display" panel listing the codes, in the same toy-like style as the gauges.
- **Logging:** Codes are saved with a timestamp to the SD card each time they are read, so the user keeps a history after clearing.
- **Freeze-frame data** (Mode 02, the snapshot taken when a code set) stays a stretch goal.

### Fuel trims (TRIM screen)

| Data | PID | Notes |
|---|---|---|
| Short-term fuel trim, Bank 1 | 0x06 | Fast correction, changes constantly |
| Long-term fuel trim, Bank 1 | 0x07 | Learned correction |
| Short-term fuel trim, Bank 2 | 0x08 | Ridgeline V6 has two banks |
| Long-term fuel trim, Bank 2 | 0x09 | |
| Total trim per bank | Calculated | Short-term + long-term |

- **Range:** -100% to +99.2%; gauges show -25% to +25% by default, with an over-range indicator.
- **Gauges:** Center-zero meters (needle or LED bar resting in the middle) for each bank, side by side, with short-term and long-term on the same gauge (two needles or two bars).
- **Color zones:** Within ±10% green, ±10–20% amber, beyond ±20% red.
- **Hints:** Simple guidance under the gauges, e.g. "Both banks positive at idle: possible vacuum leak" or "One bank only: check that bank's O2 sensor or injectors". Shown as hints, not diagnoses.
- **Bank detection:** The app checks supported PIDs and shows one bank only on inline engines.
- **Link to codes:** If lean or rich codes (P0171 / P0172 / P0174 / P0175) are stored, the DIAG screen offers a button to jump to the TRIM screen.

## 5e. Fun Modes (FUN menu)

**Safety rule for all fun modes:** Performance timers are for closed courses, track days and passenger use only. Each timer shows a one-time warning, the app has no online leaderboards, and no mode asks the driver to look at or touch the 3DS while moving.

| Mode | What it does | Data source |
|---|---|---|
| **Time Attack timers** | 0–60 mph (or 0–100 km/h), 60–0 braking, and 1/4-mile estimate. Armed while stopped, with an arcade-style "3, 2, 1, GO" countdown; timing starts automatically on the first speed change, so no touch is needed once moving. Big results screen with personal bests | Vehicle speed (PID 0x0D), polled alone during a run as fast as the adapter allows, with crossing times interpolated between samples |
| **Shift light** | Full-width row of LEDs on the top screen that fills as RPM rises and flashes at a user-set shift point, like an aftermarket shift light | RPM (PID 0x0C) |
| **G-meter** | Live G-force ball and peak-hold readout, styled as a chunky toy-like dial | 3DS built-in accelerometer; user mounts the 3DS and taps "Calibrate" while stopped |
| **Rev Sync (parked only)** | Mini game: the needle sweeps on screen and you blip the throttle to match the target RPM. Scored in a 2000s arcade style. Locked unless vehicle speed is 0 | RPM, vehicle speed |
| **Eco Challenge** | Live fuel economy estimate with a score and rank (C to S) for smooth driving over a trip | MAF (PID 0x10), or if MAF isn't supported, estimated from MAP, RPM and intake temp using the engine displacement from the vehicle settings and an assumed volumetric efficiency |
| **Garage card** | Collectible-style "car card" for each vehicle: VIN-decoded manufacturer (from the WMI) and model year (VIN position 10), user-entered model name and nickname, total logged distance, personal bests and a photo taken with the 3DS camera (the mascot car until a photo is taken). Decoding model names needs a database and stays out of v1 unless the owner approves a public-domain source (e.g. NHTSA vPIC) | VIN (Mode 09), logs, 3DS camera |
| **Achievements** | Unlockable stickers for milestones, e.g. "First Scan", "Clean Bill of Health" (no codes, all monitors ready), "Code Breaker" (first code cleared), "100 Miles Logged". Unlocks are saved to the SD card | App events |
| **Attract mode** | When parked and idle, the screens play a demo sequence like a 2000s arcade cabinet: gauges sweep, logo spins in 3D, the mascot car drives across the top screen, music plays | None (screensaver) |

- **Results screens:** Timer and challenge results use big arcade-style text ("NEW RECORD!") with matching sound effects, and can be saved as a screenshot to the SD card.
- **Accuracy note:** OBD speed updates a few times per second, so timer results are estimates. The app labels them as estimates.
- **Ridgeline note:** Rev Sync, timers, shift light and G-meter all work on the Ridgeline. The boost gauge doesn't apply (no turbo). Whether Eco Challenge uses MAF or the MAP-based estimate is decided by the M2 supported-PID scan.

## 5f. Music and Sound Effects

**Style:** Original music in the style of a 3DS game soundtrack: bright, catchy and melodic, with bouncy rhythms and playful instruments (for example light pop, funk, jazz fusion and chiptune-flavored synths), mixed to sound good on the 3DS speakers. Menu sounds are short, soft and cheerful. No melodies, samples or sound effects are taken from real games.

**Music:**
- Separate tracks for the main menu, gauge screens, fun modes, results screens and attract mode.
- In-app jukebox in SETUP to pick, skip or shuffle tracks.
- Users can add their own tracks by putting `.ogg` files in `sdmc:/3ds/gaugeboy/music/`.
- Music keeps playing across menu changes and loops cleanly.

**Sound effects:**

| Event | Sound |
|---|---|
| Button press / menu change | Soft click, then a cheerful "blip" or slide "swoosh" |
| Confirm / cancel | Rising or falling two-tone beep |
| Startup gauge sweep | Rising whoosh that follows the needles |
| Connected to car | "Ready" chime |
| CEL on / new code found | Warning chime (always plays, even if music is muted) |
| Shift point / redline | Fast beeps or buzzer |
| Timer countdown | "3, 2, 1, GO" arcade beeps |
| New record / achievement | Short fanfare jingle |

**Audio settings:** Separate volume sliders for music and sound effects; master mute; option for warnings to lower the music volume while they play. Master mute silences music and effects but never the CEL warning chime.

**Technical:**
- Played through the 3DS DSP (libctru `ndsp`); music streamed from the app's built-in files or the SD card in Ogg Vorbis to save memory; sound effects loaded once as short WAV clips.
- Audio runs on its own thread so it never stutters while the app polls the car.
- Requires DSP firmware (`dspfirm.cdc`) dumped on the user's 3DS (a standard homebrew setup step). If it's missing, the app runs silently and shows a one-time notice; the CEL warning is then shown visually instead (flashing CEL lamp and a top-screen pop-up).

**Music sourcing (to be finalized):** A mix of free licensed tracks (CC0 / CC-BY), original tracks by the developer and commissioned tracks. Exact track list and the balance between sources are still to be decided. Free licensed tracks are used first so audio work isn't blocked.

**Licensing (open source):** All music and sounds must be original or under a license that allows redistribution (e.g. CC0 or CC-BY), with credits in the README and an in-app credits screen. No music or sounds taken from real games.

## 5g. Development Workflow: Claude Code and Beads

**Roles:**
- **Claude Code** writes the code, tests, build scripts, CI and docs, and tracks its work in Beads.
- **Project owner** sets priorities, reviews and merges changes, does all real-hardware testing (3DS and the Ridgeline), and approves art, music and the go / no-go decision.

**Beads setup:**
- Beads (`bd`) is a git-backed, dependency-aware issue tracker built for AI coding agents. Issues live in the repo's `.beads/` folder, not on a website.
- Run `bd init` once in the repo. Optionally install the Beads Claude Code plugin for its slash commands.
- `CLAUDE.md` tells Claude Code to use `bd` for all task tracking instead of markdown TODO lists, and to follow the rules below.
- The README notes that `.beads/` is the agent's issue tracker and can be ignored by people not using Beads.

**How work is organized:**
- Each milestone (M0 to M7) is a Beads **epic**; features are issues under it.
- Dependencies are recorded with `bd dep add`, so `bd ready` only shows work that is unblocked. Example: every M1 issue is blocked by the M0 go / no-go issue.
- Labels: `transport`, `elm327`, `obd`, `ui`, `3d`, `audio`, `fun-mode`, `ci`, `docs`, plus `needs-hardware` and `needs-owner`.
- Contingency work (ESP32 bridge) is filed as a deferred epic, only opened if M0 fails.

**Rules for Claude Code (go in `CLAUDE.md`):**
1. Start each session with `bd ready` and claim one issue before writing code.
2. File any bug or follow-up found during work as a new issue linked to the current one, instead of fixing everything at once.
3. Close an issue only when its code builds and its tests pass; the closing note says what changed.
4. Anything that needs a real 3DS, the car or an owner decision gets the `needs-hardware` or `needs-owner` label and is left for the owner; Claude Code never marks those done itself.
5. Keep `CHANGELOG.md` and version numbers updated with each closed milestone.

**Testing Claude Code can do without hardware:**
- The ELM327 driver and OBD-II decoding are written as plain C with no 3DS dependencies, so they compile and run as unit tests on a normal computer.
- An ELM327 emulator on the computer stands in for the car during development, over the same TCP connection.
- Every change must pass a full devkitPro build in CI.
- UI layout can be checked in a 3DS emulator; real 3D depth, Wi-Fi to the adapter, audio and timers are verified by the owner on a New 3DS.

## 6. Features by Milestone

| Milestone | Version | Scope |
|---|---|---|
| M0: Spike | 0.0.x | Join adapter Wi-Fi, open socket, send `ATZ`, print reply on screen; test FBI QR install from a GitHub release link |
| M1: Core read | 0.1.x | Auto protocol detect, read VIN, CEL status, stored / pending / permanent codes with descriptions, readiness monitors, clear codes, code history log |
| M2: Live data | 0.2.x | Polling of selected PIDs, supported-PID discovery, text dashboard, fuel trims for both banks (short-term, long-term, total) |
| M3: UI polish | 0.3.x | 3DS-game visual style: chunky toy-like gauges with outlines, mascot car on the title screen, layered stereoscopic 3D on the top screen, touch control panel with pressable buttons, startup needle sweep, theme file, metric/imperial toggle, menu sounds |
| M4: Logging | 0.4.x | CSV logging to SD, session list, simple graph replay |
| M5: Audio | 0.5.x | Sound effects for all UI events and alerts, music playback, jukebox, user music folder, volume settings, credits screen |
| M6: Fun modes | 0.6.x | Time Attack timers, shift light, G-meter, Rev Sync, Eco Challenge, garage card, achievements, attract mode |
| M7: Release | 1.0.0 | `.cia` build, QR code install (stable and per-release), README, compatibility list, safety notice |
| Contingency | n/a | ESP32 bridge firmware, triggered only by the M0 go / no-go rule |
| Stretch | 1.x | Freeze-frame data (Mode 02), O2 sensor tests |

## 7. Hardware and Test Setup

- New 3DS / New 3DS XL / New 2DS XL with custom firmware (Luma3DS) and Homebrew Launcher (Old 3DS family not supported in v1)
- 2 to 3 Wi-Fi ELM327 adapters (different brands, v1.5 and v2.1 clones)
- **Primary test vehicle: 2008 Honda Ridgeline** (3.5L V6, naturally aspirated). Notes:
  - Expected protocol: ISO 15765-4 CAN (11-bit, 500 kbps), since US 2008 model-year vehicles were required to use CAN. Confirm in M0 with `ATDPN` after auto-detect.
  - No turbo, so there is no boost reading. The boost-style gauge will show manifold vacuum instead, which keeps the boost-style dial. PID 0x0B gives absolute pressure, so vacuum = barometric pressure − MAP. Use barometric pressure from PID 0x33 if supported; otherwise use the MAP reading taken at key-on, engine-off.
  - Battery voltage comes from the adapter (`ATRV`), since PID 0x42 may not be supported.
  - Run `0100`/`0120`/`0140` supported-PID scans in M1 and record the results in the compatibility list.
  - Honda-specific data (e.g. VTEC status, transmission temperature) needs enhanced PIDs and stays out of v1.
- Second test vehicle: to be decided, ideally a different make with a turbo, to test the boost gauge and a second protocol
- Optional: an OBD-II simulator or ELM327 emulator on a PC for bench testing without a car
- Contingency only (do not buy unless M0 fails): ESP32 dev board plus CAN transceiver (SN65HVD230 or similar) for Option B

## 8. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| 3DS can't join adapter's Wi-Fi | Blocks project | Test early (M0) with the Wi-Fi checklist in section 4; use ESP32 bridge as fallback |
| Wi-Fi drops when the 3DS sleeps | Lost connection mid-session | Transport reconnects on wake; tested in M0 |
| Clone ELM327 bugs or missing commands | Bad or missing data | Defensive parsing, timeouts, compatibility list |
| Slow polling rate | Laggy gauges | Ask for only the PIDs shown; batch requests where supported |
| Driver distraction | Safety | Startup warning; passenger and parked use only |
| Clearing codes by mistake | Turns off check-engine light, resets readiness monitors | Confirmation dialog with explanation |
| QR install link fails in FBI (old TLS) | Users can't install by QR | Test in M0; fall back to a redirecting short link or HTTP mirror |
| "GaugeBoy" name is close to Nintendo's Game Boy trademark | Trademark complaint or repo takedown | Original logo and lettering only (no Game Boy-style logo, fonts, colors or console art); README states the project is unofficial and not affiliated with Nintendo; keep a backup name ready |
| Claude Code marks hardware-dependent work as done without a real test | Bugs only found in the car | `needs-hardware` label; only the owner closes those issues |
| Agent loses track of long-running work across sessions | Repeated or half-finished work | Beads as the single task list; `bd ready` at the start of every session |
| UI looks too close to a real brand | Trademark complaints, repo takedown | Original logo and art only; no brand names or marks; open-licensed fonts |
| 3DS-game look plus the GaugeBoy name reads as an official Nintendo app | Trademark complaint or repo takedown | Original models, art and fonts only; nothing copied, traced or converted from Nintendo games; no Nintendo characters or UI elements; README disclaimer |
| Solo developer scope creep (3D models and art are time-consuming) | Delayed release | Keep text dashboard working first (M2); visual style in M3 builds on it; build one finished gauge in full detail before making the rest; stretch items stay out of 1.0 |
| Fun modes encourage street racing | Safety, reputation | Closed-course warning, no online leaderboards, Rev Sync locked unless stopped, no touch input needed while moving |
| Slow OBD speed updates | Inaccurate timers | Poll speed only during timer runs; label results as estimates |
| Sourcing original music as a solo developer | Delayed audio milestone | Start with CC0 / CC-BY tracks from open music libraries; commission or compose originals later; user music folder covers the gap |
| Music, gauges and polling all at once | Stutter or frame drops | Audio and polling on separate threads; stream music instead of loading it into memory |
| 3D rendering cost | Frame drops in 3D mode | Pre-rendered textures for static layers (bezel, face, glass); only needles and digits redrawn each frame |
| Detailed textures use too much memory | Crashes or slow loading | Compressed texture formats; reuse shared materials (plastic, chrome, glass) across gauges |

## 9. Success Criteria

- Connects to at least one widely sold Wi-Fi adapter on stock Luma3DS setup.
- Reads and clears DTCs correctly on the 2008 Honda Ridgeline and at least one other vehicle.
- CEL status, stored and pending codes match a reference scan tool on the Ridgeline.
- Fuel trims for both Ridgeline banks match a reference scan tool within 1%.
- Shows at least 8 live PIDs with 2 Hz or faster refresh.
- Runs 30 minutes without a crash or memory leak.
- A fresh New 3DS with FBI installs the app by scanning the README QR code alone.
- UI holds a steady 60 fps on the top screen while polling, with the 3D slider at maximum.
- Every menu and function can be reached by touch alone.
- Music and sound effects play with no stutter while gauges update at 60 fps.
- 0–60 timer result on the Ridgeline lands within 0.5 s of a GPS-based timer app.

## 10. Open Questions

1. ~~Target hardware~~ Resolved in 0.2.0: New 3DS family only.
2. ~~ESP32 bridge scope~~ Resolved in 0.3.0: backup only, triggered by the M0 go / no-go rule.
3. ~~Open source or private~~ Resolved in 0.4.0: open source, MIT license (provisional).
4. ~~Solo or team / timeline~~ Resolved in 0.5.0 and 0.7.0: solo, no fixed timeline.
5. ~~Test vehicle~~ Resolved in 0.8.0: 2008 Honda Ridgeline as primary. Second vehicle still to be chosen.
6. Music track list: sources agreed in 0.11.0 (mix of free licensed, original and commissioned); specific tracks still to be decided.
7. ~~Mascot car model~~ Resolved in 1.3.0: a 1995 Suzuki Sidekick, in teal (lime green until 1.5.0), with no names or badges.

---

## Changelog

| Version | Date | Changes |
|---|---|---|
| 1.5.0 | 2026-11-14 | Mascot car color changes from lime green to teal (`#2BAE9F`, from the owner's Sidekick photo); lime stays the main UI accent; goals, section 5b and open question 7 updated; `CLAUDE.md` updated to 1.5.0 |
| 1.4.0 | 2026-11-14 | Owner's reference image removed from section 5b (mood, reference points, model outlines, mascot car); the mascot car is based on the real 1995 Sidekick; palette values unchanged, the ring pattern color is now a darker tan for patterns and panel edges; `CLAUDE.md` updated to 1.4.0 |
| 1.3.0 | 2026-11-14 | Mascot car set to a 1995 Suzuki Sidekick (closes open question 7); music and sound effects move to a 3DS-game style (section 5f, goals); `CLAUDE.md` updated to 1.3.0 |
| 1.2.0 | 2026-11-14 | New visual direction in section 5b: look and feel like a Nintendo 3DS game, with the owner's reference image for palette and feel; chunky toy-like gauges; thin dark outlines on all models; lime-green mascot car (icon, banner, title screen, attract mode, garage card); English labels only (Japanese sub-labels dropped); IP rule covers ripped assets; goals, DIAG, fun modes, M3, risks and open questions updated to match; `CLAUDE.md` updated to 1.2.0 |
| 1.1.0 | 2026-11-13 | Review fixes: battery voltage via `ATRV`; manifold vacuum computed as baro − MAP with key-on fallback; M0 Wi-Fi checklist (manual IP, failed connection test, slots, sleep) and reconnect-on-wake; Japanese sub-label wording and font subsetting; version-free release filenames and a fixed `.cia` title ID; Time Attack arms while stopped and auto-starts, with interpolated speed samples; Eco Challenge MAP estimate uses engine displacement; VIN decode limited to manufacturer and year; master mute never silences the CEL chime, with a visual fallback when DSP firmware is missing; `CLAUDE.md` updated to 1.1.0 |
| 1.0.0 | 2026-11-13 | Brief finalized as the project baseline; starting `CLAUDE.md` (v1.0.0) drafted alongside it. Open items carried forward: second test vehicle, music track list |
| 0.13.0 | 2026-11-13 | Code to be written by Claude Code with Beads (`bd`) for task tracking; added section 5g (roles, Beads setup, epics per milestone, labels, CLAUDE.md rules, hardware-free testing); added two workflow risks |
| 0.12.0 | 2026-11-13 | Named the app "GaugeBoy: OBD-II Deluxe"; SD card paths changed to `sdmc:/3ds/gaugeboy/`; added name trademark risk and mitigation |
| 0.11.0 | 2026-11-13 | Music sourcing set to a mix of free licensed, original and commissioned tracks; track list added as open question 6 |
| 0.10.0 | 2026-11-13 | Added section 5e (fun modes: timers, shift light, G-meter, Rev Sync, Eco Challenge, garage card, achievements, attract mode, safety rule) and section 5f (music, sound effects, audio settings, licensing); added FUN menu button; added M5 Audio and M6 Fun modes, release moved to M7; new risks and success criteria |
| 0.9.0 | 2026-11-13 | Added section 5d: CEL codes (stored, pending, permanent, CEL status, readiness monitors, clear, history log) and fuel trims (both banks, short/long/total, center-zero gauges, color zones, hints); added TRIM menu and CEL lamp to bottom panel; updated goals, M1/M2 scope and success criteria |
| 0.8.0 | 2026-11-13 | Primary test vehicle set to 2008 Honda Ridgeline (expected CAN protocol, vacuum gauge in place of boost, supported-PID scan); second vehicle left to be chosen; updated success criteria and closed open question 5 |
| 0.7.0 | 2026-11-13 | Timeline set to none (milestones in order, no dates); closed open question 4 |
| 0.6.0 | 2026-11-13 | Visual style set to skeuomorphic (realistic gauges, LCDs, materials, pressable controls); stereoscopic 3D moved from stretch into v1 (layered gauges on top screen); navigation set to touch-only on bottom screen; updated goals, M3 scope, risks and success criteria |
| 0.5.0 | 2026-11-13 | Set team to solo; added section 5b (2000s JDM tuner visual direction, brand/IP rule, theme system) and section 5c (QR code install via FBI, stable and per-release links, TLS fallback); updated goals, M0/M3/M5 scope, risks and success criteria |
| 0.4.0 | 2026-11-13 | Project set to open source (MIT, provisional); added section 5a covering repo, versioning, CI builds, compatibility reports and DTC data sourcing |
| 0.3.0 | 2026-11-13 | ESP32 bridge moved out of v1 scope to a contingency; added M0 go / no-go rule; marked ESP32 hardware as contingency-only |
| 0.2.0 | 2026-11-13 | Target hardware set to New 3DS family only; added speedup mode and Old 3DS check to technical approach; updated test hardware |
| 0.1.0 | 2026-11-13 | Initial draft: goals, connectivity options, architecture, milestones, risks, open questions |
