#!/bin/sh
# Run the emulator check against every ELM327 emulator scenario (dev only).
#
#   tools/check-scenarios.sh            all scenarios
#   tools/check-scenarios.sh tracker    just one
#
# Each scenario starts headless on its own port, is checked with
# tests/build/emulator_check (driver, DIAG scan, adapter test actions) and
# stopped. Exits non-zero if any check fails. Not part of `make test` / CI:
# it needs tools/.venv (created on first run) and takes about a minute.
set -e
cd "$(dirname "$0")/.."

VENV=tools/.venv
if [ ! -x "$VENV/bin/elm" ]; then
    echo "Setting up $VENV (first run) ..."
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install -q -r tools/requirements-dev.txt
fi
make --no-print-directory -C tests build/emulator_check >/dev/null

# scenario:expected ATDPN protocol
ALL="car:6 ridgeline:3 tracker:3 j1850:2 ascent:6"
WANT="${1:-}"
PORT=35100
FAILED=""
LOGDIR="${TMPDIR:-/tmp}"

for entry in $ALL; do
    name="${entry%%:*}"
    proto="${entry##*:}"
    [ -n "$WANT" ] && [ "$WANT" != "$name" ] && continue
    PORT=$((PORT + 1))
    log="$LOGDIR/gaugeboy-emu-$name.log"
    echo "=== $name (protocol $proto) on 127.0.0.1:$PORT ==="
    # The emulator stops at end of input, so hold its input open with a fifo.
    fifo="$LOGDIR/gaugeboy-emu-$name.in"
    rm -f "$fifo"
    mkfifo "$fifo"
    "$VENV/bin/python" -I tools/elm_scenarios.py -n "$PORT" -i 127.0.0.1 -s "$name" \
        <"$fifo" >"$log" 2>&1 &
    emu=$!
    exec 3>"$fifo"
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        nc -z 127.0.0.1 "$PORT" 2>/dev/null && break
        sleep 1
    done
    if ./tests/build/emulator_check 127.0.0.1 "$PORT" "$proto"; then :; else FAILED="$FAILED $name"; fi
    kill "$emu" 2>/dev/null || true
    wait "$emu" 2>/dev/null || true
    exec 3>&-
    rm -f "$fifo"
    echo
done

if [ -n "$FAILED" ]; then
    echo "FAILED:$FAILED"
    exit 1
fi
echo "All scenarios passed."
