# CLAUDE.md: GaugeBoy: OBD-II Deluxe

**Version:** 1.15.0
**Matches project brief:** 1.11.0 (`docs/gaugeboy-brief.md`)

You are the developer on GaugeBoy, a homebrew New 3DS app that reads a car's OBD-II data over a Wi-Fi ELM327 adapter. The project brief is the source of truth for scope. If a request conflicts with the brief, ask the owner before acting.

The GaugeBoy rules in this file take precedence over the generic Beads block at the end of it and over any user-level `CLAUDE.md`.

---

## 1. Task tracking: Beads only

Use Beads (`bd`) for all task tracking. Do not create markdown TODO lists, plan files or checklists in the repo. A SessionStart hook runs `bd prime`; run it yourself if its output is missing.

**Every session:**
1. `bd ready` to see unblocked work.
2. Claim one issue: `bd update <id> --claim`.
3. Do the work for that issue only.
4. File anything else you find (bugs, follow-ups, ideas) with `bd create "<title>" --deps discovered-from:<current-id>`. Do **not** use `bd dep add` for this: its default type is `blocks`, which would block the current issue or hide the new one from `bd ready`. Use `bd dep add` only for real blocking order (e.g. an M1 issue blocked by the M0 go / no-go issue).
5. Close the issue with a short note on what changed, only after the build and tests pass.
6. Commit code and `.beads/` changes together (see Git policy below).

**Git policy (opt-in to the Beads team-maintainer profile, commits only):**
- After closing an issue, commit the code and the tracked `.beads/` files (including the auto-exported `.beads/issues.jsonl`) in one commit.
- Never `git push`, `bd dolt push` or run any remote sync unless the owner asks in the current session.
- Never commit on `main` directly if the owner has asked for branches or PRs.

**Structure:**
- One epic per milestone: M0 Spike, M1 Core read, M2 Live data, M3 UI polish, M4 Logging, M5 Audio, M6 Fun modes, M7 Release.
- ESP32 bridge is a deferred epic. Do not work on it unless the owner opens it after the M0 go / no-go decision.
- The M0 go / no-go issue is labeled `needs-owner`. Every M1 issue is blocked by it (`bd dep add <m1-id> --blocked-by <go-no-go-id>`).
- Only the owner closes milestone epics, because epics contain `needs-hardware` work and closing one triggers a release.

**Labels:** `transport`, `elm327`, `obd`, `ui`, `3d`, `audio`, `fun-mode`, `ci`, `docs`, `needs-hardware`, `needs-owner`.

**Hard rule:** Never close an issue labeled `needs-hardware` or `needs-owner`. These need a real New 3DS, a test car (2008 Honda Ridgeline, or the second test car, a 1996 Geo Tracker) or an owner decision. When your part is done, add a comment with exact test steps for the owner and leave it open.

---

## 2. Tech stack

- C (C11), devkitPro / devkitARM, libctru, citro2d / citro3d.
- Networking: libctru `soc` (BSD sockets), TCP to the adapter (default `192.168.0.10:35000`, configurable).
- Audio: libctru `ndsp`; Ogg Vorbis music streamed, WAV sound effects.
- Target: New 3DS family only. Call `osSetSpeedupEnable(true)` at startup. Show an "unsupported device" screen and exit on Old 3DS.
- Outputs: `.3dsx` and `.cia`. Release asset filenames carry no version number (`gaugeboy.3dsx`, `gaugeboy.cia`) so the "latest release" QR link never breaks.
- SD card data folder: `sdmc:/3ds/gaugeboy/`.

