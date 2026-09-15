/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_x11_command_linux.c - which key a machine gives commands with.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_x11_command.h"

/**
 * @brief Whether the key a person gives commands with is Command.
 *
 * Control, and it has to stay Control: every shortcut this desktop
 * has ever had is a Control chord, and a keyboard here has no Command
 * key to move them to.
 */
int
tiku_x11_command_is_meta(void)
{
    return 0;
}
