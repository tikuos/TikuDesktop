/* tiku_bench_device.c -- the board list, the chosen board's MCU and port,
 * and the test profile they give.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_bench_private.h"

/** The catalogue's record for the chosen board's MCU, or NULL when the
 *  catalogue has none. */
const board_t *tiku_bench_board_info(const bench_t *st)
{
    for (int i = 0; i < st->nboard; i++)
        if (!strcmp(st->boards[i].key, st->mcu)) return &st->boards[i];
    return NULL;
}

/** Derive the test profile from the MCU, with -usb or -uart for a board
 *  that has both consoles; turn off options the board lacks, and rebuild
 *  the suite and Did not run lists. */
void tiku_bench_profile_update(bench_t *st)
{
    const board_t *b = tiku_bench_board_info(st);
    st->supported = b != NULL;
    if (!b || !b->large) st->large = 0;
    if (!b || !b->usb) st->usb = 0;
    if (b && (!strcmp(b->key, "esp32c5") || !strcmp(b->key, "esp32p4"))) st->usb = 1;
    if (b && !strcmp(b->key, "esp32p4")) st->flash = 0;
    snprintf(st->profile, sizeof st->profile, "%.55s%s", st->mcu,
             b && b->usb ? (st->usb ? "-usb" : "-uart") : "");
    tiku_bench_entries_build(st);
    tiku_bench_notrun_build(st);
}

/** Read the chosen board's MCU and port, which the desktop answers from
 *  its record of the mounted board, and update the profile. */
void tiku_bench_target_capture(bench_t *st)
{
    st->mcu[0] = st->port[0] = '\0';
    if (st->services->board_mcu)
        st->services->board_mcu(st->services->ctx, st->target_name, st->mcu, sizeof st->mcu);
    if (st->services->board_port)
        st->services->board_port(st->services->ctx, st->target_name, st->port, sizeof st->port);
    for (char *p = st->mcu; *p; p++) *p = (char)tolower((unsigned char)*p);
    if (!strcmp(st->mcu, "esp32-c5")) snprintf(st->mcu, sizeof st->mcu, "esp32c5");
    if (!strcmp(st->mcu, "esp32-c61")) snprintf(st->mcu, sizeof st->mcu, "esp32c61");
    if (!strcmp(st->mcu, "esp32-p4 rev 1.3")) snprintf(st->mcu, sizeof st->mcu, "esp32p4");
    tiku_bench_profile_update(st);
}

/** Read the mounted boards into the device dropdown and choose one: the
 *  board already chosen, else the remembered board, else the desktop's
 *  default device.  An unchanged list is not rebuilt. */
void tiku_bench_targets_refresh(bench_t *st)
{
    char names[1024] = "", chosen[64] = "";
    if (st->services->board_names)
        st->services->board_names(st->services->ctx, names, sizeof names);
    /* An unchanged list, the empty one included, leaves the window
     * alone: no repaint and no menus are published. */
    if (!strcmp(names, st->mounted) && st->refreshed) return;
    st->refreshed = 1;
    snprintf(st->mounted, sizeof st->mounted, "%s", names);
    tiku_choice_init(&st->target);
    char *save = NULL;
    for (char *p = strtok_r(names, "\n", &save); p; p = strtok_r(NULL, "\n", &save))
        tiku_choice_add(&st->target, p, 1);
    snprintf(chosen, sizeof chosen, "%s", st->target_name);
    /* With no board chosen, the remembered board is chosen once it is
     * mounted, and is then forgotten; until then the desktop's default
     * device is chosen. */
    for (int i = 0; !*chosen && *st->wanted && i < st->target.count; i++)
        if (!strcmp(st->target.item[i].label, st->wanted)) {
            snprintf(chosen, sizeof chosen, "%s", st->wanted);
            st->wanted[0] = '\0';
        }
    if (!*chosen && st->services->current_device)
        st->services->current_device(st->services->ctx, chosen, sizeof chosen);
    int selected = -1;
    for (int i = 0; i < st->target.count; i++)
        if (!strcmp(st->target.item[i].label, chosen)) selected = i;
    /* A chosen board that is no longer mounted leaves no board chosen;
     * the first board is taken only when none was chosen. */
    if (selected < 0 && !*chosen && st->target.count) selected = 0;
    st->target.selected = selected;
    st->target_name[0] = '\0';
    if (selected >= 0)
        snprintf(st->target_name, sizeof st->target_name, "%s", st->target.item[selected].label);
    tiku_bench_target_capture(st); st->dirty = 1;
}
