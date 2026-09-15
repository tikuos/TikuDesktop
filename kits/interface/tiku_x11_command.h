/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_x11_command.h - which key a machine gives commands with.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_X11_COMMAND_H
#define TIKU_X11_COMMAND_H

/**
 * @brief Whether the key a person gives commands with is Command.
 *
 * One modifier on every desktop means "an instruction, not a letter",
 * and which key wears it belongs to the machine: Control on a PC
 * keyboard, Command on a Mac one.  A file per host answers it, since
 * the interface kit draws on no other and is built for boards with no
 * display.  Only which key, never which modifier bit carries it: that
 * belongs to the keymap, and is asked of the server holding it.
 *
 * @return nonzero when Command is that key, zero when Control is.
 */
int tiku_x11_command_is_meta(void);

#endif /* TIKU_X11_COMMAND_H */
