# Gauge Boy screen and button map

Every screen and button in the app as it is now. Kept up to date with each UI change (`CLAUDE.md` section 5).

**As of:** 2026-11-18, TRACK > ECO CHALLENGE, SETUP > VEHICLE engine size (gauge-boy-356.8; M4 in progress)

The arrow (→) shows where a button goes or what it does. On the bottom screen everything is reached by touch; the physical buttons in brackets are optional shortcuts.

```
Gauge Boy
├── Unsupported device screen        (Old 3DS only: tap or START exits)
└── Safety notice                    (tap the touch screen to continue)
    └── HOME                         (graphics; top screen: car facts, found by itself
                                     after connecting: car, VIN, protocol)
        ├── [CEL card]               → DIAG        (shows CEL off / CEL ON / CEL not read)
        ├── [Adapter card]           → Wi-Fi guide (shows Connected / Not connected)
        ├── [LIVE  · Dash gauges]    → LIVE view list
        ├── [DIAG  · Codes & CEL]    → DIAG
        ├── [TRIM  · Fuel trims]     → TRIM (starts polling)
        ├── [TRACK · Track modes]    → TRACK
        │   ├── TRACK                (top: the modes, closed-course rules)
        │   │   ├── [TIME ATTACK]    → closed-course notice (once a session), then TIME ATTACK
        │   │   │                      notice: [BACK] → TRACK [B]   [I UNDERSTAND] → TIME ATTACK
        │   │   ├── [SHIFT LIGHT]    → SHIFT LIGHT
        │   │   ├── [G-METER]        → G-METER
        │   │   ├── [REV SYNC]       → REV SYNC
        │   │   ├── [ECO]            → ECO CHALLENGE
        │   │   └── [BACK]           → HOME                                       [B]
        │   ├── ECO CHALLENGE        (polls speed, RPM, MAF or MAP, intake temp; top: trip
        │   │   │                     score in big digits and its rank once ranked, economy now
        │   │   │                     and for the trip, distance, fuel, driving / idling time,
        │   │   │                     hard starts / stops, a tip, this car's best trip score)
        │   │   ├── [RESET TRIP]     a new trip; refused while moving
        │   │   └── [BACK]           → TRACK (a ranked trip's score is offered as the best)
        │   ├── REV SYNC             (polls RPM, speed twice a second; top: tach bar 0-7000
        │   │   │                     with the target marked and the blip's peak, big target /
        │   │   │                     GO! / blip / score, the grade, the rounds so far, this
        │   │   │                     car's high score; locked unless speed reads 0)
        │   │   ├── [START] / [AGAIN] a 5-round game; refused while locked
        │   │   └── [BACK]           → TRACK (stops polling)                      [B]
        │   ├── G-METER              (3DS accelerometer on; polls speed; top: a friction
        │   │   │                     circle, 1 g at the edge, ring at 0.5 g, ACCEL up,
        │   │   │                     BRAKE down, the ball; side / long / total g, peaks,
        │   │   │                     forward guessed or learned, raw sensor values)
        │   │   ├── [CALIBRATE]      measures gravity (hold still); refused while moving
        │   │   ├── [RESET PEAKS]    clears the peaks; refused while moving
        │   │   └── [BACK]           → TRACK (accelerometer off, stops polling)   [B]
        │   ├── SHIFT LIGHT          (polls RPM, speed every second; top: a row of 10 LEDs,
        │   │   │                     green / amber / red, filling from 60 % of the shift
        │   │   │                     point and flashing at it with SHIFT!, RPM in big digits)
        │   │   ├── [-100] [+100]    shift point (shown between them, saved to config.ini);
        │   │   │                     refused while moving ("Set the shift point while parked.")
        │   │   └── [BACK]           → TRACK (stops polling)                      [B]
        │   └── TIME ATTACK          (polls speed alone; top: run, speed readings a second,
        │       │                     state in big text: stop the car / 3 2 1 / GO! / time /
        │       │                     result with NEW RECORD!, speed, this car's best)
        │       ├── [0-60] [60-0] [1/4 MILE]   pick the run (0-100 / 100-0 in metric);
        │       │                     green = picked; refused while moving ("Pick a run
        │       │                     while parked."); starts and re-arms by itself
        │       └── [BACK]           → TRACK (stops polling)                      [B]
        ├── [LOG   · Record drives]  → LOG (lists the drives on the SD card)
        ├── [SETUP · Settings]       → SETUP
        └── [START Close bar]        → closes Gauge Boy            [START]
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
        │   ├── [REC / REC m:ss]     records the trims (with totals, RPM, MAP / MAF, load)
        │   │                        to drives/; red while recording; tap again to stop
        │   └── [START / STOP]       pauses or resumes polling (STOP also ends a recording)
        │
        ├── DIAG                     (top: car facts or the selected code's text; a stored
        │                            code with a freeze frame adds its values)
        │   ├── [STORED] [PENDING] [PERM] [READY]   tabs (PERM hidden if unsupported)
        │   ├── [list rows]          3 per page, tap → full description on top
        │   ├── [BACK]               → HOME                                       [B]
        │   ├── [<] page n/m [>]                                                  [L/R]
        │   ├── [TRIM]               → TRIM (only when P0171 / P0172 / P0174 / P0175 is
        │   │                        stored or pending; the bottom row narrows to fit it)
        │   ├── [CLEAR]              → Clear codes dialog (needs a scan first)
        │   │   ├── [CANCEL]         → DIAG                                       [B]
        │   │   └── [CLEAR CODES]    clears (refused if moving), then rescans
        │   └── [SCAN]               reads CEL, readiness, codes, the freeze frame (with stored
        │                            codes), VIN (and model)                        [X]
        │
        ├── LOG                      (top: the selected drive's details: date, car, VIN,
        │   │                        protocol, length, size, values; else what LOG is)
        │   ├── [list rows]          3 per page, newest first: date, car, length;
        │   │                        tap → details on top (green = selected)
        │   ├── [BACK]               → HOME                                       [B]
        │   ├── [<] page n/m [>]                                                  [L/R]
        │   ├── [GRAPH]              (a drive selected) → GRAPH
        │   │   ├── (top: one value against time, drawn with graphics: axes, times,
        │   │   │    a smooth line through the readings over a faint min / max band,
        │   │   │    the window's min / max / average)
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
        │   │   ├── [SAVE LOG]       → logs/gauge-boy-log-<date>.txt
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
        │   │   ├── [Engine  Unknown/<n.n> L] [<] [>]  0.6-8.0 L by 0.1 (from Unknown: 2.0); saved;
        │   │   │                    for ECO's fuel estimate on cars without a MAF
        │   │   ├── [VIN  <VIN>/tap to read]  (next to BACK) runs a DIAG scan
        │   │   └── [BACK]           → SETUP                                      [B]
        │   ├── [OTHER]              → SETUP > OTHER
        │   │   ├── [Units        US/Metric]     toggles, saved (LIVE and the log use it)
        │   │   ├── [About Gauge Boy >]           → About (top: version, license, sources)
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

- SETUP is a menu of four pages (owner layout, gauge-boy-cib): TESTS (adapter and car tests, the log's buttons), WI-FI (connection), VEHICLE (make, model, year, engine size, VIN) and OTHER (units, About).
- LIVE views and the PIDs in each: Engine (RPM, speed, coolant, throttle, load), Idle & vacuum (RPM, MAP (PID 0B or 87) + vacuum or boost, short-term trims, timing, baro), Air & fuel (MAF, MAP (0B or 87), intake air, throttle, short- and long-term trims, baro), Warm-up (coolant, intake air, oil, outside air, run time, RPM), Drive (RPM, speed, throttle, load, timing), All (every supported PID), Custom (your pick). Each view only shows what the car supports.
- Only HOME (bottom screen) and LOG > GRAPH (top screen) are drawn with graphics (citro2d). Every other screen is still the text console until M5 (UI polish).
- The code for these screens is in `source/spike_ui.c` (screens, buttons, actions) and `source/ui/` (HOME layout and drawing).
