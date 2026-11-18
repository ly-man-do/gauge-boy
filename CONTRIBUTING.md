# Contributing to Gauge Boy

Thanks for your interest. Gauge Boy is in early development (M0 spike), so the most useful help right now is testing adapters and cars.

## Compatibility reports

If you try Gauge Boy with a Wi-Fi ELM327 adapter or a vehicle, please file a
[compatibility report](https://github.com/ly-man-do/gauge-boy/issues/new?template=compatibility-report.yml).
The form asks for:

- Adapter brand, model and reported ELM327 version (`ATI`).
- Adapter network type and security (open, WEP, WPA2; access point or ad-hoc) and whether you had to set a manual IP on the 3DS.
- 3DS model and firmware version.
- Vehicle make, model, year and engine, and the protocol shown by `ATDPN`.
- What worked, what didn't, and any error messages.

Paste the relevant part of `sdmc:/3ds/gauge-boy/spike-log.txt` if you can. Results feed the compatibility list in `docs/`.

## Before you change code

- Read the [project brief](docs/gaugeboy-brief.md). It is the source of truth for scope; changes outside it should start as an issue.
- Code is C11 built with devkitPro / devkitARM and libctru.
- `source/elm327/` and `source/obd/` must stay free of 3DS headers so they build and run as host unit tests (`make test`). Add tests for any change there, including bad and partial adapter replies.
- The full devkitPro build and `make test` must pass.

## Rules that are not negotiable

- **Safety:** no feature may require touching the 3DS while the vehicle is moving. Clearing codes always needs a confirmation dialog. Performance timers keep their closed-course warning, and there are no online leaderboards.
- **No third-party IP:** no Nintendo or other logos, trademarks, fonts, game assets, music or sounds. Fonts must be open-licensed (e.g. SIL OFL). Music and sounds must be original, CC0 or CC-BY, with the source and license recorded in `CREDITS.md`.
- **No proprietary data:** DTC descriptions are limited to the generic SAE J2012 codes; no copied manufacturer databases.

## Releases (maintainer)

1. Set the version in `source/version.h` (M0 = 0.0.x, M1 = 0.1.x ... M7 = 1.0.0).
2. In `CHANGELOG.md`, move the Unreleased entries under `## [x.y.z] - YYYY-MM-DD`.
3. Commit, push, then tag and push the tag: `git tag vx.y.z && git push origin vx.y.z`.

The Release workflow checks that the tag, `source/version.h` and the changelog agree, runs the tests, builds `gauge-boy.3dsx` and `gauge-boy.cia`, and publishes them on a GitHub release. The file names never change, so `https://github.com/ly-man-do/gauge-boy/releases/latest/download/gauge-boy.cia` always gets the newest version.

## Issue tracking

Development work is tracked with [Beads](https://github.com/gastownhall/beads) in `.beads/`. You don't need Beads to contribute; GitHub issues and pull requests are fine.

## License

Gauge Boy has no license for now (see the README); the owner may choose one later. Please open an issue to talk about a change before sending code.
