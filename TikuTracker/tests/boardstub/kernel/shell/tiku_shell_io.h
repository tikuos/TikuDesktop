/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_io.h - the console backend the board library writes frames to.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_STUB_SHELL_IO_H_
#define TIKU_STUB_SHELL_IO_H_

#include <stdint.h>

typedef struct {
    void    (*putc)(char c);
    uint8_t (*rx_ready)(void);
    int     (*getc)(void);
    uint8_t   flags;
    uint8_t   cap;
} tiku_shell_io_t;

/** @brief The test's backend, which keeps every byte written. */
const tiku_shell_io_t *tiku_shell_io_get_backend(void);

#endif /* TIKU_STUB_SHELL_IO_H_ */
