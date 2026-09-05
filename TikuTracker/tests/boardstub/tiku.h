/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku.h - the kernel as the board library sees it, for a host test.
 *
 * Only what board/gui/ names: a process to post to, a VFS node with
 * dynamic children, a console backend to write bytes to.  The test
 * supplies the functions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_STUB_H_
#define TIKU_STUB_H_

#include <stddef.h>
#include <stdint.h>

#define TIKU_STUB 1

#endif /* TIKU_STUB_H_ */
