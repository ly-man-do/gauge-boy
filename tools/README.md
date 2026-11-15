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

## Fake 3DS connection test

The 3DS joins a Wi-Fi OBD-II adapter's network (e.g. V-LINK) but fails its internet connection
test (error `003-2002`), and then won't let apps use the network. `fake_conntest.py` answers that
test from a computer on the same network, so the 3DS stays on it. It is the proof for a small
helper device on the adapter's network (Beads `gauge-boy-7v5.22`). Python 3 standard library only.

It runs:

- a DNS server on UDP 53: the test names (`conntest.nintendowifi.net`, `ctest.cdn.nintendo.net`,
  `nncs1/nncs2.app.nintendowifi.net`) resolve to this computer, every other name gets "not found"
  (there is no internet to forward to; `--answer-all` answers every name with this computer);
- a web server on TCP 80: any page gets `200 OK` with `X-Organization: Nintendo`.

Every DNS lookup and page request is logged, so you can see the 3DS use it.

### Before you start (Mac)

- **Quit VPN apps** (e.g. Mullvad) and Docker Desktop for the test. They can hold DNS port 53 and
  block other devices from reaching this computer. The script says so if port 53 is taken.
- If macOS asks whether Python may accept incoming connections, choose **Allow**.

### Test 1: at home (proves the fake answer works)

1. Mac on your home Wi-Fi. From the repo root: `python3 tools/fake_conntest.py`. It prints the IP
   address to use.
2. 3DS: **System Settings** > **Internet Settings** > **Connection Settings** > your home slot >
   **Change Settings** > **DNS**: Auto-Obtain **No**, Primary DNS = that IP. Save.
3. Run **Test Connection** on that slot.
4. **Pass:** the test succeeds and the Mac log shows `DNS <3DS IP>: conntest.nintendowifi.net A`
   and `HTTP <3DS IP>: GET ... 200 test page`. Other names get "not found", so the 3DS can only
   have passed through this script.
5. Set the home slot's DNS back to Auto-Obtain **Yes** afterwards.

### Test 2: in the truck with the V-LINK (the real target)

Needs a Mac laptop in the truck.

1. Adapter plugged in, ignition on. Disconnect your phone from `V-LINK` (adapters may allow only a
   few clients). Join the Mac to `V-LINK`.
2. Run `python3 tools/fake_conntest.py` and note the IP it prints (a `192.168.0.x` address).
3. 3DS V-LINK slot > **Change Settings**: IP Address Auto-Obtain **Yes**; DNS Auto-Obtain **No**,
   Primary DNS = the Mac's IP. Save, then **Test Connection**: it should now pass.
4. Open GaugeBoy (adapter `192.168.0.10:35000`) and tap **CONNECT**, then **ATZ**.

If step 4 connects, the 3DS can use the V-LINK directly once something on its network answers the
test. The long-term helper would be a tiny device (e.g. an ESP32) joined to `V-LINK` at a fixed IP
doing the same job, pending the owner's go / no-go decision (`gauge-boy-7v5.13`).

### Options

| Option | Default | Notes |
|---|---|---|
| `--dns-port` / `--http-port` | `53` / `80` | The 3DS always uses 53 and 80; other ports are for local checks with `dig -p` and `curl`. |
| `--ip` | auto | Address handed out for the test names (default: this computer's address on the asking device's network). |
| `--answer-all` | off | Answer every name, not just the test names. |
| `--bind` | `0.0.0.0` | macOS only lets ordinary users use ports below 1024 on all addresses; a specific address needs `sudo`. |

## .cia packaging

- `get-cia-tools.sh` downloads pinned makerom (v0.18.4) and bannertool (v1.2.2) into
  `tools/bin/` (gitignored) and checks their SHA-256 hashes. `make cia` runs it on first use.
  It needs Linux x86_64, so run `make cia` in the devkitPro Docker image.
- `check_cia.py` reads the title ID, product code and version back out of a built `.cia`.
  `make cia` runs it after every build, so a wrong title ID fails the build.
- `make_meta_assets.py` regenerates the original HOME menu icon, banner and banner chime in
  `meta/` (Python 3 standard library only):

  ```bash
  python3 tools/make_meta_assets.py
  ```

## Releases

- `release_notes.py <version>` prints the `CHANGELOG.md` section for that version. The
  release workflow uses it as the release text and stops if the section is missing.