**Approved tools and dependencies (owner-approved in 1.1.0, 1.7.0 and 1.8.0):**
- Host unit tests: a minimal hand-rolled assert harness in `tests/` (no third-party test framework).
- ELM327 emulator: the Python `ELM327-emulator` package, dev-only (never shipped); its config lives in `tools/`.
- `.cia` packaging: `makerom` and `bannertool`. The `.cia` title ID is chosen once (an M0 `needs-owner` issue) and never changed.
- 3D modeling: Blender, dev-only (never shipped). Models are built by Blender Python scripts in `tools/` so they can be regenerated, and a `tools/` converter turns the export into C data for citro3d. Scripts are MIT (GPL-compatible, as Blender's API needs); Blender's license does not cover the models it makes.
- QR codes: the Python `segno` package (BSD), dev and CI only (never shipped), pinned with a hash in `tools/requirements-release.txt`. `tools/make_qr.py` makes the README's stable install QR and each release's QR.
- Anything else still needs owner approval (section 9).

## 3. Code layout

```
source/
  transport/   TCP socket layer (swappable; ESP32 bridge could plug in here later)
  elm327/      AT commands, response parsing, timeouts (no 3DS headers)
  obd/         PID requests, decoding, DTC parsing, fuel trims (no 3DS headers)
  data/        Live values, history buffer, CSV logger
  ui/          Top screen gauges (3D), bottom screen touch panel
  audio/       Music streaming, sound effects, volume
  fun/         Timers, shift light, G-meter, Rev Sync, Eco, achievements
  main.c
romfs/         Textures, fonts, sounds, music
tests/         Host unit tests for elm327/, obd/ and the platform-free parts of data/ and fun/
tools/         ELM327 emulator config, QR generation scripts
docs/          Project brief, compatibility list
```

**Key rules:**
- `elm327/` and `obd/` must stay platform-free (no libctru includes) so they compile and run on a normal computer for testing.
- Keep the logic in `data/` (history buffer, CSV formatting) and `fun/` (timer state machines, scoring, achievement rules) platform-free too. Put 3DS-specific I/O (files, accelerometer, camera, clock) behind thin wrappers so the logic can be unit-tested on the host.

## 4. Build and test

- `make` builds the `.3dsx`; `make cia` builds the `.cia`.
- `make test` builds the host unit tests with the host C compiler (not devkitARM) and runs them.
- Before closing any issue, the full devkitPro build and `make test` must both pass.
- For transport and driver work, test against the ELM327 emulator on the computer over TCP.
- Add or update unit tests for every change in `elm327/` or `obd/`, including bad and partial adapter responses (clone adapters misbehave).
- Threading: OBD polling and audio each run on their own thread. Never block the render loop on network or file I/O.
- Handle Wi-Fi loss (e.g. lid closed / sleep) by reconnecting the transport without restarting the app.

## 5. UI rules

- Style: look and feel like a Nintendo 3DS game, with the palette in brief section 5b (warm orange backdrop, lime-green accent). Do not use the old reference image.
- Models: chunky, slightly toy-like low-poly models with colorful textures and vertex colors, and thin dark outlines on every model. Gauges are chunky, toy-like dials, not realistic tuner gauges.
- Mascot car: a teal 1995 Suzuki Sidekick, with no maker names, badges or logos.
- Music and sound effects: 3DS-game style (bright, catchy, playful); see brief section 5f.
- Top screen: display only, stereoscopic 3D with layered gauges (housing, face, needle, glass). Depth follows the 3D slider; respect the max-depth setting.
- Bottom screen: all navigation by touch. Buttons at least 40x40 px. Menus: LIVE / DIAG / TRIM / FUN / LOG / SETUP.
- Must hold 60 fps on the top screen with the 3D slider at maximum. Pre-render static layers; redraw only needles and digits each frame.
- Physical buttons are optional shortcuts only; every function must be reachable by touch.
- Labels are English only; no Japanese sub-labels or Japanese font.

## 6. Safety rules (do not remove or weaken)

- Startup safety notice: not for use by the driver while driving.
- Clearing codes always needs a confirmation dialog explaining it turns off the CEL and resets readiness monitors.
- Fun-mode timers show a closed-course warning. No online leaderboards.
- Time Attack timers are armed while stopped and start automatically when the car moves; starting, stopping and dismissing results never needs a touch while moving.
- Rev Sync is locked unless vehicle speed is 0.
- No feature may require touching the 3DS while the vehicle is moving.
- CEL warning sound always plays, even with music muted or master mute on. If DSP firmware is missing, the CEL warning is shown visually instead (flashing CEL lamp and a top-screen pop-up).

## 7. Legal and asset rules

- License: MIT. Every dependency and asset must be compatible.
- "GaugeBoy" lettering and logo must be original and must not imitate the Game Boy logo, fonts or colors.
- The 3DS-game look is built from original art only.
- Fonts: open-licensed (e.g. SIL OFL) only.
- Music and sounds: original, commissioned or CC0 / CC-BY. Record the source and license of every asset in `CREDITS.md`.
- DTC descriptions: generic codes, plus manufacturer-specific codes per make (shown only when the car's make is known), both from Wal33D/dtc-database (MIT) by owner decision (2026-11-15, gauge-boy-bxf.7, bxf.12), pinned and generated by `tools/make_dtc_table.py`, credited in `CREDITS.md`. Don't swap or add sources without the owner.
- VIN decoding: generic data only (manufacturer from the WMI, model year from VIN position 10). No model-name database unless the owner approves a public-domain source.
- README must state the project is unofficial and not affiliated with Nintendo.

## 8. Versioning and changelog

- Semantic versioning. Milestone releases: M0 = 0.0.x, M1 = 0.1.x ... M7 = 1.0.0.
- Update `CHANGELOG.md` with every closed issue that changes behavior.
- When the owner closes a milestone epic, tag the release; CI builds `.3dsx`, `.cia` and the per-release QR code.
- If you change this file, bump its version and add a changelog entry below.
- When the brief changes, update the "Matches project brief" version at the top of this file.

## 9. When to stop and ask the owner

- Anything that changes scope, the UI style, the safety rules or the license.
- Adding a new dependency or tool not listed in section 2.
- Any decision labeled `needs-owner`.
- If an M0 result suggests switching to the ESP32 bridge.

---

## Changelog

| Version | Date | Changes |
|---|---|---|
| 1.15.0 | 2026-11-15 | Matches brief 1.11.0: the METER menu is renamed LIVE (section 5) |
| 1.14.0 | 2026-11-15 | Matches brief 1.10.0: removed "No Nintendo or third-party logos, trademarks, fonts, game assets, music or sounds" from section 7 (owner decision) |
| 1.13.0 | 2026-11-15 | Owner decision: removed the ripped-asset sentence (studied for size and structure, never shipped, traced or converted) from section 7 |
| 1.12.0 | 2026-11-15 | Matches brief 1.9.0: second test car, 1996 Geo Tracker (section 1) |
| 1.11.0 | 2026-11-15 | Matches brief 1.8.0: manufacturer-specific DTC descriptions per make from the same source (section 7) |
| 1.10.0 | 2026-11-15 | DTC descriptions come from Wal33D/dtc-database (MIT, generic rows only) by owner decision (section 7) |
| 1.9.0 | 2026-11-15 | Matches brief 1.7.0: all OBD-II protocols the ELM327 handles (Ridgeline is ISO 9141-2, not CAN) |
| 1.8.0 | 2026-11-15 | `segno` approved for making the FBI install QR codes (section 2), dev and CI only |
| 1.7.0 | 2026-11-14 | Blender approved as a dev-only 3D modeling tool (section 2): scripted models in `tools/`, converted to C data for citro3d |
| 1.6.0 | 2026-11-14 | Matches brief 1.6.0: the mascot car's fender flares and bumpers are body-colored |
| 1.5.0 | 2026-11-14 | Matches brief 1.5.0: the mascot car is teal; lime stays the UI accent |
| 1.4.0 | 2026-11-14 | Matches brief 1.4.0: the owner's reference image is dropped; the style comes from the brief's palette and the real 1995 Sidekick |
| 1.3.0 | 2026-11-14 | Matches brief 1.3.0: mascot car is a 1995 Suzuki Sidekick; 3DS-game style music and sound effects |
| 1.2.0 | 2026-11-14 | Matches brief 1.2.0: 3DS-game visual direction (reference palette, toy-like gauges, outlines on all models, lime-green mascot car), English-only labels, ripped assets are references only |
| 1.1.0 | 2026-11-13 | Matches brief 1.1.0. Follow-ups filed with `--deps discovered-from` (not `bd dep add`); explicit git policy (commit, never push); owner closes epics; go / no-go issue labeled `needs-owner`; approved test harness, ELM327 emulator and `.cia` tools; version-free release filenames; platform-free logic in `data/` and `fun/`; Wi-Fi reconnect; Time Attack auto-start; CEL warning under master mute and without DSP firmware; VIN decode limits; brief/CLAUDE.md version sync rule |
| 1.0.0 | 2026-11-13 | Initial version, matching project brief 1.0.0 |

---

<!-- BEGIN BEADS INTEGRATION v:1 profile:minimal hash:6cd5cc61 -->
## Beads Issue Tracker

This project uses **bd (beads)** for issue tracking. Run `bd prime` to see full workflow context and commands.

### Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --claim  # Claim work
bd close <id>         # Complete work
```

### Rules

- Use `bd` for ALL task tracking — do NOT use TodoWrite, TaskCreate, or markdown TODO lists
- Run `bd prime` for detailed command reference and session close protocol
- Use `bd remember` for persistent knowledge — do NOT use MEMORY.md files

**Architecture in one line:** issues live in a local Dolt DB; sync uses `refs/dolt/data` on your git remote; `.beads/issues.jsonl` is a passive export. See https://github.com/gastownhall/beads/blob/main/docs/SYNC_CONCEPTS.md for details and anti-patterns.

## Agent Context Profiles

The managed Beads block is task-tracking guidance, not permission to override repository, user, or orchestrator instructions.

- **Conservative (default)**: Use `bd` for task tracking. Do not run git commits, git pushes, or Dolt remote sync unless explicitly asked. At handoff, report changed files, validation, and suggested next commands.
- **Minimal**: Keep tool instruction files as pointers to `bd prime`; use the same conservative git policy unless active instructions say otherwise.
- **Team-maintainer**: Only when the repository explicitly opts in, agents may close beads, run quality gates, commit, and push as part of session close. A current "do not commit" or "do not push" instruction still wins.

## Session Completion

This protocol applies when ending a Beads implementation workflow. It is subordinate to explicit user, repository, and orchestrator instructions.

1. **File issues for remaining work** - Create beads for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **Handle git/sync by active profile**:
   ```bash
   # Conservative/minimal/default: report status and proposed commands; wait for approval.
   git status

   # Team-maintainer opt-in only, unless current instructions forbid it:
   git pull --rebase
   git push
   git status
   ```
5. **Hand off** - Summarize changes, validation, issue status, and any blocked sync/commit/push step

**Critical rules:**
- Explicit user or orchestrator instructions override this Beads block.
- Do not commit or push without clear authority from the active profile or the current user request.
- If a required sync or push is blocked, stop and report the exact command and error.
<!-- END BEADS INTEGRATION -->
