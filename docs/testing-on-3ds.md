# Testing Gauge Boy on a real New 3DS

How to get the current build onto your New 3DS and run the M0 spike tests with a Wi-Fi ELM327 adapter.

> **Safety:** do every step here parked, with the engine off or idling, or as a passenger. Never touch the 3DS while driving.

## What you need

- A **New** Nintendo 3DS, New 3DS XL or New 2DS XL with **Luma3DS** and the **Homebrew Launcher**. If you don't have these yet, follow [3ds.hacks.guide](https://3ds.hacks.guide/). Older (non-New) 3DS models show "unsupported device" and exit.
- A **Wi-Fi** ELM327 adapter (Bluetooth adapters can't work: the 3DS has no Bluetooth radio).
- One way to put files on the SD card: a card reader, FTP, or `3dslink` (see step 2).

## 1. Get `gauge-boy.3dsx`

Pick one.

**From GitHub (no build needed).** Every push builds the app.

```bash
rm -f ~/Downloads/gauge-boy-ci/gauge-boy.3dsx && gh run download -R ly-man-do/gauge-boy -n gauge-boy-3dsx -D ~/Downloads/gauge-boy-ci
```

That downloads the newest build's `gauge-boy.3dsx` into `~/Downloads/gauge-boy-ci/`. The `rm` is there because `gh` refuses to overwrite an existing file, so a second download (or one into the repo folder after a local build) would otherwise fail. In a browser: repo > **Actions** > latest **Build** run > **Artifacts** > `gauge-boy-3dsx` (it arrives as a zip; unzip it).

**Build it yourself** (from the repo folder, with Docker running):

```bash
docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src devkitpro/devkitarm make
```

## 2. Put it on the 3DS

The app goes in `sdmc:/3ds/gauge-boy/gauge-boy.3dsx`. The Homebrew Launcher finds apps in `/3ds/` and its subfolders, and Gauge Boy keeps its config and log in that same folder.

### Option A: SD card reader

1. Power off the 3DS and take out the SD card.
2. On the card, create `3ds/gauge-boy/` if it doesn't exist and copy `gauge-boy.3dsx` into it.
3. macOS leaves hidden `._` files on the card that can show up as broken entries. Remove them, then eject:

   ```bash
   dot_clean /Volumes/<card name>
   ```

4. Put the card back and power on.

### Option B: FTP over your home Wi-Fi (no card swapping)

1. Install **ftpd** on the 3DS (from Universal-Updater) and run it. It shows the 3DS's IP and port (usually `5000`).
2. On the Mac:

   ```bash
   curl -T gauge-boy.3dsx --ftp-create-dirs ftp://<3ds-ip>:5000/3ds/gauge-boy/
   ```

3. Quit ftpd (START).

### Option C: `3dslink` (fastest for repeated tests)

This sends the app over your home Wi-Fi and runs it at once, without touching the SD card. Good for quick UI checks; use A or B for the car test so the app is on the card.

1. Open the Homebrew Launcher and press **Y**. It waits for a connection and shows its IP.
2. On the Mac, from the repo folder:

   ```bash
   docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src devkitpro/devkitarm /opt/devkitpro/tools/bin/3dslink -a <3ds-ip> gauge-boy.3dsx
   ```

### Option D: install the `.cia` (HOME menu icon)

The `.cia` puts Gauge Boy on the HOME menu like a normal app, so you don't need the Homebrew Launcher to start it. It runs the same code as the `.3dsx`. You need **FBI** on the 3DS (from Universal-Updater).

1. Get `gauge-boy.cia` from CI:

   ```bash
   rm -f ~/Downloads/gauge-boy-ci/gauge-boy.cia && gh run download -R ly-man-do/gauge-boy -n gauge-boy-cia -D ~/Downloads/gauge-boy-ci
   ```

   or build it with:

   ```bash
   docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src devkitpro/devkitarm make cia
   ```

2. Copy it anywhere on the SD card (for example `/cias/gauge-boy.cia`) with a card reader or FTP.
3. In FBI: **SD** > find `gauge-boy.cia` > **Install and delete CIA** (or **Install CIA** to keep the file).
4. Gauge Boy appears on the HOME menu with a gauge icon. Installing a newer `.cia` later updates it in place, because the title ID never changes.

To remove it: FBI > **Titles** > Gauge Boy > **Delete Title**. Your `sdmc:/3ds/gauge-boy/` folder (config and log) stays.

## 3. Save the adapter's Wi-Fi on the 3DS

Do this once per adapter, in the car with the adapter plugged in and the ignition on (most adapters only power up then). Tested with the Vgate iCar Pro Wi-Fi adapter (ELM327 v2.3), whose network is called `V-LINK`, on 2026-11-15.

1. **System Settings** > **Internet Settings** > **Connection Settings**. Pick an empty slot (there are only 3; note which one you use).
2. **New Connection** > **Manual Setup** > **Search for an Access Point**, and pick the adapter's network (`V-LINK` for the Vgate iCar Pro).
   - If the adapter's network doesn't appear, it is probably ad-hoc or 5 GHz, which the 3DS can't join. Note that in your report.
3. Enter the password if it has one, then **OK** to save. The **connection test will fail** (error `003-2002`) because the adapter has no internet. That's expected; make sure the slot is still saved.

4. Open the slot > **Change Settings** > **IP Address**:
   - Auto-Obtain IP Address: **No**
   - IP Address: `192.168.000.011`
   - Subnet Mask: `255.255.255.000`
   - Gateway: `192.168.000.010` (the adapter)
5. **DNS**:
   - Auto-Obtain DNS: **No**
   - Primary DNS: `192.168.000.011` (the 3DS's own address, so the connection test asks Gauge Boy)
   - Secondary DNS: `000.000.000.000`
6. Save. The connection test will still fail: Gauge Boy isn't running yet.

**Check:** open Gauge Boy, go to **SETUP**, tap **Wi-Fi slot** until it shows the adapter's slot and tap **Check the slot's IP and DNS**. It reads the slot (read-only) and says it's set up, or shows the current (NOW) and needed (NEW) values to fix in System Settings.

This assumes the adapter is at `192.168.0.10`, as the Vgate iCar Pro and most Wi-Fi ELM327 adapters are. If yours differs, use its address as the Gateway, pick a free address in the same subnet for the 3DS, and use that same address as Primary DNS.

Why: the 3DS drops a network that fails its internet connection test and won't let apps use it. Gauge Boy answers that test itself (a tiny DNS server and test page on the 3DS), so no other device is needed. When you tap **CONNECT**, Gauge Boy asks the 3DS to join the adapter's network and answers the test while it does. The log shows `Conntest:` lines when the 3DS asks Gauge Boy for the test page, and the `Network:` line shows the network, slot and the 3DS's IP. If this ever fails, `tools/fake_conntest.py` on a laptop on the adapter's network does the same job (see `tools/README.md`).

The 3DS uses whichever saved connection is in range, so your home Wi-Fi slot can stay. If another saved network (for example your home Wi-Fi in the driveway) takes over, tap **SLOT** in Gauge Boy until it shows the adapter's slot (`SLOT 2` if you saved it in slot 2). The start-up log lists each slot's network name. CONNECT then joins only that slot and, if the 3DS is on another network, asks it to switch. The choice is saved as `wifi_slot` in `config.ini`.

## 4. Launch Gauge Boy

If you installed the `.cia`, start Gauge Boy from the HOME menu. Otherwise, open the Homebrew Launcher the way you usually do. With a standard Luma3DS setup: open **Download Play**, press **L + Down + Select** for the Rosalina menu, choose **Miscellaneous options** > **Switch the hb. title to the current app**, back out with B, close Download Play with HOME, and open Download Play again.

Pick **Gauge Boy**. You should see the safety notice; tap the bottom screen to continue.

On first launch the app creates `sdmc:/3ds/gauge-boy/config.ini`:

```ini
[adapter]
host = 192.168.0.10
port = 35000
wifi_slot = any

[vehicle]
make = auto
```

If your adapter uses a different address, edit the file (card reader or FTP) and relaunch. The bottom screen header shows the address in use. `make`, `model` and `year` are set from **SETUP > VEHICLE**, `units` from **SETUP > OTHER**.

## 5. Smoke test (at home, no adapter)

1. Gauge Boy opens on **HOME**: two status cards on top (the check engine light, amber when on, which opens DIAG; and the adapter, Connected or Not connected, which opens the Wi-Fi guide), six big cards, LIVE, DIAG, TRIM / TRACK, LOG, SETUP (LIVE, TRIM, TRACK and LOG say in the bottom bar which version brings them), and a **START Close** bar (tap it or press START to close Gauge Boy). The card you tapped last has a green outline. **DIAG** has tabs STORED / PENDING / PERM / READY across the top, a list of up to three rows per page, then **BACK**, **<** page **>**, **CLEAR** and **SCAN** along the bottom. **SETUP** is a menu of four pages, each with **BACK** to the menu: **TESTS** (ATZ, ATSP0+0100, ATDPN / ATRV, 0902 VIN, SPEED TEST / LIVE TEST, SAVE LOG / BACK, OLDER, NEWER), **WI-FI** (connect to the adapter, Wi-Fi slot, check the slot's IP and DNS, Wi-Fi setup guide), **VEHICLE** (make, model, year, VIN) and **OTHER** (units US or metric, About Gauge Boy). The Wi-Fi setup guide is the setup steps below with this adapter's values, and a CHECK button that reports the slot, its IP / DNS, Wi-Fi and the adapter. The top screen shows the session log on every SETUP page (L / R scroll it), except About. Shortcuts: B = BACK, X = SCAN, L / R = page. A tap flashes the button yellow; while an action runs it turns red and the top header shows `BUSY`.
2. Tap **ATZ**. About 3 seconds later the log shows "Adapter did not answer" (or "refused or unreachable"). The buttons keep responding meanwhile.
3. OLDER / NEWER scroll the log. SAVE LOG writes this session's log to its own file (see step 7).
4. Exit with **START**. On the SD card, `sdmc:/3ds/gauge-boy/spike-log.txt` has the same lines as the screen.

Report anything garbled (colors, layout, text running off the screen).

## 6. Car test (parked)

1. Plug in the adapter, ignition on (engine running is fine).
2. Make sure the 3DS's wireless switch is on and it has joined the adapter's slot.
3. Launch Gauge Boy (engine running). Once it connects to the adapter it finds the car by itself: the HOME top screen shows the car (e.g. "2023 Subaru Ascent"), its VIN and protocol within a few seconds, and the log says `= Car: ...`. Then tap **DIAG**, then **SCAN**. It connects, finds the protocol (about 5 s on ISO 9141-2) and reads the check engine light, readiness, stored / pending / permanent codes and the VIN. The top screen shows the facts (Car, VIN, Protocol, CEL, Readiness, Codes); the bottom screen lists the chosen tab's codes or monitors. Tap a row to see its full description on the top screen (with stored codes, the scan also reads the freeze frame: the facts show "Freeze  P0xxx", and tapping that code adds "Freeze frame when it set: ..." with the values the engine computer kept); **<** / **>** page through longer lists. PERM disappears if the car doesn't support permanent codes. Cars that don't report a VIN over OBD (for example a 1996 Geo Tracker) show "VIN not read"; choose the make in **SETUP > VEHICLE > Make** (Auto, then 32 makes over three pages; L / R or **<** / **>** to page), and type the model (3DS keyboard) and step the year with **<** **>** on the same page. The Car line then shows them with "(set in SETUP)", and manufacturer codes (P1xxx and so on) get that make's meanings. Values read from the VIN always win (the VEHICLE page marks them "(VIN)"); tapping the VIN row there reads it again. They're saved as `make`, `model` and `year` under `[vehicle]` in `config.ini`.
   - **CLEAR** (only if you want the codes gone: it also resets the readiness monitors, so the car needs more driving before an emissions test): opens a question explaining that; **CANCEL** or B backs out, the red **CLEAR CODES** clears. Gauge Boy first checks the speed is 0, then sends the clear and scans again. Parked with the ignition on and the engine off works on most cars; with the engine running some cars refuse.
4. Go back to HOME and tap **LIVE** (engine running), then pick a view: **Engine**, **Idle & vacuum**, **Air & fuel**, **Warm-up**, **Drive** (a few PIDs each, for one job), **All** (every PID the car supports) or **Custom** (your own pick: **EDIT PIDS** has a toggle for every supported PID, green = polled). The top screen shows the view's values with how often each refreshes (fast ones like RPM as often as possible, slow ones like coolant every few seconds), the adapter's battery voltage, and manifold vacuum once it has seen MAP with the engine off or barometric pressure (on cars without a baro PID, such as the Ridgeline, turn the key to ON with the engine off and open Idle & vacuum once: the reading is saved for that car, and later sessions show vacuum from the start, marked "saved baro") (on turbo cars such as the Ascent, a **Boost** row in psi replaces it under boost: check Idle & vacuum shows MAP, Baro and Vacuum at idle and Boost on a firm pull, passenger watching); All and Custom also say how often the fast values refresh. **<** / **>** (or L / R) step through the views, **STOP** / **START** pauses polling, **BACK** returns to the view list. The last view and the Custom PIDs are saved under `[live]` in `config.ini`. Note the requests per second on the top line: it shows how fast the adapter and car answer. On CAN cars the log's LIVE summary also says "engine ECU only" (LIVE asks just the engine computer, so other computers' replies can't get mixed in), whether combined requests carried the response count, and how many PIDs were left out of combined replies, with the first three replies (send these logs: they show why).
   - **REC** (on any view; a passenger operates it, or start it parked): the button turns red with the time and the top screen shows `REC m:ss` and the file name. Drive a little, then tap REC again (or STOP / BACK). The log says `= REC stopped (...): drive-....csv, N rows, m:ss, N KB`. Copy `sdmc:/3ds/gauge-boy/drives/drive-....csv` off the card and open it in a spreadsheet: a header with the car and units, then 5 rows a second. Switching views while recording starts a new file.
   - **LOG** (on HOME, after recording): lists the recorded drives, newest first, with date, car and length. Tap one: the top screen shows its date, car, VIN, protocol, length, size and values. **DELETE** asks first, then removes the file. **GRAPH** plots the selected drive's first value against time on the top screen: check that < > step through the values, ZOOM + / - and << >> change the time window, ALL shows the whole drive, and BACK returns to the list. Note how long a long drive takes to load.
5. Back on HOME, tap **TRIM** (engine running, warm). It shows short-term, long-term and total fuel trim for each bank (one bank on inline engines such as the Tracker) as bars from -25 to +25 %: green within 10 %, amber 10 to 20 %, red beyond, with `<` or `>` at the end when off the scale. Under them: the engine speed, whether it's at idle, and hints such as "Both banks lean at idle: possible vacuum leak" (hints, not diagnoses). Check at idle, then at a steady 2500 rpm while parked. **BACK** returns to HOME. If DIAG finds a lean or rich code (P0171, P0172, P0174, P0175), DIAG also shows a **TRIM** button that opens this screen. **REC** on TRIM records the trims (with Total B1 / B2, RPM, MAP or MAF and load) to `drives/`: record a minute at idle, then at a steady 2500 rpm, stop, and look at it in LOG > GRAPH (Total B1 shows the lean or rich trend).
6. Then **SETUP > WI-FI > Connect to the adapter** (expect `= Connected (... ms)`), then **SETUP > TESTS** and tap, in order:
   - **ATZ** 10 or more times: expect `ELM327 v...` each time.
   - **ATSP0+0100**: expect `SEARCHING...` then a `41 00 ...` reply.
   - **ATDPN**: the 2008 Ridgeline reports `Protocol 3: ISO 9141-2 (5 baud init) (auto)`. Other cars can report any OBD-II protocol (J1850 PWM / VPW, ISO 9141-2, ISO 14230-4 KWP, ISO 15765-4 CAN); with only the ignition on, `0100` may say `UNABLE TO CONNECT` until the engine runs.
   - **ATRV**: expect roughly 12.4 to 14.5 V (higher with the engine running).
   - **0902 VIN**: expect several `0: 49 02 ...` lines.
   - **SPEED TEST** (engine running): sends each kind of request 10 times and logs how long they take: `ATRV` (the adapter alone, so Wi-Fi and adapter time), `010C` (a car request), `010C1` (the adapter returns after one reply instead of waiting for more ECUs; LIVE uses this), then `010C1` with `ATAT2` (the adapter's aggressive timing) and with `ATST19` (a 100 ms reply timeout instead of 200 ms); the defaults are put back afterwards. The last line sums it up, e.g. `= Speed (avg ms): adapter 393, car 631, one reply 399, ATAT2 ..., ATST19 ...`. Save the log afterwards.
   - **LIVE TEST** (engine running, about 15-25 s): opens the link, asks every supported PID Gauge Boy decodes once and logs each reply and value (`= Decode: n of m supported PIDs read`), then polls the Engine view for 10 s as LIVE does and logs the requests per second, each PID's refresh rate, whether requests carried several PIDs, and the last values.
7. Transport checks (on SETUP > TESTS):
   - **Lid:** close the lid for 10 seconds, open it, tap ATRV. It should reconnect on its own.
   - **Unplug:** unplug the adapter, tap ATRV a few times (retries back off up to 8 s), plug it back in, tap SETUP > WI-FI > Connect to the adapter.
   - **Wrong IP:** set a wrong `host` in `config.ini`, relaunch, tap ATZ: "Adapter did not answer" within about 3 s and the app stays responsive. Put the right address back afterwards.

## 7. Report the results

Every scan and every clear is also added to `sdmc:/3ds/gauge-boy/code-history.csv` (date, VIN, car, check engine light, code type, code, description), which opens in any spreadsheet and keeps the codes after they're cleared.

Tap **SAVE LOG** when you're done. It copies everything from this session to a new file and shows its name in the log, for example `sdmc:/3ds/gauge-boy/logs/gauge-boy-log-20261115-114205.txt` (date and time from the 3DS clock). Copy that file off the SD card (card reader, or FTP with ftpd) and attach it to your report. `sdmc:/3ds/gauge-boy/spike-log.txt` keeps every session, one after the other.

File a [compatibility report](https://github.com/ly-man-do/gauge-boy/issues/new?template=compatibility-report.yml) per adapter with the saved log. Results go into the hardware test issue `gauge-boy-7v5.11`.

## Troubleshooting

| Problem | Try |
|---|---|
| Gauge Boy isn't in the Homebrew Launcher | Check the path is `/3ds/gauge-boy/gauge-boy.3dsx` and the file isn't a zip. |
| "Unsupported device" | It's an Old 3DS / 2DS. Gauge Boy needs a New 3DS model. |
| Adapter network not listed | Ignition on? Adapter LED lit? The 3DS can't see ad-hoc or 5 GHz networks. |
| `Network error (errno 115 at socket(): ...)` | The 3DS isn't on the adapter's Wi-Fi. Tap CONNECT again and read the Wi-Fi join line above it. As a manual fallback, open the Rosalina menu (**L + Down + Select**) > **System configuration** > **Control wireless connection**, force the adapter's slot, then return to Gauge Boy. |
| Adapter joined, but **Test Connection** fails with `003-2002` and CONNECT gives errno 115 | The 3DS rejects networks without internet. Answer its connection test with `tools/fake_conntest.py` on a computer on the adapter's network (see `tools/README.md`, "Fake 3DS connection test"). |
| `Network:` shows your home network | The 3DS picked your home Wi-Fi. Move out of its range (or switch the router's Wi-Fi off) and relaunch. |
| Joined, but every command says "did not answer" or "unreachable" | Check the slot settings (step 3) and check `host`/`port` in `config.ini` against the adapter's manual. |
| Garbled or odd replies | Keep the log: clone adapters misbehave, and the raw lines are what's needed to handle them. |
