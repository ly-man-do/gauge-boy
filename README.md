# GaugeBoy: OBD-II Deluxe

A homebrew app for the New Nintendo 3DS family that connects to a car's OBD-II port through a Wi-Fi ELM327 adapter. It reads check engine light codes, readiness monitors, fuel trims and live engine data, and shows them on the 3DS's two screens in a 2000s Japanese tuner style.

> **Unofficial project.** GaugeBoy is an independent, fan-made homebrew project. It is not affiliated with, endorsed by or sponsored by Nintendo. Nintendo 3DS and New Nintendo 3DS are trademarks of Nintendo.

> **Safety.** GaugeBoy is not for use by the driver while driving. Use it parked, or let a passenger operate it. Performance timers are for closed courses and track days only.

## Status

**Early development: M0 spike.** There is nothing to install yet. The first milestone tests whether a New 3DS can join a Wi-Fi ELM327 adapter's network and talk to it reliably. If no tested adapter works, the project switches to a small ESP32 Wi-Fi bridge before going further.

| Milestone | Version | Scope |
|---|---|---|
| M0 Spike | 0.0.x | Connect to the adapter, send `ATZ`, show the reply; test QR install |
| M1 Core read | 0.1.x | Check engine codes (stored, pending, permanent), readiness monitors, clear codes, VIN |
| M2 Live data | 0.2.x | Live sensor data and fuel trims for both banks |
| M3 UI polish | 0.3.x | Skeuomorphic gauges, stereoscopic 3D top screen, touch control panel |
| M4 Logging | 0.4.x | CSV session logs on the SD card |
| M5 Audio | 0.5.x | Original music and sound effects |
| M6 Fun modes | 0.6.x | Timers, shift light, G-meter, achievements and more |
| M7 Release | 1.0.0 | `.cia` release, QR code install |

## Planned features

- **Diagnostics:** check engine light status, stored / pending / permanent codes with generic SAE J2012 descriptions, readiness monitors, and clearing codes behind a confirmation dialog.
- **Fuel trims:** short-term, long-term and total trims for both banks on center-zero gauges.
- **Live data:** RPM, speed, coolant temp, throttle, engine load, intake air temp, manifold vacuum and battery voltage.
- **Logging:** CSV logs and a code history saved to the SD card.
- **Style:** realistic tuner-style gauges and LCD readouts, with layered stereoscopic 3D on the top screen and touch-only controls on the bottom screen.

## Requirements

- New Nintendo 3DS, New 3DS XL or New 2DS XL with custom firmware (Luma3DS). Older 3DS models are not supported.
- A Wi-Fi ELM327 OBD-II adapter. Bluetooth adapters cannot work because the 3DS has no Bluetooth radio.
- A car with an OBD-II port (US cars from 1996 on).

## Install

Not available yet. Releases will include a QR code you can scan with FBI (*Remote Install* > *Scan QR Code*), plus `.cia` and `.3dsx` files to copy to the SD card manually.

## Building

Build instructions will be added with the project skeleton. The app is written in C with devkitPro / devkitARM and libctru: `make` builds the `.3dsx`, `make cia` builds the `.cia`, and `make test` runs host unit tests.

To test without a car, see [tools/README.md](tools/README.md) for the ELM327 emulator.

## Project docs

- [Project brief](docs/gaugeboy-brief.md): scope, design and milestones.
- [Contributing](CONTRIBUTING.md)
- [Changelog](CHANGELOG.md)

Development uses [Beads](https://github.com/gastownhall/beads) for issue tracking. The `.beads/` folder is the issue tracker used by the project's AI coding agent; you can ignore it if you don't use Beads.

## License

[MIT](LICENSE). All artwork, music and sounds are original or openly licensed; sources will be listed in `CREDITS.md`.
