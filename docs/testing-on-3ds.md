# Testing GaugeBoy on a real New 3DS

How to get the current build onto your New 3DS and run the M0 spike tests with a Wi-Fi ELM327 adapter.

> **Safety:** do every step here parked, with the engine off or idling, or as a passenger. Never touch the 3DS while driving.

## What you need

- A **New** Nintendo 3DS, New 3DS XL or New 2DS XL with **Luma3DS** and the **Homebrew Launcher**. If you don't have these yet, follow [3ds.hacks.guide](https://3ds.hacks.guide/). Older (non-New) 3DS models show "unsupported device" and exit.
- A **Wi-Fi** ELM327 adapter (Bluetooth adapters can't work: the 3DS has no Bluetooth radio).
- One way to put files on the SD card: a card reader, FTP, or `3dslink` (see step 2).

## 1. Get `gaugeboy.3dsx`

Pick one.

**From GitHub (no build needed).** Every push builds the app.

```bash
rm -f ~/Downloads/gaugeboy-ci/gaugeboy.3dsx && gh run download -R ly-man-do/gauge-boy -n gaugeboy-3dsx -D ~/Downloads/gaugeboy-ci
```

That downloads the newest build's `gaugeboy.3dsx` into `~/Downloads/gaugeboy-ci/`. The `rm` is there because `gh` refuses to overwrite an existing file, so a second download (or one into the repo folder after a local build) would otherwise fail. In a browser: repo > **Actions** > latest **Build** run > **Artifacts** > `gaugeboy-3dsx` (it arrives as a zip; unzip it).

**Build it yourself** (from the repo folder, with Docker running):

```bash
docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src devkitpro/devkitarm make
```

## 2. Put it on the 3DS

The app goes in `sdmc:/3ds/gaugeboy/gaugeboy.3dsx`. The Homebrew Launcher finds apps in `/3ds/` and its subfolders, and GaugeBoy keeps its config and log in that same folder.

### Option A: SD card reader

1. Power off the 3DS and take out the SD card.
2. On the card, create `3ds/gaugeboy/` if it doesn't exist and copy `gaugeboy.3dsx` into it.
3. macOS leaves hidden `._` files on the card that can show up as broken entries. Remove them, then eject:

   ```bash
   dot_clean /Volumes/<card name>
   ```

4. Put the card back and power on.

### Option B: FTP over your home Wi-Fi (no card swapping)

1. Install **ftpd** on the 3DS (from Universal-Updater) and run it. It shows the 3DS's IP and port (usually `5000`).
2. On the Mac:

   ```bash
   curl -T gaugeboy.3dsx --ftp-create-dirs ftp://<3ds-ip>:5000/3ds/gaugeboy/
   ```

3. Quit ftpd (START).

### Option C: `3dslink` (fastest for repeated tests)

This sends the app over your home Wi-Fi and runs it at once, without touching the SD card. Good for quick UI checks; use A or B for the car test so the app is on the card.

1. Open the Homebrew Launcher and press **Y**. It waits for a connection and shows its IP.
2. On the Mac, from the repo folder:

   ```bash
   docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src devkitpro/devkitarm /opt/devkitpro/tools/bin/3dslink -a <3ds-ip> gaugeboy.3dsx
   ```

### Option D: install the `.cia` (HOME menu icon)

The `.cia` puts GaugeBoy on the HOME menu like a normal app, so you don't need the Homebrew Launcher to start it. It runs the same code as the `.3dsx`. You need **FBI** on the 3DS (from Universal-Updater).

1. Get `gaugeboy.cia` from CI:

   ```bash
   rm -f ~/Downloads/gaugeboy-ci/gaugeboy.cia && gh run download -R ly-man-do/gauge-boy -n gaugeboy-cia -D ~/Downloads/gaugeboy-ci
   ```

   or build it with:

   ```bash
   docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src devkitpro/devkitarm make cia
   ```

2. Copy it anywhere on the SD card (for example `/cias/gaugeboy.cia`) with a card reader or FTP.
3. In FBI: **SD** > find `gaugeboy.cia` > **Install and delete CIA** (or **Install CIA** to keep the file).
4. GaugeBoy appears on the HOME menu with a gauge icon. Installing a newer `.cia` later updates it in place, because the title ID never changes.

To remove it: FBI > **Titles** > GaugeBoy > **Delete Title**. Your `sdmc:/3ds/gaugeboy/` folder (config and log) stays.

## 3. Save the adapter's Wi-Fi on the 3DS

Do this once per adapter, in the car with the adapter plugged in and the ignition on (most adapters only power up then).

1. **System Settings** > **Internet Settings** > **Connection Settings**. Pick an empty slot (there are only 3; note which one you use).
2. **New Connection** > **Manual Setup** > **Search for an Access Point**, and pick the adapter's network (the Vgate iCar Pro calls it `V-LINK`).
   - If the adapter's network doesn't appear, it is probably ad-hoc or 5 GHz, which the 3DS can't join. Note that in your report.
3. Enter the password if it has one, then **OK** to save.
4. The **connection test will fail** because the adapter has no internet. That's expected. Make sure the slot is still saved.
5. **If the 3DS can't get an IP address** (many adapters have no DHCP server), set one by hand: open the slot > **Change Settings**:
   - **IP Address:** Auto-Obtain **No**; IP Address `192.168.0.11`, Subnet Mask `255.255.255.0`, Gateway `192.168.0.10`.
   - **DNS:** Auto-Obtain **No**; Primary DNS `192.168.0.10`.

   This assumes the adapter is at `192.168.0.10`. If its manual says otherwise, use an address in the same subnet.

The 3DS uses whichever saved connection is in range, so your home Wi-Fi slot can stay.

Because the adapter has no internet, the 3DS may not stay on its network by itself. GaugeBoy handles this: when you tap CONNECT and the 3DS isn't on Wi-Fi, it asks the 3DS to join a saved connection (like Rosalina's **Force wireless connection**) and logs the result. The `Network:` line in the log shows which network and slot the 3DS is on and its IP address.

