# GaugeBoy screen and button map

Every screen and button in the app as it is now. Kept up to date with each UI change (`CLAUDE.md` section 5).

**As of:** 2026-11-16, 0.1.0+2d4e05a (M2 in progress)

The arrow (→) shows where a button goes or what it does. On the bottom screen everything is reached by touch; the physical buttons in brackets are optional shortcuts.

```
GaugeBoy
├── Unsupported device screen        (Old 3DS only: tap or START exits)
└── Safety notice                    (tap the touch screen to continue)
    └── HOME                         (graphics; top screen: car facts)
        ├── [CEL card]               → DIAG        (shows CEL off / CEL ON / CEL not read)
        ├── [Adapter card]           → Wi-Fi guide (shows Connected / Not connected)
        ├── [LIVE  · Dash gauges]    → LIVE (starts polling)
        ├── [DIAG  · Codes & CEL]    → DIAG
        ├── [TRIM  · Fuel trims]     → notice "comes in version 0.2"
        ├── [FUN   · Fun modes]      → notice "comes in version 0.6"
        ├── [LOG   · Record drives]  → notice "comes in version 0.4"
        ├── [SETUP · Settings]       → SETUP
        └── [START Close bar]        → closes GaugeBoy            [START]
        │
        ├── LIVE                     (top: values, refresh rates, battery, vacuum)
        │   ├── [PID toggles]        6 per page, green = polled (Load, Coolant,
        │   │                        STFT/LTFT B1-B2, MAP, RPM, Speed, Timing,
        │   │                        Intake air, Throttle, MAF … as the car supports)
        │   ├── [BACK]               → HOME, stops polling, saves the PID choice  [B]
        │   ├── [<] page n/m [>]                                                  [L/R]
        │   └── [START / STOP]       pauses or resumes polling
        │
        ├── DIAG                     (top: car facts or the selected code's text)
        │   ├── [STORED] [PENDING] [PERM] [READY]   tabs (PERM hidden if unsupported)
        │   ├── [list rows]          3 per page, tap → full description on top
        │   ├── [BACK]               → HOME                                       [B]
        │   ├── [<] page n/m [>]                                                  [L/R]
        │   ├── [CLEAR]              → Clear codes dialog (needs a scan first)
        │   │   ├── [CANCEL]         → DIAG                                       [B]
        │   │   └── [CLEAR CODES]    clears (refused if moving), then rescans
        │   └── [SCAN]               reads CEL, readiness, codes, VIN             [X]
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
- Only HOME is drawn with graphics (citro2d). Every other screen is still the text console until M3.
- The code for these screens is in `source/spike_ui.c` (screens, buttons, actions) and `source/ui/` (HOME layout and drawing).
