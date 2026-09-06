/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_draw_desk.c - one file, every road off the cable.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_draw_desk.h"

#if TIKU_APPL_GUI_IP
#include "tiku_draw_ip.h"
#endif
#if TIKU_APPL_GUI_BLE
#include "tiku_draw_ble.h"
#endif

#include <stdio.h>
#include <string.h>

/** @brief The word written, less the line ending a shell leaves on it. */
static size_t
trimmed(const char *value, size_t len)
{
    while (len > 0u && (value[len - 1u] == '\n' || value[len - 1u] == '\r' ||
                        value[len - 1u] == ' ')) {
        len--;
    }
    return len;
}

int
tiku_draw_desk_write(const char *value, size_t len)
{
    size_t n = trimmed(value, len);

    if (n == 3u && memcmp(value, "off", 3u) == 0) {
#if TIKU_APPL_GUI_IP
        (void)tiku_draw_ip_write("off", 3u);
#endif
#if TIKU_APPL_GUI_BLE
        (void)tiku_draw_ble_write("off", 3u);
#endif
        return 0;
    }
    if (n == 3u && memcmp(value, "ble", 3u) == 0) {
#if TIKU_APPL_GUI_BLE
#if TIKU_APPL_GUI_IP
        (void)tiku_draw_ip_write("off", 3u);
#endif
        return tiku_draw_ble_write(value, n);
#else
        return -1;
#endif
    }
#if TIKU_APPL_GUI_IP
#if TIKU_APPL_GUI_BLE
    (void)tiku_draw_ble_write("off", 3u);
#endif
    return tiku_draw_ip_write(value, n);
#else
    return -1;
#endif
}

int
tiku_draw_desk_read(char *buf, size_t max)
{
#if TIKU_APPL_GUI_BLE
    if (tiku_draw_ble_active()) {
        return tiku_draw_ble_read(buf, max);
    }
#endif
#if TIKU_APPL_GUI_IP
    return tiku_draw_ip_read(buf, max);
#else
    return snprintf(buf, max, "off\n");
#endif
}
