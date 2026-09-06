/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_draw_ip.c - dialling a desktop, and saying how that went.
 *
 * The link is the kit's; this is the little that turns an address written
 * to a file into one, and the line read back that says where it stands.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_draw_ip.h"

#include "tiku_draw.h"

#include <kernel/link/tiku_link_console.h>
#include <tikukits/net/link/tiku_kits_net_link_ip.h>

#include <stdio.h>
#include <string.h>

/*
 * The session's messages are one size on every link it rides, so a window
 * drawn over the cable is the same window over an address.
 */
#define DESK_MTU TIKU_LINK_CONSOLE_SESSION_MTU

static tiku_kits_net_link_ip_t desk;
static tiku_link_t *desk_link;
static uint8_t desk_buf[DESK_MTU];

/**
 * @brief Read "a.b.c.d" or "a.b.c.d:port" out of @p s.
 * @return 0 and fills @p addr and @p port, or -1
 */
static int
parse_desk(const char *s, size_t len, uint8_t addr[4], uint16_t *port)
{
    unsigned v[4] = { 0u, 0u, 0u, 0u };
    unsigned p = TIKU_KITS_NET_LINK_IP_PORT;
    char text[32];
    int n;

    if (len == 0u || len >= sizeof text) {
        return -1;
    }
    memcpy(text, s, len);
    text[len] = '\0';
    n = sscanf(text, "%u.%u.%u.%u:%u", &v[0], &v[1], &v[2], &v[3], &p);
    if (n < 4 || v[0] > 255u || v[1] > 255u || v[2] > 255u || v[3] > 255u ||
        p == 0u || p > 65535u) {
        return -1;
    }
    addr[0] = (uint8_t)v[0];
    addr[1] = (uint8_t)v[1];
    addr[2] = (uint8_t)v[2];
    addr[3] = (uint8_t)v[3];
    *port = (uint16_t)p;
    return 0;
}

/** @brief Hang up, and carry the session on the console again. */
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
tiku_draw_ip_write(const char *value, size_t len)
{
    uint8_t addr[4];
    uint16_t port;

    while (len > 0u && (value[len - 1u] == '\n' || value[len - 1u] == '\r' ||
                        value[len - 1u] == ' ')) {
        len--;
    }
    if (len == 3u && memcmp(value, "off", 3u) == 0) {
        hang_up();
        return 0;
    }
    if (parse_desk(value, len, addr, &port) != 0) {
        return -1;
    }
    hang_up();
    desk_link = tiku_kits_net_link_ip_open(&desk, addr, port,
                                           desk_buf, sizeof desk_buf);
    if (desk_link == NULL) {
        return -1;
    }
    tiku_draw_set_link(desk_link);
    return 0;
}

int
tiku_draw_ip_read(char *buf, size_t max)
{
    static const char *const where[] = { "down", "dialling", "up" };
    const tiku_kits_net_link_ip_stats_t *st;
    uint8_t state;

    if (desk_link == NULL) {
        return snprintf(buf, max, "off\n");
    }
    state = tiku_kits_net_link_ip_state(&desk);
    st = tiku_kits_net_link_ip_stats(&desk);
    return snprintf(buf, max,
                    "%u.%u.%u.%u:%u %s dials %lu rx %lu tx %lu drops %lu "
                    "refused %lu stalled %lu\n",
                    desk.addr[0], desk.addr[1], desk.addr[2], desk.addr[3],
                    (unsigned)desk.port,
                    where[(state <= 2u) ? state : 0u],
                    (unsigned long)st->dials, (unsigned long)st->rx,
                    (unsigned long)st->tx, (unsigned long)st->drops,
                    (unsigned long)st->refused, (unsigned long)st->stalled);
}
