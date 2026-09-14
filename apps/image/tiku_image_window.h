/* TikuImage: native, read-only image and camera-snapshot viewer.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_IMAGE_WINDOW_H_
#define TIKU_IMAGE_WINDOW_H_

#include "tiku_window.h"
#include <stddef.h>

typedef struct tiku_image_window tiku_image_window_t;

/** @brief Create an empty viewer; file selection remains desktop policy. */
tiku_image_window_t *tiku_image_window_new(
    tiku_workspace_t *workspace, int *dirty,
    void (*choose)(void *ctx), void *ctx);
/** @brief Open or activate the viewer without implicitly loading a file. */
void tiku_image_window_open(tiku_image_window_t *c);
/** @brief Close, stop refresh and discard the document and pending result. */
void tiku_image_window_close(tiku_image_window_t *c);
/** @brief Detach callbacks; pending workers retain their own lifetime. */
void tiku_image_window_destroy(tiku_image_window_t *c);
/** @brief Current native window, or NULL. */
tiku_window_t *tiku_image_window_handle(tiku_image_window_t *c);
/** @brief Queue a bounded regular-file read and decode off the UI thread. */
int tiku_image_window_file(tiku_image_window_t *c, const char *path);
/** @brief Copy a complete encoded snapshot and decode asynchronously.
 * This is not a camera transport. The producer owns acquisition and must
 * supply a whole PNG/JPEG/BMP, not a text VFS value or partial video frame.
 */
int tiku_image_window_bytes(tiku_image_window_t *c,
    const char *label, const void *bytes, size_t len);
/** @brief Service optional file refresh; monotonic microseconds. */
void tiku_image_window_tick(tiku_image_window_t *c, int64_t now);
/** @brief Maximize/restore inside the desktop's usable frame rectangle. */
int tiku_image_window_zoom(tiku_image_window_t *c, tiku_rect_t area);
/** @brief Explain a rejected source without replacing the displayed image. */
void tiku_image_window_note(tiku_image_window_t *c, const char *text);
/** @brief Current local path, empty for an in-memory image or empty viewer. */
const char *tiku_image_window_path(tiku_image_window_t *c);
/** @brief Filename hint for desktop routing; decoding still checks content. */
int tiku_image_file_supported(const char *path);

#endif
