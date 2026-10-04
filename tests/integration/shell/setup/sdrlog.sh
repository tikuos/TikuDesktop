# Set up before sdrlog.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s, $tmp.

# TikuSDR joins this lane's applications with its manifest, so Tracker
# knows the type it opens; no board is needed to play a recording.
cp -R "$top/build/runtime/apps/sdr" "$appsdir/"

# A recording of three seconds of Live at 2437 MHz: a narrow signal walking
# up through the band over a floor with a little texture.
mkdir -p "$deskhome/recordings"
{
    printf '# TikuSDR recording 1\n# board: c61 (esp32c61)\n'
    printf '# began: 2026-10-04 09:30:00 +0800\n'
    printf '0.000 VIEW live 2437\n'
    i=0
    while [ $i -lt 30 ]; do
        printf '%d.%03d SPEC 2437 40000000 76 256 ' $((i / 10)) \
            $(((i % 10) * 100))
        awk -v p=$((40 + i * 6)) 'BEGIN {
            for (j = 0; j < 256; j++) {
                v = 70 + (j * 7) % 11
                if (j >= p - 2 && j <= p + 2) v = 190
                printf "%02x", v
            }
            printf "\n"
        }'
        i=$((i + 1))
    done
} > "$deskhome/recordings/morning.sdrlog"
