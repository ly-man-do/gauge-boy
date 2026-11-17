# GaugeBoy screen and button map

Every screen and button in the app as it is now. Kept up to date with each UI change (`CLAUDE.md` section 5).

**As of:** 2026-11-16, LOG > GRAPH replays a recorded drive (gauge-boy-43v.5; M3 in progress)

The arrow (→) shows where a button goes or what it does. On the bottom screen everything is reached by touch; the physical buttons in brackets are optional shortcuts.

```
GaugeBoy
├── Unsupported device screen        (Old 3DS only: tap or START exits)
└── Safety notice                    (tap the touch screen to continue)
    └── HOME                         (graphics; top screen: car facts, found by itself
                                     after connecting: car, VIN, protocol)
        ├── [CEL card]               → DIAG        (shows CEL off / CEL ON / CEL not read)
        ├── [Adapter card]           → Wi-Fi guide (shows Connected / Not connected)
        ├── [LIVE  · Dash gauges]    → LIVE view list
        ├── [DIAG  · Codes & CEL]    → DIAG
        ├── [TRIM  · Fuel trims]     → TRIM (starts polling)
        ├── [FUN   · Fun modes]      → notice "comes in version 0.6"
        ├── [LOG   · Record drives]  → LOG (lists the drives on the SD card)
        ├── [SETUP · Settings]       → SETUP
        └── [START Close bar]        → closes GaugeBoy            [START]
        │
        ├── LIVE view list           (top: the last view's values)
        │   ├── [Engine] [Idle & vacuum] [Air & fuel]
        │   ├── [Warm-up] [Drive] [All]
        │   ├── [Custom]             tap a view → LIVE view (starts polling; green = last used)
        │   ├── [BACK]               → HOME                                       [B]
        │   └── LIVE view            (top: view, purpose or refresh warning, values,
        │       │                    refresh rates, battery, vacuum)
        │       ├── [BACK]           → view list, stops polling, saves the view   [B]
        │       ├── [<] view n/7 [>] previous / next view                         [L/R]
        │       ├── [START / STOP]   pauses or resumes polling (STOP also ends a recording)
        │       ├── [REC / REC m:ss] records this view to drives/drive-<date>.csv; red while
        │       │                    recording; tap again to stop (starts polling if stopped)
        │       └── [EDIT PIDS]      (Custom only) → Custom PIDs
        │           ├── [PID toggles] 6 per page, green = polled (the PIDs the car supports)
        │           ├── [BACK]       → Custom view, saves the choice              [B]
        │           └── [<] page n/m [>]                                          [L/R]
        │
        ├── TRIM                     (top: short / long / total trim per bank as
        │   │                        colored center-zero bars, engine state, hints)
        │   ├── (bottom: what trims mean, the color zones, how to check them)
        │   ├── [BACK]               → HOME (or DIAG, if opened from there), stops polling [B]
        │   └── [START / STOP]       pauses or resumes polling
        │
        ├── DIAG                     (top: car facts or the selected code's text)
        │   ├── [STORED] [PENDING] [PERM] [READY]   tabs (PERM hidden if unsupported)
        │   ├── [list rows]          3 per page, tap → full description on top
        │   ├── [BACK]               → HOME                                       [B]
        │   ├── [<] page n/m [>]                                                  [L/R]
        │   ├── [TRIM]               → TRIM (only when P0171 / P0172 / P0174 / P0175 is
        │   │                        stored or pending; the bottom row narrows to fit it)
        │   ├── [CLEAR]              → Clear codes dialog (needs a scan first)
        │   │   ├── [CANCEL]         → DIAG                                       [B]
        │   │   └── [CLEAR CODES]    clears (refused if moving), then rescans
        │   └── [SCAN]               reads CEL, readiness, codes, VIN (and model)  [X]
        │
        ├── LOG                      (top: the selected drive's details: date, car, VIN,
        │   │                        protocol, length, size, values; else what LOG is)
        │   ├── [list rows]          3 per page, newest first: date, car, length;
        │   │                        tap → details on top (green = selected)
        │   ├── [BACK]               → HOME                                       [B]
        │   ├── [<] page n/m [>]                                                  [L/R]
        │   ├── [GRAPH]              (a drive selected) → GRAPH
        │   │   ├── (top: one value against time, drawn with graphics: axes, times,
        │   │   │    min / max per pixel column, the window's min / max / average)
        │   │   ├── [<<] [>>]        earlier / later (half a window)
        │   │   ├── [ZOOM -] [ZOOM +] more / less of the drive (5 s at the least)
        │   │   ├── [<] value n/m [>] the drive's other values                    [L/R]
        │   │   ├── [ALL]            the whole drive
        │   │   └── [BACK]           → LOG (same drive selected)                  [B]
        │   └── [DELETE]             (a drive selected) → Delete question
        │       ├── [CANCEL]         → LOG                                        [B]
        │       └── [DELETE]         removes the file from the SD card, lists again
        │
        ├── SETUP                    (top: session log on every SETUP page but About)  [L/R scroll log]
        │   ├── [TESTS]              → SETUP > TESTS
        │   │   ├── [ATZ] [ATSP0+0100] [ATDPN]
        │   │   ├── [ATRV] [0902 VIN] [SPEED TEST]
        │   │   ├── [LIVE TEST]      every supported PID once, then 10 s of the Engine view
        │   │   ├── [SAVE LOG]       → logs/gaugeboy-log-<date>.txt
        │   │   └── [BACK → SETUP] [OLDER ^] [NEWER v]   (scroll the log)  [B, Up/Down, L/R]
        │   ├── [WI-FI]              → SETUP > WI-FI
        │   │   ├── [Connect to the adapter      Connected/Not connected]
        │   │   ├── [Wi-Fi slot                  Any/1/2/3]   cycles the slot, saved
        │   │   ├── [Check the slot's IP and DNS >]  → Slot check screen
        │   │   │   └── [OK]         → SETUP > WI-FI
        │   │   ├── [Wi-Fi setup guide >]        → Wi-Fi guide
        │   │   └── [BACK]           → SETUP                                      [B]
        │   ├── [VEHICLE]            → SETUP > VEHICLE  (VIN values win, marked "(VIN)")
        │   │   ├── [Make         Auto/<make>]   → Car make picker
        │   │   │   ├── [Auto] [Acura] [Audi] …   12 per page, 3 pages; tap saves → VEHICLE
        │   │   │   ├── [BACK]       → SETUP > VEHICLE                            [B]
        │   │   │   └── [<] page n/3 [>]                                          [L/R]
        │   │   ├── [Model        Auto/<model>]  → 3DS keyboard; empty = Auto; saved
        │   │   ├── [Year  Auto/<year>] [<] [>]  steps Auto, 1996 ... next year; saved
        │   │   ├── [VIN (tap to read)  <VIN>/not read]  runs a DIAG scan
        │   │   └── [BACK]           → SETUP                                      [B]
        │   ├── [OTHER]              → SETUP > OTHER
        │   │   ├── [Units        US/Metric]     toggles, saved (LIVE and the log use it)
        │   │   ├── [About GaugeBoy >]           → About (top: version, license, sources)
        │   │   │   └── [BACK]       → SETUP > OTHER                              [B]
        │   │   └── [BACK]           → SETUP                                      [B]
        │   └── [BACK]               → HOME                                       [B]
        │
        └── Wi-Fi guide              (top: step text; 5 steps)
            ├── [BACK]               → SETUP > WI-FI, or HOME if opened from the Adapter card  [B]
            ├── [<] step n/5 [>]                                                  [L/R]
            └── [CHECK]              slot, IP/DNS, Wi-Fi, adapter → results on bottom
```

## Notes

- SETUP is a menu of four pages (owner layout, gauge-boy-cib): TESTS (adapter and car tests, the log's buttons), WI-FI (connection), VEHICLE (make, model, year, VIN) and OTHER (units, About).
- LIVE views and the PIDs in each: Engine (RPM, speed, coolant, throttle, load), Idle & vacuum (RPM, MAP (PID 0B or 87) + vacuum or boost, short-term trims, timing, baro), Air & fuel (MAF, MAP (0B or 87), intake air, throttle, short- and long-term trims, baro), Warm-up (coolant, intake air, oil, outside air, run time, RPM), Drive (RPM, speed, throttle, load, timing), All (every supported PID), Custom (your pick). Each view only shows what the car supports.
- Only HOME (bottom screen) and LOG > GRAPH (top screen) are drawn with graphics (citro2d). Every other screen is still the text console until M4 (UI polish).
- The code for these screens is in `source/spike_ui.c` (screens, buttons, actions) and `source/ui/` (HOME layout and drawing).
