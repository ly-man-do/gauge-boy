#!/bin/sh
# Start the ELM327 emulator (dev only) for GaugeBoy testing.
#
#   tools/run-emulator.sh          listen on 127.0.0.1:35000 (this computer only)
#   tools/run-emulator.sh --lan    listen on all interfaces, so a real 3DS on the
#                                  same Wi-Fi can connect to this computer's IP
#
# Environment: ELM_PORT (default 35000), ELM_SCENARIO (default car).
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
SCENARIO="${ELM_SCENARIO:-car}"
echo "ELM327 emulator: $IFACE:$PORT, scenario '$SCENARIO'"
exec "$VENV/bin/elm" -n "$PORT" -i "$IFACE" -s "$SCENARIO" "$@"
