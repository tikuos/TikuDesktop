/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_draw_ble.h - the desk reached over the air: the session on the BLE
 * link, started and stopped through /gui/desk.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DRAW_BLE_H_
#define TIKU_DRAW_BLE_H_

#include <stddef.h>
#include <stdint.h>

/** @brief The name the board advertises under while its desk is the air. */
#define TIKU_DRAW_BLE_NAME "TIKU-DESK"

/**
 * @brief "ble" starts advertising and carries the session over whoever
 *        connects; "off" returns it to the console.
 * @return 0, or -1 for a word that is neither or a link that will not open
 */
int tiku_draw_ble_write(const char *value, size_t len);

/**
 * @brief The state line: "off", or the link's state and the messages that
 *        have crossed.
 * @return bytes written, or -1
 */
int tiku_draw_ble_read(char *buf, size_t max);

/** @brief Whether the session is on the air right now. */
uint8_t tiku_draw_ble_active(void);

#endif /* TIKU_DRAW_BLE_H_ */
