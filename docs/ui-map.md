# Gauge Boy screen and button map

Every screen and button in the app as it is now. Kept up to date with each UI change (`CLAUDE.md` section 5).

**As of:** 2026-11-19, LIVE v5 fixes: edge half circles, (B) Back and START caps (gauge-boy-82l.17)

The arrow (→) shows where a button goes or what it does. On the bottom screen everything is reached by touch; the physical buttons in brackets are optional shortcuts.

```
Gauge Boy
├── Unsupported device screen        (Old 3DS only: tap or START exits)
└── Safety notice                    (tap the touch screen to continue)
    └── HOME                         (graphics, night theme, the 3DS system pieces. Top screen:
                                     the HOME Menu's status bar (signal, Connected / Not
                                     Connected pill, date, time, battery), the GAUGE BOY:
                                     OBD-II DELUXE title row, and an info panel of fact rows:
                                     Car, CEL (lamp, Off / ON and how far), Codes (count and
                                     the first ones), Odometer (big LCD digits), Adapter
                                     (version and protocol, or Not connected and the address))
        ├── [Garage]                 → GARAGE (icon dims before a car is known; BACK returns to HOME)
        │   └── GARAGE               (no polling; top: one car's card: a picture frame (photo
        │       │                     in M5), nickname, year make model, VIN or "(set in
        │       │                     SETUP)", logged distance and drives, odometer, bests;
        │       │                     "Counting drive n of m..." while it reads new drives)
        │       ├── [TRIP]           → TRIP METER
        │       ├── [MILEAGE LOG]    → MILEAGE LOG
        │       ├── [<] [>]          the card before / after (the connected car's first)
        │       ├── [NICKNAME]       keyboard; saved to garage.csv (empty removes it)
        │       ├── [BACK]           → HOME                                       [B]
        │       ├── TRIP METER       (the card's car: odometer now (live if it's the connected
        │       │   │                 car: polls PID A6, else the last saved), trip distance
        │       │   │                 in big digits since RESET TRIP, its date and start)
        │       │   ├── [RESET TRIP] a new trip from the odometer now; refused while moving
        │       │   └── [BACK]       → GARAGE (stops polling)                     [B]
        │       └── MILEAGE LOG      (the card's car: average economy, then the newest
        │           │                 entries: date, end odometer, distance, fuel, mpg / L/100,
        │           │                 cost; "Spent" total and cost a mile / km)
        │           │                 (bottom: the entries newest first, 3 a page; tap one to
        │           │                 select it, green; it's highlighted on top too)
        │           ├── [NEW ENTRY]  number pad x4: start odometer (from the last entry),
        │           │                 end odometer (from the odometer), fuel, price per gallon
        │           │                 (US) or litre (metric), optional; refused while moving
        │           ├── [<] n/m [>]  list pages
        │           ├── [EDIT]       the selected entry: the same questions, filled in with it
        │           ├── [DELETE]     the selected entry, after a second tap ("AGAIN")
        │           └── [BACK]       → GARAGE                                     [B]
        ├── [Adapter]                → Wi-Fi guide (green Wi-Fi icon when connected; red and crossed
        │                              out when not)
        ├── [LIVE]                   → LIVE view list
        ├── [DIAG]                   → DIAG (amber outline, icon and label when the CEL is on)
        ├── [TRIM]                   → TRIM (starts polling)
        ├── [TRACK]                  → TRACK
        │   ├── TRACK                (top: the modes, closed-course rules)
        │   │   ├── [TIME ATTACK]    → closed-course notice (once a session), then TIME ATTACK
        │   │   │                      notice: [BACK] → TRACK [B]   [I UNDERSTAND] → TIME ATTACK
        │   │   ├── [SHIFT LIGHT]    → SHIFT LIGHT
        │   │   ├── [G-METER]        → G-METER
        │   │   ├── [REV SYNC]       → REV SYNC
        │   │   ├── [ECO]            → ECO CHALLENGE
        │   │   ├── [ACHIEVEMENTS]   → ACHIEVEMENTS (next to BACK)
        │   │   └── [BACK]           → HOME                                       [B]
        │   ├── ACHIEVEMENTS         (top: the 12 stickers, unlocked with a date, or locked
        │   │   │                     with how to get them; "n of 12 unlocked")
        │   │   └── [BACK]           → TRACK                                      [B]
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
        │   │   ├── [CALIBRATE]      measures gravity (about 1 s, averaged; hold still); refused while moving
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
        ├── [LOG]                    → LOG (lists the drives on the SD card)
        ├── [SETUP]                  → SETUP
        └── [START Close bar]        → closes Gauge Boy            [START]
        │
        ├── LIVE view list           (top: the Dashboard: tach 0-8 x1000 rpm, speed, coolant
        │   │                        C-H, battery 10-16 V, CEL engine icon, odometer;
        │   │                        polls RPM, speed, coolant and the odometer)
        │   ├── [Engine] [Warm-up] [Drive] [Air/Fuel] [Idle/Vacuum]
        │   ├── [O2 Sensors] [Transmission] [Custom] [All]
        │   │                        a list that scrolls by dragging it or the scroll bar's grip
        │   │                        (a touch on the bar moves the grip there); tap a view →
        │   │                        LIVE view (starts polling). Transmission polls the
        │   │                        transmission module alone (CAN cars)
        │   ├── [B Back]             → HOME, stops polling                        [B]
        │   └── LIVE view            (top: the view's name and its values, one or two columns)
        │       │                    (bottom: "n PIDs, r a second" at the top; its controls
        │       │                    look pressed while held and act when the finger lifts)
        │       ├── [<] [>]          half circles at the edges: previous / next view  [L/R]
        │       ├── ● REC m:ss       above the toggle while recording (the dot pulses)
        │       ├── [Stop | Record]  a toggle: Record records this view to
        │       │                    drives/drive-<date>.csv; Stop ends it (the chosen half is red)
        │       ├── [Edit PIDs]      (Custom only) → Custom PIDs
        │       │   ├── [PID toggles] 6 per page, green = polled (the PIDs the car supports)
        │       │   ├── [BACK]       → Custom view, saves the choice              [B]
        │       │   └── [<] page n/m [>]                                          [L/R]
        │       └── [B Back]         → view list and the Dashboard (ends a recording, saves
        │                            the view)                                    [B]
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
        │                            code with a freeze frame adds its values; facts list the
        │                            modules that answered; a code's text says which set it)
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
        │   │   ├── [CHECK & CONNECT  Connected/Not connected/checking...]  Wi-Fi (joins the
        │   │   │                    slot if needed), reaching the adapter, the adapter
        │   │   │                    answering, the car; below: "Connected." with the adapter
        │   │   │                    and car, or "Failed at: <step>" and what to fix (and the
        │   │   │                    slot's IP / DNS when they differ from the guide)
        │   │   ├── [Wi-Fi slot                  Any/1/2/3]   cycles the slot, saved
        │   │   ├── [CONNECTION GUIDE >]          → Wi-Fi guide (the setup steps)
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
            └── [CHECK]              the same CHECK & CONNECT → results on bottom
```

