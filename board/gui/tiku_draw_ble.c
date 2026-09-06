/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_draw_ble.c - the session over the BLE link, and how that stands.
 *
 * The link is the kernel's; this is the little that turns one word written
 * to a file into advertising, and the line read back that says whether a
 * central has taken the session.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_draw_ble.h"
#include "tiku_draw.h"

#include <kernel/link/tiku_link_ble.h>
#include <kernel/link/tiku_link_console.h>

#include <stdio.h>
#include <string.h>

/* One size for the session's messages on every link it rides. */
#define DESK_MTU TIKU_LINK_CONSOLE_SESSION_MTU

static tiku_link_ble_t desk;
static tiku_link_t *desk_link;
static uint8_t desk_buf[DESK_MTU];

/** @brief Stop advertising, and carry the session on the console again. */
static void
hang_up(void)
{
    if (desk_link != NULL) {
        tiku_draw_set_link(NULL);
        tiku_link_close(desk_link);
        desk_link = NULL;
    }
}

int
tiku_draw_ble_write(const char *value, size_t len)
{
    while (len > 0u && (value[len - 1u] == '\n' || value[len - 1u] == '\r' ||
                        value[len - 1u] == ' ')) {
        len--;
    }
    if (len == 3u && memcmp(value, "off", 3u) == 0) {
        hang_up();
        return 0;
    }
    if (len != 3u || memcmp(value, "ble", 3u) != 0) {
        return -1;
    }
    hang_up();
    desk_link = tiku_link_ble_open(&desk, TIKU_DRAW_BLE_NAME,
                                   desk_buf, sizeof desk_buf);
    if (desk_link == NULL) {
        return -1;
    }
    tiku_draw_set_link(desk_link);
    return 0;
}

int
tiku_draw_ble_read(char *buf, size_t max)
{
    static const char *const where[] = { "advertising", "up" };
    const tiku_link_ble_stats_t *st;
    uint8_t state;

    if (desk_link == NULL) {
        return snprintf(buf, max, "off\n");
    }
    state = tiku_link_ble_state(&desk);
    st = tiku_link_ble_stats(&desk);
    return snprintf(buf, max,
                    "ble %s %s rx %lu tx %lu drops %lu oversize %lu "
                    "refused %lu stalled %lu\n",
                    TIKU_DRAW_BLE_NAME, where[(state <= 1u) ? state : 0u],
                    (unsigned long)st->rx, (unsigned long)st->tx,
                    (unsigned long)st->drops, (unsigned long)st->oversize,
                    (unsigned long)st->refused, (unsigned long)st->stalled);
}

uint8_t
tiku_draw_ble_active(void)
{
    return (desk_link != NULL) ? 1u : 0u;
}
