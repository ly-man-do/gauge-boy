# GaugeBoy screen and button map

Every screen and button in the app as it is now. Kept up to date with each UI change (`CLAUDE.md` section 5).

**As of:** 2026-11-16, DIAG shows the model from the VIN (gauge-boy-1hi; M2 in progress)

The arrow (→) shows where a button goes or what it does. On the bottom screen everything is reached by touch; the physical buttons in brackets are optional shortcuts.

```
GaugeBoy
├── Unsupported device screen        (Old 3DS only: tap or START exits)
└── Safety notice                    (tap the touch screen to continue)
    └── HOME                         (graphics; top screen: car facts)
        ├── [CEL card]               → DIAG        (shows CEL off / CEL ON / CEL not read)
        ├── [Adapter card]           → Wi-Fi guide (shows Connected / Not connected)
        ├── [LIVE  · Dash gauges]    → LIVE view list
        ├── [DIAG  · Codes & CEL]    → DIAG
        ├── [TRIM  · Fuel trims]     → TRIM (starts polling)
        ├── [FUN   · Fun modes]      → notice "comes in version 0.6"
        ├── [LOG   · Record drives]  → notice "comes in version 0.4"
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
        │       ├── [START / STOP]   pauses or resumes polling
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
        ├── SETUP                    (top: session log)                     [L/R scroll log]
        │   ├── [Wi-Fi slot   Any/1/2/3]          cycles the slot, saved
        │   ├── [Check the slot's IP and DNS]     → Slot check screen
        │   │   └── [OK]             → SETUP
        │   ├── [Car make     Auto/<make>]        → Car make picker
        │   │   ├── [Auto] [Acura] [Audi] …       12 per page, 3 pages; tap saves → SETUP
        │   │   ├── [BACK]           → SETUP                                      [B]
        │   │   └── [<] page n/3 [>]                                              [L/R]
        │   ├── [Save this session's log]         → logs/gaugeboy-log-<date>.txt
        │   ├── [BACK]               → HOME                                       [B]
        │   ├── [ADAPTER]            → Adapter test
        │   │   ├── [CONNECT] [ATZ] [ATSP0+0100]
        │   │   ├── [ATDPN]   [ATRV] [0902 VIN]
        │   │   ├── [SLOT ANY/n] [SPEED TEST] [BACK → SETUP]
        │   │   └── [OLDER ^] [SAVE LOG] [NEWER v]   (scroll / save the log) [Up/Down, L/R]
        │   └── [WI-FI GUIDE]        → Wi-Fi guide
        │
        └── Wi-Fi guide              (top: step text; 5 steps)
            ├── [BACK]               → SETUP, or HOME if opened from the Adapter card  [B]
            ├── [<] step n/5 [>]                                                  [L/R]
            └── [CHECK]              slot, IP/DNS, Wi-Fi, adapter → results on bottom
```

## Notes

- The adapter test screen and Car make are both under SETUP. SETUP's bottom row is BACK, ADAPTER and WI-FI GUIDE.
- **SLOT** on the adapter test screen does the same thing as SETUP's **Wi-Fi slot** row.
- LIVE views and the PIDs in each: Engine (RPM, speed, coolant, throttle, load), Idle & vacuum (RPM, MAP (PID 0B or 87) + vacuum or boost, short-term trims, timing, baro), Air & fuel (MAF, MAP (0B or 87), intake air, throttle, short- and long-term trims, baro), Warm-up (coolant, intake air, oil, outside air, run time, RPM), Drive (RPM, speed, throttle, load, timing), All (every supported PID), Custom (your pick). Each view only shows what the car supports.
- Only HOME is drawn with graphics (citro2d). Every other screen is still the text console until M3.
- The code for these screens is in `source/spike_ui.c` (screens, buttons, actions) and `source/ui/` (HOME layout and drawing).
