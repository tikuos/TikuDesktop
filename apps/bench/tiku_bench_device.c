/* tiku_bench_device.c -- device.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_bench_private.h"

const board_t *tiku_bench_board_info(const bench_t *st)
{
    for (int i = 0; i < st->nboard; i++)
        if (!strcmp(st->boards[i].key, st->mcu)) return &st->boards[i];
    return NULL;
}

void tiku_bench_profile_update(bench_t *st)
{
    const board_t *b = tiku_bench_board_info(st);
    st->supported = b != NULL;
    if (!b || !b->large) st->large = 0;
    if (!b || !b->usb) st->usb = 0;
    snprintf(st->profile, sizeof st->profile, "%.55s%s", st->mcu,
             b && b->usb ? (st->usb ? "-usb" : "-uart") : "");
    tiku_bench_entries_build(st);
    tiku_bench_notrun_build(st);
}

/** Capture MCU and port while the shell still has a warm device record. */
void tiku_bench_target_capture(bench_t *st)
{
    st->mcu[0] = st->port[0] = '\0';
    if (st->services->board_mcu)
        st->services->board_mcu(st->services->ctx, st->target_name, st->mcu, sizeof st->mcu);
    if (st->services->board_port)
        st->services->board_port(st->services->ctx, st->target_name, st->port, sizeof st->port);
    for (char *p = st->mcu; *p; p++) *p = (char)tolower((unsigned char)*p);
    tiku_bench_profile_update(st);
}

void tiku_bench_targets_refresh(bench_t *st)
{
    char names[1024] = "", chosen[64] = "";
    if (st->services->board_names)
        st->services->board_names(st->services->ctx, names, sizeof names);
    /* Same board list, same answer: with none mounted that list is the
     * empty string, and rebuilding it every tick repainted an idle
     * window and republished its menus once a second. */
    if (!strcmp(names, st->mounted) && st->refreshed) return;
    st->refreshed = 1;
    snprintf(st->mounted, sizeof st->mounted, "%s", names);
    tiku_choice_init(&st->target);
    char *save = NULL;
    for (char *p = strtok_r(names, "\n", &save); p; p = strtok_r(NULL, "\n", &save))
        tiku_choice_add(&st->target, p, 1);
    snprintf(chosen, sizeof chosen, "%s", st->target_name);
    /* The remembered board first, while it is not mounted the shell's
     * own device; the memory is spent the moment the board is found. */
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
    /* Never silently switch a vanished selected board to a different device. */
    if (selected < 0 && !*chosen && st->target.count) selected = 0;
    st->target.selected = selected;
    st->target_name[0] = '\0';
    if (selected >= 0)
        snprintf(st->target_name, sizeof st->target_name, "%s", st->target.item[selected].label);
    tiku_bench_target_capture(st); st->dirty = 1;
}
