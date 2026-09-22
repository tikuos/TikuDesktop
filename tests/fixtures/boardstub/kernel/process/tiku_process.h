/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_process.h - the process and event shapes the board library posts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_STUB_PROCESS_H_
#define TIKU_STUB_PROCESS_H_

#include <stdint.h>

struct tiku_process { const char *name; };
typedef unsigned char tiku_event_t;
typedef void *tiku_event_data_t;
#define TIKU_EVENT_USER 0x10

/** @brief The test records every post. */
uint8_t tiku_process_post(struct tiku_process *p, tiku_event_t ev,
                          tiku_event_data_t data);

#endif /* TIKU_STUB_PROCESS_H_ */
