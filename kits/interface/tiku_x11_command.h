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
 * Every desktop has one modifier that means "this is an instruction,
 * not a letter", and which key wears it is a fact about the machine the
 * keyboard is attached to: on a PC keyboard it is Control, and on a Mac
 * keyboard it is the Command key beside the space bar.  A person's hand
 * goes where their machine taught it to go.
 *
 * One file per host answers this, chosen by the Makefile, so no source
 * in this kit has to name a platform to ask -- the same arrangement the
 * sound backends use.  It cannot be asked of the support kit's seam
 * where the rest of the machine's answers live, because the interface
 * kit draws on nothing: it is built for boards that have no display and
 * no support kit under them.
 *
 * This says only WHICH KEY, never which modifier bit carries it.  That
 * is the keymap's business and is asked of the server holding it.
 *
 * @return nonzero when Command is that key, zero when Control is.
 */
int tiku_x11_command_is_meta(void);

#endif /* TIKU_X11_COMMAND_H */
