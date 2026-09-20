# The script language

What a `.script` file may say to a shell started with `-script`, one
instruction per line, `#` to the end of a line a comment.  The engine is
`src/shell/script/`; the same vocabulary is spoken by `trk-conduct`, which
drives a shipping shell from outside.  The harness that runs them is
`tests/shell/run.sh`, whose `# header:` lines are documented at its top.

```
    key <keysym> [ctrl] [shift] [alt]   -- e.g. `key n ctrl`
    click <x> <y> [button]              -- press and release together
    click-row <n> [button]               -- click a visible list row
    click-cell <row> <column title>      -- click a cell, not a pixel
    click-window <title>                 -- bring a window to the front by
                                            clicking its tab
    write <path> <text>                  -- change a file behind the shell
                                            (folders made as needed)
    draw-reset                           -- forget how much was painted
    expect-drawn <n>                     -- rows painted since that reset
    expect-ink <x> <y>                   -- something is drawn here
    expect-icon-screen <name> <x> <y>    -- an icon's place on the SCREEN,
                                            which is what a drop point is
    press-row/move-row/release-row <n> [button] [mods]
                                         -- one half of a gesture, aimed
                                            at a row rather than a pixel
    press <x> <y> [button]
    release <x> <y> [button]
    wait <turns>                        -- let the loop run
    shot <path>
    expect-status <substring>
    expect-file <path>
    expect-no-file <path>
    expect-rows-at-least <n>
    remove <path>                       -- change the world behind it
    expect-perm <path> <rwx>            -- the owner bits, e.g. "rw-"
    expect-windows <n>
    expect-selected <n>
    expect-selected-name <name>
    expect-renaming <0|1>
    expect-find-text <text>             -- the Find panel's own field
    expect-find-type <label>
    expect-location <path>
    expect-nav-back <0|1>
    expect-nav-forward <0|1>
    expect-nav-up <0|1>
    expect-navigator <0|1>
    expect-visible-rows <n>
    expect-type-narrowed                -- every row is of the asked type
    expect-window-at <x> <y>            -- the focused window's corner
    expect-columns <n>
    expect-deskbar-at <x> <y>
    expect-title <substring>
    expect-menu <substring>
    expect-narrate <substring>          -- the focused window, said from
                                           its own display list
    expect-no-narrate <substring>       -- and what it does not say, which
                                           is what proves it moved
    expect-sound <event>                -- the moment sounded (counted; a
                                           scripted run is mute)
    expect-no-sound <event>
    expect-sound-on <event>             -- the moment is enabled in the
                                           Sounds preferences
    expect-sound-off <event>
    expect-volume <n>                   -- the volume in force, 0..100
    expect-no-menu
    expect-read <noun> <substring>      -- what the shell says of a noun
                                           (title, titles, status, menu,
                                           hot, tip, strip, minimized,
                                           menu-bottom, mounts, alert,
                                           operation, turns, a /path)
    expect-said <noun> [text]           -- the whole answer, so "nothing"
                                           is a check too
    expect-under <noun> <n>             -- a numeric noun's bound
    expect-over <noun> <n>
    note <text>                         -- what the next check is for
    quit
```
