/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_appl_autostart.c - the one autostart list the samples share.
 *
 * The kernel's list is weak and a build may define it once, so every
 * sample that is built is named here rather than in its own file.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku.h"
#include <kernel/process/tiku_process.h>

#if TIKU_APPL_GUI_DEMO
extern struct tiku_process gui_demo_process;
#endif
#if TIKU_APPL_DRAW_DEMO
extern struct tiku_process draw_demo_process;
#endif
#if TIKU_APPL_DOC_DEMO
extern struct tiku_process doc_demo_process;
#endif

TIKU_AUTOSTART_PROCESSES(
#if TIKU_APPL_GUI_DEMO
    &gui_demo_process,
#endif
#if TIKU_APPL_DRAW_DEMO
    &draw_demo_process,
#endif
#if TIKU_APPL_DOC_DEMO
    &doc_demo_process,
#endif
    NULL);
