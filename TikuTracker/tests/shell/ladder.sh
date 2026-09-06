#!/bin/sh
# The portability proof's hardware half: each board reaches a desk over its
# best link.  With the two Nordic DKs on the bench it is a short ladder --
# a board over the console link, a board over IP, the two at once, and (on a
# host adapter that hears) a board over BLE.  Each rung flashes the image it
# needs and runs a lane run.sh already has, so the counts match the demos on
# the board.
#
#   OS=../..  NRF1=<lm20 serial>  P1=/dev/ttyACM0 \
#   NRF2=<l15 serial>  P2=/dev/ttyACM3 \
#   MCU1=nrf54lm20a MCU2=nrf54l15 tests/shell/ladder.sh
#
# The BLE rung waits on a host adapter that reports what it hears; set
# TRK_AIR=1 to run it.
set -u
here=$(dirname "$0")
os=${OS:-$here/../../..}
: "${NRF1:?the first board's probe serial}"; : "${P1:?its port}"
: "${NRF2:?the second board's probe serial}"; : "${P2:?its port}"
mcu1=${MCU1:-nrf54lm20a}; mcu2=${MCU2:-nrf54l15}
log=/tmp/trk_ladder.log; rung=/tmp/trk_ladder_rung.log; : > "$log"

flash() { # $1 mcu $2 serial $3.. make vars
    m=$1; sn=$2; shift 2
    ( cd "$os" && rm -f main.elf && \
      make MCU="$m" TIKU_SHELL_ENABLE=1 HAS_TESTS=0 HAS_EXAMPLES=0 \
           NRF_SN="$sn" "$@" flash ) >> "$log" 2>&1 && ( cd "$os" && rm -f main.elf )
}
run() { # $1 label $2 script $3 TRK_PORT $4 TRK_PORT2
    label=$1; script=$2; p1=$3; p2=$4
    (
        TRK_SHELL_ONLY="$script"; TRK_PORT="$p1"; export TRK_SHELL_ONLY TRK_PORT
        [ -n "$p2" ] && { TRK_PORT2="$p2"; export TRK_PORT2; }
        sh "$here/run.sh"
    ) > "$rung" 2>&1
    if ! grep -q "\[FAIL\]" "$rung" && grep -q "\[PASS\]" "$rung"; then
        printf '  %-38s %s\n' "$label" "held"
    else
        printf '  %-38s %s\n' "$label" "FAILED"
        grep -A 2 "\[FAIL\]" "$rung" | sed 's/^/      /'
    fi
    printf '== %s\n' "$label" >> "$log"; cat "$rung" >> "$log"
}

echo "the ladder (log: $log):"

# The forms + drawing demos on board 1: a form and a window; its console link
# carries them to a mount on the desk.
flash "$mcu1" "$NRF1" TIKU_APPL_GUI=1 TIKU_APPL_GUI_DEMO=1 TIKU_APPL_DRAW_DEMO=1
run "board 1, console link"            boarddraw.script "$P1" ""

# The drawing demo + the IP link on board 2: it dials the desk and its
# window rides the connection.
flash "$mcu2" "$NRF2" \
      TIKU_KIT_NET_ENABLE=1 TIKU_KITS_NET_LINK_IP_ENABLE=1 \
      TIKU_APPL_GUI=1 TIKU_APPL_GUI_IP=1 TIKU_APPL_DRAW_DEMO=1
run "board 2, IP link (dials in)"      boardnet.script "$P2" ""

# Both at once: board 1 on its cable (drawing demo only now, one window) and
# board 2 still dialling -- two boards, two media, one desk.
flash "$mcu1" "$NRF1" TIKU_APPL_GUI=1 TIKU_APPL_DRAW_DEMO=1
run "both boards, two media at once"   boardboth.script "$P1" "$P2"

# Board 1 over BLE: the drawing demo + the BLE link, the host as the central.
if [ -n "${TRK_AIR:-}" ]; then
    flash "$mcu1" "$NRF1" TIKU_FLPR_ENABLE=1 TIKU_LINK_BLE_ENABLE=1 \
          TIKU_APPL_GUI=1 TIKU_APPL_GUI_BLE=1 TIKU_APPL_DRAW_DEMO=1
    run "board 1, BLE link (host central)" boardble.script "$P1" ""
else
    printf '  %-38s %s\n' "board 1, BLE link (host central)" \
        "waits on a host adapter (TRK_AIR=1 to run)"
fi
