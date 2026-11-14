# Changelog

All notable changes to GaugeBoy are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [Semantic Versioning](https://semver.org/). Milestone releases: M0 = 0.0.x, M1 = 0.1.x, ... M7 = 1.0.0.

## [Unreleased]

### Added

- Project brief (`docs/gaugeboy-brief.md`) and agent instructions (`CLAUDE.md`).
- Beads issue tracking with milestone epics M0 to M7 and the M0 issue plan.
- README with the unofficial / not-affiliated statement and safety notice, MIT license, changelog and contributing guide.
- Project skeleton: `make` builds `gaugeboy.3dsx`, which enables New 3DS speedup mode, shows the startup safety notice, and shows an "unsupported device" screen and exits on Old 3DS models.
- Host unit tests: `make test` builds and runs `tests/` with the host C compiler using a small built-in assert harness.
