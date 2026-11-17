#!/usr/bin/env bash
#
# Runs the 10 test scenarios required by the assignment and prints, for each
# one, the exact command, everything procmon printed (stdout + stderr) and its
# exit code.
#
# Usage (from the repository root, as a normal non-root user):
#   ./tests/run_scenarios.sh                    # print to the terminal
#   ./tests/run_scenarios.sh | tee tests/results.txt
#
# Set VALGRIND=1 to also run procmon under valgrind in every scenario.

cd "$(dirname "$0")/.." || exit 1
make -s || exit 1

# stdbuf -oL: line-buffer stdout so stdout and stderr lines appear in the real order
PROCMON="stdbuf -oL ./procmon"
if [ "${VALGRIND:-0}" = "1" ]; then
    PROCMON="stdbuf -oL valgrind -q --leak-check=full --error-exitcode=99 ./procmon"
fi

TMPDIR_T=$(mktemp -d)
BG_PIDS=()

cleanup() {
    for p in "${BG_PIDS[@]}"; do kill "$p" 2>/dev/null; done
    rm -rf "$TMPDIR_T"
}
trap cleanup EXIT

# Start a background helper process and remember it for cleanup.
# Usage: spawn sleep 60   -> PID in $LAST
spawn() {
    "$@" > /dev/null 2>&1 &
    LAST=$!
    BG_PIDS+=("$LAST")
}

header() {
    echo
    echo "=================================================================="
    echo "Scenario $1: $2"
    echo "=================================================================="
}

# Run procmon with the given arguments, show the command, output and exit code.
run() {
    echo "\$ procmon $*"
    $PROCMON "$@" 2>&1
    echo "[exit code: $?]"
}

# A PID that can never exist (above the kernel's pid_max).
NO_PID=$(( $(cat /proc/sys/kernel/pid_max) + 1 ))

# ---------------------------------------------------------------------------
header 1 "Monitor a single valid PID"
spawn sleep 60
run -p "$LAST" -i 1 -n 3

# ---------------------------------------------------------------------------
header 2 "Monitor multiple valid PIDs"
spawn sleep 60;              P1=$LAST
spawn sh -c 'while :; do :; done'; P2=$LAST    # busy loop -> ~100% CPU
run -p "$P1,$P2" -i 1 -n 3
kill "$P2" 2>/dev/null

# ---------------------------------------------------------------------------
header 3 "Provide a PID that does not exist"
run -p "$NO_PID" -i 1 -n 2
echo
echo "# Non-existent PID mixed with a valid one: the valid one is still monitored"
spawn sleep 60
run -p "$NO_PID,$LAST" -i 1 -n 2
echo
echo "# Syntactically invalid PIDs are skipped with a warning"
run -p "abc,$LAST,,0" -i 1 -n 1

# ---------------------------------------------------------------------------
header 4 "Monitor a process that terminates during monitoring"
spawn sleep 2
run -p "$LAST" -i 1 -n 5

# ---------------------------------------------------------------------------
header 5 "Provide an invalid monitoring interval"
for v in 0 -3 abc 2x 99999999999999999999; do
    run -p 1 -i "$v"
done

# ---------------------------------------------------------------------------
header 6 "Provide an invalid measurement count"
for v in 0 -1 xyz 5.5; do
    run -p 1 -n "$v"
done

# ---------------------------------------------------------------------------
header 7 "Stop the application using Ctrl+C (SIGINT)"
echo "# procmon runs without -n; SIGINT is sent after ~2.5 s (same signal as Ctrl+C)"
echo "\$ procmon -p 1 -i 1   (then SIGINT)"
$PROCMON -p 1 -i 1 > "$TMPDIR_T/sigint.out" 2>&1 &
MON=$!
sleep 2.5
kill -INT "$MON"
wait "$MON"
RC=$?
cat "$TMPDIR_T/sigint.out"
echo "[exit code: $RC]"
echo
echo "# Same with SIGTERM (what 'systemctl stop' sends)"
echo "\$ procmon -p 1 -i 1   (then SIGTERM)"
$PROCMON -p 1 -i 1 > "$TMPDIR_T/sigterm.out" 2>&1 &
MON=$!
sleep 2.5
kill -TERM "$MON"
wait "$MON"
RC=$?
cat "$TMPDIR_T/sigterm.out"
echo "[exit code: $RC]"

# ---------------------------------------------------------------------------
header 8 "Write output to a file"
OUT="$TMPDIR_T/process.log"
spawn sleep 60
run -p "1,$LAST" -i 1 -n 3 -o "$OUT"
echo "# Contents of the output file:"
cat "$OUT"

# ---------------------------------------------------------------------------
header 9 "Attempt to write to a file that cannot be accessed"
run -p 1 -n 1 -o /nonexistent_dir/process.log
if [ "$(id -u)" -ne 0 ]; then
    run -p 1 -n 1 -o /root/process.log
fi
RO="$TMPDIR_T/readonly.log"
touch "$RO" && chmod 444 "$RO"
if [ "$(id -u)" -ne 0 ]; then
    run -p 1 -n 1 -o "$RO"
fi

# ---------------------------------------------------------------------------
header 10 "Monitor multiple processes when one of them terminates"
spawn sleep 60;  LONG=$LAST
spawn sleep 2;   SHORT=$LAST
run -p "$LONG,$SHORT" -i 1 -n 5

# ---------------------------------------------------------------------------
echo
echo "=================================================================="
echo "Optional feature: CSV output (-c)"
echo "=================================================================="
spawn sleep 60
run -p "1,$LAST" -i 1 -n 2 -c
echo
echo "# A process name containing a comma is quoted (RFC 4180)"
# (the shell renames itself by writing to /proc/self/comm, then just waits)
spawn sh -c 'printf "a,b" > /proc/$$/comm; while :; do sleep 1; done'
sleep 0.3
run -p "$LAST" -n 1 -c
if command -v python3 > /dev/null; then
    echo
    echo "# Parsed back with Python's csv module (6 fields per row):"
    $PROCMON -p "1,$LAST" -n 1 -c 2>/dev/null \
        | python3 -c "import csv,sys; [print(r) for r in csv.reader(sys.stdin)]"
fi

echo
echo "=================================================================="
echo "All scenarios finished."
