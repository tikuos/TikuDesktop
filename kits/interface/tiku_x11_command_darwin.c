/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_x11_command_darwin.c - which key a machine gives commands with.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_x11_command.h"

/**
 * @brief Whether the key a person gives commands with is Command.
 *
 * Command.  Every application on this machine closes with Command-W
 * and copies with Command-C, and a desktop asking for Control-C instead
 * would be the only thing on the screen that did.
 */
int
tiku_x11_command_is_meta(void)
{
    return 1;
}