## 4. Launch GaugeBoy

If you installed the `.cia`, start GaugeBoy from the HOME menu. Otherwise, open the Homebrew Launcher the way you usually do. With a standard Luma3DS setup: open **Download Play**, press **L + Down + Select** for the Rosalina menu, choose **Miscellaneous options** > **Switch the hb. title to the current app**, back out with B, close Download Play with HOME, and open Download Play again.

Pick **GaugeBoy**. You should see the safety notice; tap the bottom screen to continue.

On first launch the app creates `sdmc:/3ds/gaugeboy/config.ini`:

```ini
[adapter]
host = 192.168.0.10
port = 35000
```

If your adapter uses a different address, edit the file (card reader or FTP) and relaunch. The bottom screen header shows the address in use.

## 5. Smoke test (at home, no adapter)

1. The bottom screen shows 8 buttons: CONNECT, ATZ, ATSP0+0100, ATDPN, ATRV, 0902 VIN, OLDER, NEWER. A tap flashes the button yellow; while an action runs it turns red and the top header shows `BUSY`.
2. Tap **ATZ**. About 3 seconds later the log shows "Adapter did not answer" (or "refused or unreachable"). The buttons keep responding meanwhile.
3. OLDER / NEWER scroll the log.
4. Exit with **START**. On the SD card, `sdmc:/3ds/gaugeboy/spike-log.txt` has the same lines as the screen.

Report anything garbled (colors, layout, text running off the screen).

## 6. Car test (parked)

1. Plug in the adapter, ignition on (engine running is fine).
2. Make sure the 3DS's wireless switch is on and it has joined the adapter's slot.
3. Launch GaugeBoy and tap, in order:
   - **CONNECT**: expect `= Connected (... ms)`.
   - **ATZ** 10 or more times: expect `ELM327 v...` each time.
   - **ATSP0+0100**: expect `SEARCHING...` then a `41 00 ...` reply.
   - **ATDPN**: on the 2008 Ridgeline, expect `Protocol 6: ISO 15765-4 CAN (11 bit, 500 kbaud) (auto)`.
   - **ATRV**: expect roughly 12.4 to 14.5 V (higher with the engine running).
   - **0902 VIN**: expect several `0: 49 02 ...` lines.
4. Transport checks:
   - **Lid:** close the lid for 10 seconds, open it, tap ATRV. It should reconnect on its own.
   - **Unplug:** unplug the adapter, tap ATRV a few times (retries back off up to 8 s), plug it back in, tap CONNECT.
   - **Wrong IP:** set a wrong `host` in `config.ini`, relaunch, tap ATZ: "Adapter did not answer" within about 3 s and the app stays responsive. Put the right address back afterwards.

## 7. Report the results

File a [compatibility report](https://github.com/ly-man-do/gauge-boy/issues/new?template=compatibility-report.yml) per adapter and paste the relevant part of `spike-log.txt`. Results go into the hardware test issue `gauge-boy-7v5.11`.

## Troubleshooting

| Problem | Try |
|---|---|
| GaugeBoy isn't in the Homebrew Launcher | Check the path is `/3ds/gaugeboy/gaugeboy.3dsx` and the file isn't a zip. |
| "Unsupported device" | It's an Old 3DS / 2DS. GaugeBoy needs a New 3DS model. |
| Adapter network not listed | Ignition on? Adapter LED lit? The 3DS can't see ad-hoc or 5 GHz networks. |
| `Network error (errno 115 at socket(): ...)` | The 3DS isn't on the adapter's Wi-Fi. Tap CONNECT again and read the Wi-Fi join line above it. As a manual fallback, open the Rosalina menu (**L + Down + Select**) > **System configuration** > **Control wireless connection**, force the adapter's slot, then return to GaugeBoy. |
| Adapter joined, but **Test Connection** fails with `003-2002` and CONNECT gives errno 115 | The 3DS rejects networks without internet. Answer its connection test with `tools/fake_conntest.py` on a computer on the adapter's network (see `tools/README.md`, "Fake 3DS connection test"). |
| `Network:` shows your home network | The 3DS picked your home Wi-Fi. Move out of its range (or switch the router's Wi-Fi off) and relaunch. |
| Joined, but every command says "did not answer" or "unreachable" | Set the manual IP (step 3.5) and check `host`/`port` in `config.ini` against the adapter's manual. |
| Garbled or odd replies | Keep the log: clone adapters misbehave, and the raw lines are what's needed to handle them. |
