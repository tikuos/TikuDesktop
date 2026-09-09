#!/bin/sh
# The supervisor: the program still standing when the desk is not.
#
# Kills a desk from outside -- no signal handler inside it can record a
# SIGKILL -- and asks that the supervisor wrote down how it ended, started
# it once more told why, and that the desk then said so on its screen.
# Then that a desk which keeps dying is left down, that a quiet quit
# leaves no report, and that an end asked of the supervisor is not
# mistaken for a mishap.

top=$(cd "$(dirname "$0")/../.." && pwd)
sup="$top/build/tiku-desk"
script="$top/TikuTracker/tests/supervisor.script"
pass=0; fail=0

check() {
    if [ "$1" -eq 0 ]; then pass=$((pass + 1)); echo "  [PASS] $2"
    else fail=$((fail + 1)); echo "  [FAIL] $2"; fi
}

fresh() {
    H=/tmp/trk_sup_home; rm -rf "$H"; mkdir -p "$H/.config"
    R="$H/reports"
    export HOME="$H" TIKU_TRK_REPORTS="$R" DISPLAY=
    export TIKU_DESK="$top/build/dev/TikuDesktop"
}

# The desk the supervisor started, found as its child; waited for with
# a bound, because a supervisor that never starts one is what is being
# tested for too.
desk_pid() {
    i=0
    while [ $i -lt 100 ]; do
        p=$(pgrep -P "$1" -x TikuDesktop 2>/dev/null | head -1)
        [ -n "$p" ] && { echo "$p"; return; }
        sleep 0.1; i=$((i + 1))
    done
}

echo "the supervisor"

# 1. Killed once: written down, started again, and said on the screen.
fresh
"$sup" -script "$script" > /tmp/trk_sup.out 2>&1 &
S=$!
first=$(desk_pid $S)
check $([ -n "$first" ]; echo $?) "the desk is started"
sleep 1
kill -KILL "$first"
second=""
i=0
while [ $i -lt 100 ] && [ -z "$second" ]; do
    p=$(pgrep -P $S -x TikuDesktop 2>/dev/null | head -1)
    [ -n "$p" ] && [ "$p" != "$first" ] && second=$p
    sleep 0.1; i=$((i + 1))
done
check $([ -n "$second" ]; echo $?) "killed from outside, it is started again"
if [ -n "$second" ] && tr '\0' ' ' < /proc/$second/cmdline | grep -q -- "-after $R/exit-"; then
    check 0 "and told where the account of the first one is"
else
    check 1 "and told where the account of the first one is"
fi
wait $S; rc=$?
check $([ $rc -eq 0 ]; echo $?) "the second run ended well and so did the supervisor (rc=$rc)"
f=$(ls "$R"/exit-*.txt 2>/dev/null | head -1)
check $([ -n "$f" ]; echo $?) "an exit report was written"
check $(grep -q "signal 9" "$f" 2>/dev/null; echo $?) "naming the signal"
check $(grep -q "the last it printed" "$f" 2>/dev/null; echo $?) "with what the desk last printed"
check $([ -f "$R/desk.log" ]; echo $?) "the desk's stderr was kept"
check $([ -L "$R/latest-exit.txt" ]; echo $?) "and latest-exit points at the report"
check $(grep -q "started again" /tmp/trk_sup.out; echo $?) "the restarted desk SAID so on its screen"
check $([ "$(ls "$R"/exit-*.txt | wc -l)" -eq 1 ]; echo $?) "one report: a good end writes nothing"

# 2. Killed twice: left down, with the reason, not started a third time.
fresh
"$sup" -script "$script" > /tmp/trk_sup2.out 2>&1 &
S=$!
first=$(desk_pid $S); sleep 1; kill -KILL "$first"
second=""; i=0
while [ $i -lt 100 ] && [ -z "$second" ]; do
    p=$(pgrep -P $S -x TikuDesktop 2>/dev/null | head -1)
    [ -n "$p" ] && [ "$p" != "$first" ] && second=$p
    sleep 0.1; i=$((i + 1))
done
sleep 1; [ -n "$second" ] && kill -KILL "$second"
wait $S; rc=$?
check $([ $rc -eq 1 ]; echo $?) "a desk that dies twice is left down (rc=$rc)"
check $(grep -q "not started a third time" /tmp/trk_sup2.out; echo $?) "and the reason is on the supervisor's stderr"
check $([ "$(ls "$R"/exit-*.txt | wc -l)" -eq 2 ]; echo $?) "with a report for each end"
pgrep -x TikuDesktop -P $S > /dev/null; check $([ $? -ne 0 ]; echo $?) "leaves no desk behind"

# 3. Ending the supervisor ends the desk, and that is not a mishap.
fresh
"$sup" -script "$script" > /tmp/trk_sup3.out 2>&1 &
S=$!
first=$(desk_pid $S); sleep 1
kill -TERM $S
wait $S; rc=$?
check $([ $rc -eq 0 ]; echo $?) "an end asked of the supervisor is a good end (rc=$rc)"
check $([ -z "$(ls "$R"/exit-*.txt 2>/dev/null)" ]; echo $?) "and writes no report"
check $(kill -0 "$first" 2>/dev/null; [ $? -ne 0 ]; echo $?) "and the desk went with it"

echo "supervisor: $pass passed, $fail failed"
[ $fail -eq 0 ]