Any screen: a new achievement shows "Unlocked: <name>!" (or "Unlocked n achievements!") in the bottom screen's notice line for 5 s.

## Notes

- SETUP is a menu of four pages (owner layout, gauge-boy-cib): TESTS (adapter and car tests, the log's buttons), WI-FI (connection), VEHICLE (make, model, year, engine size, VIN) and OTHER (units, About).
- LIVE views and the PIDs in each, in list order: Engine (RPM, speed, coolant, throttle, load), Idle/Vacuum (RPM, MAP (PID 0B or 87) + vacuum or boost, short-term trims, timing, baro, fuel loop), Air/Fuel (MAF, MAP (0B or 87), intake air, throttle, short- and long-term trims, baro, fuel loop, commanded lambda), O2 Sensors (RPM, short-term trims, then every oxygen sensor the car has: narrow band voltage, PIDs 14-1B, or wide band lambda, 24-2B / 34-3B, banks 1-2, sensors 1-4), Warm-up (coolant, intake air, oil, outside air, run time, RPM), Drive (RPM, speed, throttle, load, timing, torque, gear ratio, odometer, fuel rate (the car's PID 5E / 9D, else estimated from MAF or MAP + engine size) and instant economy), All (every supported PID), Custom (your pick). Each view only shows what the car supports.
- Only HOME (bottom screen) and LOG > GRAPH (top screen) are drawn with graphics (citro2d). Every other screen is still the text console until M5 (UI polish).
- The code for these screens is in `source/spike_ui.c` (screens, buttons, actions) and `source/ui/` (HOME layout and drawing).
