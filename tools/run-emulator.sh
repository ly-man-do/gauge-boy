#!/bin/sh
# Start the ELM327 emulator (dev only) for Gauge Boy testing.
#
#   tools/run-emulator.sh          listen on 127.0.0.1:35000 (this computer only)
#   tools/run-emulator.sh --lan    listen on all interfaces, so a real 3DS on the
#                                  same Wi-Fi can connect to this computer's IP
#
# Environment: ELM_PORT (default 35000), ELM_SCENARIO (default ridgeline).
# Scenarios: ridgeline, tracker (ISO 9141-2), j1850 (J1850 VPW, codes set),
# ascent (CAN, turbo: PID 87 boost), track (ridgeline with a speed loop for
# Time Attack)
# from tools/elm_scenarios.py; car, default (CAN) from the emulator package.
# Extra arguments are passed to the emulator. Type "help" at its CMD> prompt
# for commands; Ctrl-D or "quit" stops it.
set -e
cd "$(dirname "$0")"

VENV=.venv
if [ ! -x "$VENV/bin/elm" ]; then
    echo "Setting up $VENV (first run) ..."
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install -q -r requirements-dev.txt
fi

IFACE=127.0.0.1
if [ "${1:-}" = "--lan" ]; then
    IFACE=0.0.0.0
    shift
fi

PORT="${ELM_PORT:-35000}"
SCENARIO="${ELM_SCENARIO:-ridgeline}"
echo "ELM327 emulator: $IFACE:$PORT, scenario '$SCENARIO'"
exec "$VENV/bin/python" -I elm_scenarios.py -n "$PORT" -i "$IFACE" -s "$SCENARIO" "$@"
