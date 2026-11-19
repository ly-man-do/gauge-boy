# Gauge Boy screen and button map

Every screen and button in the app as it is now. Kept up to date with each UI change (`CLAUDE.md` section 5).

**As of:** 2026-11-19, LIVE's Custom PID picker as a list (gauge-boy-82l.26)

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
        ├── [Garage]                 → GARAGE (icon dims before a car is known; Back returns to HOME)
        │   └── GARAGE               (no polling; top: one car's card: its photo (the Garage
        │       │                     icon and "No photo yet" until there is one), nickname,
        │       │                     year make model, VIN or "(set in SETUP)", odometer in
        │       │                     LCD digits, logged distance and drives, two bests;
        │       │                     "Counting drive n of m..." while it reads new drives)
        │       ├── (< name >)       half circles: the card before / after (the connected car's first)
        │       ├── [Nickname]       keyboard; saved to garage.csv (empty removes it)
        │       ├── [Photo]          → CAMERA (the outer camera live on the top screen, corner
        │       │   │                 guides around the part kept)
        │       │   ├── [Take Photo] the shot (or A) → [Use Photo] keeps it as the card's photo
        │       │   │                (photos/<car>.bmp, 160 x 120), [Retake], [Remove] (no photo)
        │       │   └── [B Back]     → GARAGE (camera off)                        [B]
        │       ├── [Trip]           → TRIP METER
        │       ├── [Mileage Log]    → MILEAGE LOG
        │       ├── [Y Info]         what the Garage counts, the half circles, Nickname, Photo  [Y]
        │       ├── [B Back]         → HOME                                       [B]
        │       ├── TRIP METER       (the card's car: the trip in LCD digits since Reset Trip,
        │       │   │                 when and where it started, the odometer now (live if
        │       │   │                 it's the connected car: polls PID A6, else the last saved))
        │       │   ├── [Reset Trip] a new trip from the odometer now; refused while moving
        │       │   ├── [Y Info]     how the trip meter reads the odometer        [Y]
        │       │   └── [B Back]     → GARAGE (stops polling)                     [B]
        │       └── MILEAGE LOG      (top: average economy, distance, fuel, money spent)
        │           ├── [entry rows] newest first: date, odometer and distance, economy
        │           │   │            (scrolls); tap → the entry's page (top: its numbers)
        │           │   ├── [Edit]   number pad x4, filled in with it
        │           │   ├── [Delete] → Delete question (top: what it removes): [Cancel] /
        │           │   │            red [Delete]                                 [B cancels]
        │           │   └── [B Back] → the list                                   [B]
        │           ├── [X New Entry] number pad x4: start odometer (from the last entry),
        │           │                 end odometer (from the odometer), fuel, price per gallon
        │           │                 (US) or litre (metric), optional; refused while moving
        │           └── [B Back]     → GARAGE                                     [B]
        ├── [Adapter]                → Wi-Fi guide (green Wi-Fi icon when connected; red and crossed
        │                              out when not)
        ├── [LIVE]                   → LIVE view list
        ├── [DIAG]                   → DIAG (amber outline, icon and label when the CEL is on)
        ├── [TRIM]                   → TRIM (starts polling)
        ├── [TRACK]                  → TRACK
        │   ├── TRACK                (top: what each mode does, achievements unlocked)
        │   │   │                    Every TRACK screen: (B) Back bottom-left and (Y) Info
        │   │   │                    bottom-right; Info shows the screen's explanation (the top
        │   │   │                    keeps updating) and its Back returns to the controls [B]
        │   │   ├── [Time Attack]    → closed-course notice (once a session), then TIME ATTACK
        │   │   │                      notice (top: the rule): [I Understand] full width,
        │   │   │                      [B Back] → TRACK                              [B]
        │   │   ├── [Shift Light]    → SHIFT LIGHT
        │   │   ├── [G-Meter]        → G-METER
        │   │   ├── [Rev Sync]       → REV SYNC
        │   │   ├── [Eco]            → ECO CHALLENGE
        │   │   ├── [Achievements]   → ACHIEVEMENTS
        │   │   ├── [Y Info]         closed courses, track days and passengers only; no
        │   │   │                    touch while moving; no online leaderboards   [Y]
        │   │   └── [B Back]         → HOME                                       [B]
        │   ├── ACHIEVEMENTS         (top: the 12 stickers, coloured with a star when unlocked,
        │   │   │                     "n of 12 unlocked")
        │   │   ├── [list]           each one: its date, or how to get it (scrolls: drag or grip)
        │   │   └── [B Back]         → TRACK                                      [B]
        │   ├── ECO CHALLENGE        (polls speed, RPM, MAF or MAP, intake temp; top: trip
        │   │   │                     score in LCD digits and its rank badge once ranked,
        │   │   │                     economy now and for the trip, this car's best, distance,
        │   │   │                     fuel, driving / idling time, hard starts / stops, a tip)
        │   │   ├── [Reset Trip]     a new trip; refused while moving
        │   │   └── [B Back]         → TRACK (a ranked trip's score is offered as the best)
        │   ├── REV SYNC             (polls RPM, speed twice a second; top: a 0-7000 LED bar
        │   │   │                     with the target marked and the blip's peak, the target /
        │   │   │                     GO! / blip / score in big digits, the grade, target,
        │   │   │                     round and score, this car's high score)
        │   │   ├── (bottom: "Parked: speed 0. Ready." or "Locked")
        │   │   ├── [Start] / [Again] a 5-round game; refused while locked
        │   │   └── [B Back]         → TRACK (stops polling)                      [B]
        │   ├── G-METER              (3DS accelerometer on; polls speed; top: a friction
        │   │   │                     circle, 1 g at the edge, dashed ring at 0.5 g, ACCEL up,
        │   │   │                     BRAKE down, the ball; side / long / total g, peaks,
        │   │   │                     forward guessed or learned, or the calibration state)
        │   │   ├── [Calibrate]      measures gravity (about 1 s, averaged; hold still); refused while moving
        │   │   ├── [Reset Peaks]    clears the peaks; refused while moving
        │   │   └── [B Back]         → TRACK (accelerometer off, stops polling)   [B]
        │   ├── SHIFT LIGHT          (polls RPM, speed every second; top: 10 round LEDs,
        │   │   │                     green / amber / red, filling from 60 % of the shift
        │   │   │                     point and flashing red at it with SHIFT!, RPM in LCD digits)
        │   │   ├── [-] [+]          shift point by 100 rpm (shown between them, saved to
        │   │   │                     config.ini); refused while moving
        │   │   └── [B Back]         → TRACK (stops polling)                      [B]
        │   └── TIME ATTACK          (polls speed alone; top: each run's best and last time)
        │       ├── [0-60 mph] [60-0 mph] [1/4 Mile]   (0-100 / 100-0 in metric) → the run
        │       │   │                (top: the run, its time in LCD digits with NEW RECORD!,
        │       │   │                 countdown / GO! / what to do, speed, best and last)
        │       │   ├── [Stop | Start] Start arms the timer (refused while moving: "Start a
        │       │   │                 run while parked."); it starts and re-arms by itself;
        │       │   │                 Stop disarms (the chosen half is red)
        │       │   └── [B Back]     → the run list (stops timing)                [B]
        │       └── [B Back]         → TRACK (stops polling)                      [B]
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
        │       ├── [Edit PIDs]      (Custom only) → Custom PIDs (top: the Custom view's values)
        │       │   ├── [PID rows]   the PIDs the car supports, each ✓ On (green) or Off; a tap
        │       │   │                turns it on or off and the view polls the new set (up to 20;
        │       │   │                scrolls: drag or the grip); "n of 20 picked" beside Back
        │       │   └── [B Back]     → Custom view, saves the choice              [B]
        │       └── [B Back]         → view list and the Dashboard (ends a recording, saves
        │                            the view)                                    [B]
        │
        ├── TRIM                     (top: a panel per bank, BANK n up its left edge, with
        │   │                        Long and Short center-zero bars, -25 to +25 %, in the
        │   │                        zone colours (an end arrow past the range) and their
        │   │                        values; then rpm, load and the engine state. Polls while
        │   │                        open)
        │   ├── (bottom: poll rate, the hints (hints, not diagnoses), REC m:ss while recording)
        │   ├── [Stop | Record]      LIVE's toggle: records the trims (with totals, RPM, MAP /
        │   │                        MAF, load) to drives/; Stop ends the recording
        │   ├── [B Back]             → HOME, stops polling                        [B]
        │   └── [Y Info]             → Info: what trims mean, the colour zones, how to check
        │       │                    them (the top keeps updating)                [Y]
        │       └── [B Back]         → TRIM's controls                            [B]
        │                            (if graphics fail to start, TRIM falls back to the text
        │                            layout: bars, BACK, REC, START / STOP)
        │
        ├── DIAG                     (top: car, CEL, and what each button holds: stored,
        │   │                        pending and permanent codes, readiness; the protocol;
        │   │                        scan progress under the panel)
        │   ├── [Stored]             → Stored codes page (top: count, modules, freeze frame)
        │   │   ├── [code rows]      scroll (drag, or the scroll bar's grip); tap → the code
        │   │   │                    on top: its text, module and freeze frame (red outline)
        │   │   ├── [B Back]         → DIAG                                       [B]
        │   │   └── [X Clear]        → Clear codes question (needs a scan first)  [X]
        │   │       ├── (top: what clearing does: CEL off, codes and freeze frame
        │   │       │    erased, readiness reset, the fault not fixed)
        │   │       ├── [Cancel]     → back to the page                           [B]
        │   │       └── [Clear Codes] clears (refused if moving), then rescans
        │   ├── [Pending]            → Pending codes page (same as Stored, no freeze frame)
        │   ├── [Permanent]          → Permanent codes page (same; "Not supported" before 2010)
        │   ├── [Readiness]          → Readiness page (top: ready n of m, not ready, likely to
        │   │   │                    pass inspection)
        │   │   ├── [monitor rows]   ✓ Complete (green) / Not ready (amber); scroll
        │   │   └── [B Back]         → DIAG                                       [B]
        │   ├── [B Back]             → HOME                                       [B]
        │   └── [X Scan]             reads CEL, readiness, codes, the freeze frame (with stored
        │                            codes), VIN (and model)                        [X]
        │                            (if graphics fail to start, DIAG falls back to the text
        │                            layout: STORED / PENDING / PERM / READY tabs, paged
        │                            rows, BACK < > TRIM CLEAR SCAN)
        │
        ├── LOG                      (top: how many drives, the newest, the space used)
        │   ├── [drive rows]         newest first: date and time, car, length (scrolls: drag
        │   │   │                    or the grip); tap → the drive's page
        │   │   ├── (top: its details: car, VIN, protocol, length, size, the values)
        │   │   ├── [Graph]          → GRAPH
        │   │   │   ├── (top: the status bar, the value as the title, then a panel with the
        │   │   │   │    drive and one value against time: axes, times, a smooth blue line
        │   │   │   │    through the readings over a faint min / max band; Min / Max /
        │   │   │   │    Average under it)
        │   │   │   ├── [<<] [>>]    earlier / later (half a window)
        │   │   │   ├── [Zoom -] [Zoom +] more / less of the drive (5 s at the least)
        │   │   │   ├── (< >)        half circles: the drive's other values         [L/R]
        │   │   │   ├── [Whole Drive] the whole drive
        │   │   │   └── [B Back]     → the drive's page                           [B]
        │   │   ├── [Delete]         → Delete question (top: what it removes; can't be undone)
        │   │   │   ├── [Cancel]     → the drive's page                           [B]
        │   │   │   └── [Delete]     (red) removes the file from the SD card → the list
        │   │   └── [B Back]         → the list                                   [B]
        │   ├── [Y Info]             how to record a drive (REC on LIVE), what LOG does  [Y]
        │   └── [B Back]             → HOME                                       [B]
        │
        ├── SETUP                    (top: the settings at a glance: adapter, Wi-Fi slot, car,
        │   │                        engine, units, version)
        │   ├── [Wi-Fi]              → SETUP > WI-FI (rows: label, value, > when it opens)
        │   │   ├── (top: Check & Connect's progress, then "Connected." with the adapter and
        │   │   │    car, or "Failed at: <step>" and what to fix; before a check, a hint)
        │   │   ├── [Check & Connect  Connected/Not connected/checking...]  Wi-Fi (joins the
        │   │   │                    slot if needed), reaching the adapter, the adapter
        │   │   │                    answering, the car
        │   │   ├── [Wi-Fi slot  Any/1/2/3]  cycles the slot, saved
        │   │   ├── [Connection Guide >]     → Wi-Fi guide
        │   │   └── [B Back]         → SETUP                                      [B]
        │   ├── [Vehicle]            → SETUP > VEHICLE (VIN values win, marked "(VIN)"; top: make,
        │   │   │                    model, year, engine, VIN)
        │   │   ├── [Make  Auto/<make> >]    → Car make picker (a scrolling list, the chosen one
        │   │   │                    red; a tap saves it → VEHICLE; top: the make now) [B back]
        │   │   ├── [Model  Auto/<model> >]  → 3DS keyboard; empty = Auto; saved
        │   │   ├── [Year  Auto/<year>] [-] [+]   steps Auto, 1996 ... next year; saved
        │   │   ├── [Engine  Unknown/<n.n> L] [-] [+]  0.6-8.0 L by 0.1 (from Unknown: 2.0); saved;
        │   │   │                    for ECO's fuel estimate on cars without a MAF
        │   │   ├── [VIN  <VIN>/tap to read >]  runs a DIAG scan
        │   │   └── [B Back]         → SETUP                                      [B]
        │   ├── [Other]              → SETUP > OTHER (top: units, version)
        │   │   ├── [US | Metric]    the units toggle, saved (LIVE and the log use it)
        │   │   ├── [About Gauge Boy >]  → About (top: name, version, what it does, unofficial,
        │   │   │   │                the repo; bottom: sources, CREDITS.md, the safety line)
        │   │   │   └── [B Back]     → SETUP > OTHER                              [B]
        │   │   └── [B Back]         → SETUP                                      [B]
        │   ├── [Tests]              → SETUP > TESTS (top: the session log)   [L/R, Up/Down scroll]
        │   │   ├── [ATZ] [ATSP0 + 0100] [ATDPN] [ATRV] [0902 VIN] [Speed Test]
        │   │   ├── [Live Test]      every supported PID once, then 10 s of the Engine view
        │   │   ├── [Save Log]       → logs/gauge-boy-log-<date>.txt
        │   │   └── [B Back]         → SETUP                                      [B]
        │   └── [B Back]             → HOME                                       [B]
        │
        └── Wi-Fi guide              (top: "Step n of 5: <title>" and the step's text)
            ├── (< step >)           half circles: the other steps                 [L/R]
            ├── (the last check's first line, or its progress)
            ├── [Check & Connect]    the same check → results here and on SETUP > WI-FI
            └── [B Back]             → SETUP > WI-FI, or HOME if opened from the Adapter card  [B]
```

Any screen: a new achievement shows "Unlocked: <name>!" (or "Unlocked n achievements!") in the bottom screen's notice line for 5 s.

## Notes

- SETUP is a menu of four pages (owner layout, gauge-boy-cib): TESTS (adapter and car tests, the log's buttons), WI-FI (connection), VEHICLE (make, model, year, engine size, VIN) and OTHER (units, About).
- LIVE views and the PIDs in each, in list order: Engine (RPM, speed, coolant, throttle, load), Idle/Vacuum (RPM, MAP (PID 0B or 87) + vacuum or boost, short-term trims, timing, baro, fuel loop), Air/Fuel (MAF, MAP (0B or 87), intake air, throttle, short- and long-term trims, baro, fuel loop, commanded lambda), O2 Sensors (RPM, short-term trims, then every oxygen sensor the car has: narrow band voltage, PIDs 14-1B, or wide band lambda, 24-2B / 34-3B, banks 1-2, sensors 1-4), Warm-up (coolant, intake air, oil, outside air, run time, RPM), Drive (RPM, speed, throttle, load, timing, torque, gear ratio, odometer, fuel rate (the car's PID 5E / 9D, else estimated from MAF or MAP + engine size) and instant economy), All (every supported PID), Custom (your pick). Each view only shows what the car supports.
- HOME, LIVE, DIAG, TRIM, TRACK and LOG (with GRAPH) are drawn with graphics (citro2d, the UI kit in `source/ui/kit.c`); SETUP, the Wi-Fi guide, GARAGE (with its camera), TRIP and MILEAGE are graphics too; LIVE's Custom PID picker too: every screen is graphics now. If graphics fail to start, every screen falls back to its text layout.
- The code for these screens is in `source/spike_ui.c` (screens, buttons, actions) and `source/ui/` (HOME layout and drawing).
