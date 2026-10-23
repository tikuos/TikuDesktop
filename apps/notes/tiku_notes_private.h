/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_notes_private.h - sticky-note documents, views and save jobs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_NOTES_PRIVATE_H_
#define TIKU_NOTES_PRIVATE_H_

#include "tiku_app.h"
#include "tiku_richfont.h"
#include <pthread.h>
#include <stdatomic.h>

#define NOTES_MAX 24
#define NOTE_W 240
#define NOTE_H 180
#define NOTE_MIN_W 140
#define NOTE_MIN_H 80
#define MARGIN 8
#define NOTE_TEXT_MAX (4u * 1024u * 1024u)
#define NOTE_FILE_MAX (NOTE_TEXT_MAX + 250000u + 256u)
#define SAVE_AFTER_US 500000
#define DOUBLE_CLICK_US 400000
#define NOTE_QUEUE_MAX 256

/** @brief Application commands, separate from document command identities. */
enum {
    CMD_NEW = 1, CMD_DELETE, CMD_IMPORTANT, CMD_QUIT, CMD_SAVE,
    CMD_COLOUR = 10, CMD_UNDO = 20, CMD_REDO, CMD_CUT, CMD_COPY,
    CMD_PASTE, CMD_SELECT_ALL, CMD_SHOW = 100
};

/** @brief Named background colours retained in the plain-text note header. */
typedef struct {
    const char *name;          /**< plain-file header value */
    tiku_rgb_t face;           /**< paper background colour */
} note_paper_t;
extern const note_paper_t PAPERS[4];
#define NPAPERS 4

/** @brief Immutable save input and worker result, acquired through done. */
typedef struct {
    pthread_t thread;         /**< joinable save worker */
    atomic_int done;          /**< release/acquire result publication */
    tiku_richsnapshot_t *snapshot; /**< immutable text being saved */
    char path[512];           /**< copied destination filename */
    int paper, important;     /**< captured plain-file header values */
    uint64_t metadata;        /**< captured metadata revision */
    int result;                /**< zero on durable replacement success */
} note_save_t;

/** @brief One plain-text note, its independent view and optional window. */
typedef struct {
    int used;                 /**< allocated slot */
    uint32_t id;              /**< host window, zero when absent */
    tiku_surface_t *surface;  /**< complete frame raster */
    int w, h;                /**< window content size in logical pixels */
    tiku_richdoc_t *doc;      /**< owner-thread mutable text */
    tiku_richview_t *view;    /**< independent editing and layout state */
    int paper, important;    /**< current plain-file header values */
    char path[512];          /**< destination filename */
    uint64_t metadata, saved_metadata; /**< current/saved header revisions */
    int64_t edited_at, attempted_at; /**< quiet-save and retry clocks */
    note_save_t *save;       /**< active worker, or NULL */
    int closing;               /**< close after the latest state is saved */
    int active;                /**< focus state for caret painting */
    int reveal;                /**< reveal caret when reflow finishes */
    char error[96];            /**< visible operation refusal */
    int64_t clicked_at;      /**< previous primary press time */
    int click_x, click_y, clicks; /**< successive press position and count */
    tiku_event_t queued[NOTE_QUEUE_MAX]; /**< input held during reflow */
    size_t queue_first, queue_count; /**< bounded input ring indices */
    int cancel_input;          /**< cancel composition after queue overflow */
} note_t;

/** @brief Notes application state; fonts are used on the owner thread. */
typedef struct {
    const tiku_app_services_t *services; /**< borrowed host callbacks */
    char dir[512];             /**< notes directory */
    note_t note[NOTES_MAX];    /**< document slots */
    unsigned next_number;     /**< next filename search starts after this */
    tiku_richfont_t *fonts;    /**< shared owner-thread native font set */
    int quitting;              /**< requested quit awaits durable saves */
} notes_t;

/** @brief Whether text or note metadata differs from its saved state. */
int note_dirty(const note_t *note);
/** @brief Read a bounded regular UTF-8 note file without partial import. */
int note_load(note_t *note, const char *path);
/** @brief Start an immutable snapshot save; return -1 on setup refusal. */
int note_save_start(note_t *note);
/** @brief Collect a completed save, optionally waiting during shutdown.
 *  @return 0 while pending, 1 on success, -1 on failure; keeps edits dirty. */
int note_save_finish(note_t *note, int wait);
/** @brief Write unsaved shutdown text to a private temporary recovery file.
 *  @note Copies its filename into path and reports it on stderr. Capacity
 *        must be at least 32 bytes. Returns -1 if recovery also fails. */
int note_recover(note_t *note, char *path, size_t capacity);
/** @brief Read the first paragraph into a UTF-8 boundary-safe window title. */
void note_title(const note_t *note, char *out, size_t capacity);
/** @brief Paint current geometry or a progress/error indication. */
void note_paint(notes_t *state, note_t *note);
/** @brief Publish Note, Edit and Notes menus with live command availability. */
void note_publish(notes_t *state, note_t *note);
/** @brief Repaint and update the note's window title and menus. */
void note_refresh(notes_t *state, note_t *note);
/** @brief Advance bounded layout and ordered pending input. */
void note_step(notes_t *state, note_t *note);
/** @brief Handle input, retaining its order while geometry is pending. */
void note_input(notes_t *state, note_t *note, const tiku_event_t *event);
/** @brief Execute a shared editing or plain clipboard action. */
void note_edit(notes_t *state, note_t *note, int command);

#endif /* TIKU_NOTES_PRIVATE_H_ */
