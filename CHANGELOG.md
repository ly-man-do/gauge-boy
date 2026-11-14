# Changelog

All notable changes to GaugeBoy are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [Semantic Versioning](https://semver.org/). Milestone releases: M0 = 0.0.x, M1 = 0.1.x, ... M7 = 1.0.0.

## [Unreleased]

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
