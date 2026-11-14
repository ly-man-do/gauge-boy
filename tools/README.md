# Developer tools

## ELM327 emulator

A simulated Wi-Fi OBD-II adapter, so GaugeBoy can be tested without a car. It uses the
[ELM327-emulator](https://github.com/Ircama/ELM327-emulator) Python package (dev only, never
shipped), pinned in `requirements-dev.txt`.

### Start it

Needs Python 3. From the repo root:

```sh
tools/run-emulator.sh
```

The first run creates `tools/.venv` and installs the package. The emulator then listens on
`127.0.0.1:35000` with the `car` scenario: ISO 15765-4 CAN 11-bit 500 kbaud (protocol 6, as the
2008 Ridgeline is expected to use), CEL status, stored / pending codes, bank 1 fuel trims, MAP and a
VIN. It shows a `CMD>` prompt; type `help` for its commands. Press Ctrl-D to stop it.

Options:

| Setting | Default | Notes |
|---|---|---|
| `--lan` | off | Listen on all interfaces so a real 3DS on the same Wi-Fi can connect to this computer's IP. macOS may ask to allow incoming connections. |
| `ELM_PORT` | `35000` | e.g. `ELM_PORT=35001 tools/run-emulator.sh` |
| `ELM_SCENARIO` | `car` | `default` answers with two ECUs (CAN multi-response) but has no fuel trims or codes |

Other arguments are passed to the emulator (`tools/.venv/bin/elm -h` lists them).

### Check the driver against it

With the emulator running, in another terminal:

```sh
make emulator-check
```

This runs the ELM327 driver start-up sequence, `0100`, `ATDPN`, `ATRV`, `0902` and every M0 spike
button action over TCP, and prints PASS / FAIL for each. Use `EMU_HOST=... EMU_PORT=...` for another
address. It is not part of `make test` or CI because it needs the emulator running.

### Use it with the app

- **Azahar (3DS emulator on this computer):** set `host = 127.0.0.1` and `port = 35000` under
  `[adapter]` in Azahar's emulated SD card, `sdmc/3ds/gaugeboy/config.ini` (on macOS:
  `~/Library/Application Support/Azahar/sdmc/`). Azahar passes the app's sockets through to this
  computer. Set System Model to New 3DS.
- **Real New 3DS on your home Wi-Fi:** start with `tools/run-emulator.sh --lan` and set `host` in the
  3DS's `sdmc:/3ds/gaugeboy/config.ini` to this computer's IP address.

### Limits

The emulator is not a car or a real adapter: replies are instant or fixed-delay, there are no clone
quirks, and the `car` scenario has no bank 2 fuel trims (the Ridgeline V6 has two banks). Real
adapter and vehicle behavior is checked on hardware (see the M0 hardware test issues).
