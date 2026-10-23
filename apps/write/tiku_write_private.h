/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_private.h - document window, commands and isolated file jobs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_WRITE_PRIVATE_H_
#define TIKU_WRITE_PRIVATE_H_
#include "tiku_app.h"
#include "tiku_richfont.h"
#include "tiku_richio.h"
#include "tiku_richhtml.h"
#include "tiku_richmarkup.h"
#include "tiku_richoutline.h"
#include "tiku_richruler.h"
#include "tiku_richlinkscan.h"
#include "tiku_richbookmark.h"
#include "tiku_richoutput.h"
#include "tiku_atomic_file.h"
#include "tiku_dialog.h"
#include "tiku_field_event.h"
#include "tiku_image.h"
#include <pthread.h>
#include <stdatomic.h>
#include <sys/stat.h>

#define WRITE_PATH 1024
#define WRITE_QUEUE 256
#define WRITE_FILE_MAX (68u << 20)

/** @brief Window actions; document commands occupy a separate numeric range. */
enum {
    W_NEW = 1, W_OPEN, W_SAVE, W_SAVEAS, W_CLOSE, W_RECOVER,
    W_CUT, W_COPY, W_PASTE, W_ALL, W_FIND, W_NEXT, W_REPLACE,
    W_REPLACE_ALL, W_CANCEL, W_CONFIRM, W_DISCARD, W_ZOOM_IN, W_ZOOM_OUT,
    W_ZOOM_RESET, W_FONT, W_SIZE, W_COLOR, W_LINK, W_APPLY,
    W_BODY = 100, W_FIELD, W_FIELD2, W_OUTLINE_LIST, W_RULER_CONTROL,
    W_FORMAT = 200, W_BULLET = 300, W_NUMBER, W_UNLIST,
    W_SUPER, W_SUB, W_NORMAL, W_HIGHLIGHT, W_TITLE, W_HEADING, W_TEXT,
    W_EXPORT_TEXT, W_PAGE_A4, W_PAGE_LETTER, W_LANDSCAPE, W_MARGINS,
    W_PREVIEW, W_PREV_PAGE, W_NEXT_PAGE, W_EXPORT_PDF, W_WORDS,
    W_INDENT, W_OUTDENT, W_FIRST_INDENT, W_SPACE_COMPACT, W_SPACE_NORMAL,
    W_SPACE_ROOMY, W_KEEP_NEXT, W_KEEP_LINES, W_PAGE_BREAK,
    W_LIST_DEEPER, W_LIST_SHALLOWER, W_LIST_RESTART,
    W_STYLE_APPLY, W_STYLE_DEFINE, W_STYLE_UPDATE, W_STYLE_CLEAR, W_STYLE_NONE,
    W_PICTURE_INSERT = 400, W_PICTURE_SIZE, W_PICTURE_ALT,
    W_TABLE_INSERT, W_TABLE_REMOVE,
    W_TABLE_LAYOUT, W_TABLE_BORDER, W_TABLE_COLUMN,
    W_TABLE_HEADERS, W_TABLE_EQUAL,
    W_CELL_FILL, W_CELL_CLEAR,
    W_SELECT_CELL, W_SELECT_ROW, W_SELECT_COLUMN, W_SELECT_TABLE, W_CELLS_ERASE,
    W_EXPORT_HTML, W_EXPORT_MARKDOWN,
    W_OUTLINE = 450, W_OUTLINE_PREV, W_OUTLINE_NEXT,
    W_RULER,
    W_LINK_PREV, W_LINK_NEXT,
    W_HEADING_LEVEL = 460,
    W_BOOKMARK_ADD = 470, W_BOOKMARK_GO, W_BOOKMARK_REMOVE, W_LINK_FOLLOW,
    W_PASTE_TEXT = 480,
    W_STYLE_ITEM = 1000,
    W_BOOKMARK_ITEM = 1100
};
/** @brief Modal question and auxiliary panel identities. */
enum {
    W_PROMPT_NONE, W_PROMPT_UNSAVED, W_PROMPT_OVERWRITE, W_PROMPT_CONFLICT,
    W_PROMPT_RECOVER
};
enum { W_PANEL_NONE, W_PANEL_FIND, W_PANEL_FONT, W_PANEL_SIZE,
       W_PANEL_COLOR, W_PANEL_LINK, W_PANEL_STYLE_APPLY, W_PANEL_STYLE_DEFINE,
       W_PANEL_PICTURE_SIZE, W_PANEL_PICTURE_ALT, W_PANEL_TABLE,
       W_PANEL_TABLE_LAYOUT, W_PANEL_TABLE_BORDER, W_PANEL_TABLE_COLUMN,
       W_PANEL_TABLE_HEADERS,
       W_PANEL_CELL_FILL, W_PANEL_BOOKMARK_ADD, W_PANEL_BOOKMARK_GO,
       W_PANEL_BOOKMARK_REMOVE };
enum { W_JOB_LOAD = 1, W_JOB_SAVE, W_JOB_RECOVERY, W_JOB_TEXT,
       W_JOB_PROBE, W_JOB_PDF, W_JOB_IMAGE, W_JOB_HTML, W_JOB_MARKDOWN };

/** @brief Recorded file identity, metadata and SHA-256 of its full contents. */
typedef struct {
    int exists;                 /**< zero denotes an absent destination */
    struct stat stat;            /**< file identity and modification times */
    unsigned char hash[32];     /**< content digest */
} write_version_t;

/** @brief Worker-owned input and output, published by an atomic completion. */
typedef struct {
    pthread_t thread;           /**< joined before freeing job memory */
    atomic_int done, cancel;    /**< completion and cooperative cancellation */
    int kind;                   /**< load, save, recovery or text export */
    char path[WRITE_PATH];      /**< immutable local destination */
    write_version_t expected, actual; /**< before and after file versions */
    unsigned char written_hash[32]; /**< digest of completed temporary file */
    tiku_richsnapshot_t *snapshot; /**< retained immutable save source */
    tiku_richdoc_t *loaded;     /**< transferred to owner after join */
    tiku_richio_status_t status; /**< codec or input failure */
    tiku_atomic_result_t result; /**< replacement outcome */
    uint64_t generation;       /**< owner generation at job creation */
    int imported;              /**< converted input requires native Save As */
    int recovered;             /**< recovered copies do not own source path */
    unsigned notices;          /**< format-specific conversion differences */
    tiku_image_t image;        /**< decoded worker-owned inline image */
    tiku_rich_pos_t first, last; /**< captured image insertion range */
} write_job_t;

/** @brief One independently hosted document and its local UI state. */
typedef struct {
    const tiku_app_services_t *services; /**< borrowed host callbacks */
    uint32_t id;               /**< host window, zero after closure */
    tiku_surface_t *surface;   /**< complete raster frame */
    int width, height;         /**< accepted logical frame dimensions */
    tiku_richfont_t *fonts;    /**< owner-thread drawing provider */
    tiku_richdoc_t *doc;       /**< owner-thread mutable document */
    tiku_richview_t *view;     /**< independent selection and layout */
    int clip_supported, clip_action; /**< protocol support and pending verb */
    int clip_formats;           /**< negotiated multi-format publication count */
    unsigned clip_notices;      /**< HTML paste conversion differences */
    int64_t clip_request, clip_deadline; /**< correlation ID and timeout */
    uint64_t clip_generation; /**< source/destination content at request */
    tiku_rich_pos_t clip_anchor, clip_caret; /**< directional selection */
    tiku_rich_cells_t clip_cells; /**< pending rectangle, or table zero */
    tiku_richpages_t *pages;   /**< preview job retaining source geometry */
    uint64_t pages_generation; /**< page-preview source generation */
    size_t preview_page;      /**< zero-based displayed page */
    int preview;              /**< preview takes no text-editing input */
    int show_outline;         /**< heading sidebar visibility */
    int show_ruler, ruler_drag, ruler_part; /**< visibility and handle capture */
    tiku_rich_ruler_t ruler_source, ruler_preview; /**< immutable drag and preview */
    double ruler_pointer;     /**< press position in document points */
    tiku_richlinkscan_t *linkscan; /**< pending navigation, cancelled on selection */
    uint64_t link_generation;
    int link_forward;          /**< pending navigation direction, 0 previous */
    tiku_rich_link_match_t link_match; /**< last raw occurrence; target is NULL */
    tiku_rich_pos_t link_first, link_last; /**< displayed grapheme-expanded range */
    uint64_t bookmark_generation; /**< generation of pending panel or navigation */
    tiku_rich_pos_t bookmark_position; /**< captured caret or navigation target */
    int bookmark_pending;     /**< navigation waiting for current layout */
    uint64_t bookmark_menu_generation;
    char menu_bookmarks[24][64]; /**< exact names in current shortcut submenu */
    int outline_pending;      /**< navigation action awaiting its index */
    tiku_richoutline_t *outline; /**< immutable incremental heading index */
    uint64_t outline_generation; /**< latest attempted source generation */
    size_t outline_top, outline_selected; /**< virtual list positions */
    tiku_rich_status_t outline_status; /**< scan or allocation result */
    uint64_t menu_styles[32]; /**< named paragraph style IDs in current menu */
    tiku_dialog_t dialog;     /**< toolbar, panel and question focus */
    tiku_field_t fields[2];    /**< panel text inputs */
    tiku_field_input_t field_input[2]; /**< committed text and field history */
    uint64_t table_id, table_generation; /**< property panel target */
    unsigned table_column;    /**< property panel's zero-based column */
    uint64_t cell_id;          /**< shading panel's cell identity */
    tiku_rich_cells_t cell_selection; /**< shading panel's captured rectangle */
    int pointer_x, pointer_y, cursor; /**< last window coordinates and shape */
    tiku_event_t queue[WRITE_QUEUE]; /**< ordered input during reflow */
    size_t qfirst, qcount;     /**< input ring indices */
    int cancel_input;          /**< discard incomplete rejected transfer */
    int active, reveal, repaint; /**< caret, deferred scroll and frame flags */
    int body_drag, scroll_drag, scroll_grab; /**< pointer capture state */
    double zoom;              /**< logical pixels per document point */
    int panel, prompt, pending; /**< panel, question, continuation action */
    int pick_kind;            /**< requested file panel operation */
    uint64_t pick_generation; /**< prevent stale picker replacing edits */
    tiku_rich_pos_t pick_first, pick_last; /**< image picker target */
    int imported;             /**< unsaved imported or recovered content */
    int closing;              /**< owner requested application closure */
    char path[WRITE_PATH];    /**< current native save destination */
    char target[WRITE_PATH];  /**< destination awaiting confirmation */
    write_version_t version, target_version; /**< expected file versions */
    write_job_t *job;         /**< one joinable I/O worker */
    char recovery_dir[WRITE_PATH]; /**< private per-process recovery folder */
    char recovery[WRITE_PATH]; /**< this document's native checkpoint */
    char offered[WRITE_PATH]; /**< inactive recovery offered explicitly */
    int recovery_lock;        /**< locked private session descriptor */
    uint64_t recovered_generation; /**< most recent recovery checkpoint */
    int64_t now, edited_at, attempted_at; /**< recovery scheduling clocks */
    char status[256];         /**< non-modal result or refusal message */
    tiku_richsearch_t *search; /**< cancellable incremental literal scan */
    tiku_richsnapshot_t *search_source; /**< immutable scope */
    tiku_rich_match_t match;  /**< currently selected verified occurrence */
    int search_wrapped;       /**< wrapped once to document beginning */
    int whole_words;         /**< Unicode word/grapheme match filtering */
    char find_text[256], replace_text[256]; /**< last Find panel values */
    tiku_rich_pos_t search_origin; /**< exclusive wrapped search end */
    uint64_t search_generation; /**< captured model generation */
    int search_replace_all;  /**< collect matches for atomic replacement */
    tiku_rich_match_t matches[1024]; /**< bounded all-match transaction */
    size_t match_count;       /**< number of collected nonoverlapping hits */
    int64_t click_time;       /**< previous primary press */
    int click_x, click_y, clicks; /**< word/line multi-click tracking */
} write_app_t;

/** @brief Read a bounded regular file's version; absent paths are valid. */
int write_version(const char *path, write_version_t *out);
/** @brief Compare presence, identity, metadata and full content digest. */
int write_version_equal(const write_version_t *a, const write_version_t *b);
/** @brief Launch a worker using copied inputs and a retained snapshot. */
int write_job_start(write_app_t *app, int kind, const char *path,
                    const write_version_t *expected, int recovered);
/** @brief Collect a completed worker; wait is reserved for shutdown. */
int write_job_finish(write_app_t *app, int wait);
/** @brief Adopt ownership of a new document after allocating its view. */
int write_adopt(write_app_t *app, tiku_richdoc_t *doc);
/** @brief Prepare private recovery storage and find an inactive checkpoint. */
void write_recovery_init(write_app_t *app);
/** @brief Release the recovery lock and optionally remove this owned folder. */
void write_recovery_stop(write_app_t *app, int discard);
/** @brief Start a native checkpoint after quiet edits; never mark it saved. */
void write_recovery_tick(write_app_t *app);
/** @brief Whether text or an imported copy needs a native save. */
int write_dirty(const write_app_t *app);
/** @brief Execute a menu/button action and update its continuation state. */
void write_action(write_app_t *app, int command);
/** @brief Handle bookmark panels, quick navigation and internal link activation. */
int write_bookmarks_action(write_app_t *app, int command);
/** @brief Select a pending bookmark once its generation has usable layout. */
void write_bookmarks_step(write_app_t *app);
/** @brief Add a bounded HTML representation to a captured native publication. */
struct tiku_msg *write_clip_html_publish(write_app_t *app, const char *text,
    const char *mime, const void *data, size_t bytes);
/** @brief Decode external UTF-8 HTML and paste a flat fragment or one table. */
tiku_rich_status_t write_clip_html_paste(write_app_t *app,
    const void *data, size_t bytes);
/** @brief Execute a supported formatting command on selection or typing. */
void write_format(write_app_t *app, int command);
/** @brief Edit named paragraph styles through public commands. */
void write_style_action(write_app_t *app, int command);
/** @brief Cancel search and release its immutable input. */
void write_search_cancel(write_app_t *app);
/** @brief Start a bounded literal search or collect an atomic Replace All. */
void write_search_start(write_app_t *app, int all);
/** @brief Advance search without blocking the owner event loop. */
void write_search_step(write_app_t *app);
/** @brief Replace a still-selected, current-generation match. */
void write_replace(write_app_t *app);
/** @brief Build live controls and update the viewport dimensions. */
void write_controls(write_app_t *app);
/** @brief Text viewport in window logical pixels. */
tiku_rect_t write_body(const write_app_t *app);
/** @brief Render the window and publish current menu states. */
void write_paint(write_app_t *app);
/** @brief Advance layout and queued events in arrival order. */
void write_step(write_app_t *app);
/** @brief Queue shared editor input in viewport-local coordinates. */
void write_input(write_app_t *app, const tiku_event_t *event);
/** @brief Update dirty/recovery clocks after a successful model mutation. */
void write_changed(write_app_t *app, uint64_t before);
/** @brief Show an operation refusal without altering document state. */
void write_refused(write_app_t *app, tiku_rich_status_t status);
/** @brief Process a file panel result with stale-document checks. */
void write_picked(void *state, uint32_t window, const char *path);
/** @brief Load installed generic font variants for display or export. */
int write_fonts(tiku_richfont_t **out);
/** @brief Advance the preview job for the current document generation. */
void write_pages_step(write_app_t *app);
/** @brief Set paper properties atomically or change page-preview navigation. */
void write_page_action(write_app_t *app, int command);
/** @brief Collect an image job only if its document and selection match. */
void write_picture_finish(write_app_t *app, write_job_t *job);
/** @brief Open or apply width/alternate-text controls for one picture. */
void write_picture_action(write_app_t *app, int command);
/** @brief Open the table-size panel or apply an insert/delete table command. */
void write_table_action(write_app_t *app, int command);
/** @brief Edit table sizing, border appearance or the current column share. */
void write_table_properties(write_app_t *app, int command);
/** @brief Open or apply the current cell's background-colour controls. */
void write_cell_action(write_app_t *app, int command);
/** @brief Parse #RRGGBB or #AARRGGBB without partial input acceptance. */
int write_argb(const char *text, uint32_t *out);
/** @brief Update the resize cursor from geometry and pointer position. */
void write_pointer(write_app_t *app, const tiku_event_t *event);
/** @brief Whether an action supports an active cell rectangle. */
int write_cells_allowed(write_app_t *app, int action);
/** @brief Handle rectangular selection, copy and content clearing actions. */
int write_cells_action(write_app_t *app, int action);
/** @brief Discover typed clipboard support without changing its contents. */
void write_clip_probe(write_app_t *app);
/** @brief Begin an acknowledged Copy/Cut or a fresh clipboard read. */
void write_clip_action(write_app_t *app, int action);
/** @brief Consume matching clipboard replies; stale replies change nothing. */
void write_clip_message(void *state, const struct tiku_msg *message);
/** @brief Cancel unanswered clipboard actions without editing content. */
void write_clip_tick(write_app_t *app);
/** @brief Advance or refresh the heading index for the current document. */
void write_outline_step(write_app_t *app);
/** @brief Release heading state when changing or closing a document. */
void write_outline_clear(write_app_t *app);
/** @brief Width reserved beside the editor for the visible outline. */
int write_outline_width(const write_app_t *app);
/** @brief Paint visible headings and scan status without traversing the source. */
void write_outline_paint(write_app_t *app);
/** @brief Handle heading-list pointer and keyboard input; return consumed. */
int write_outline_event(write_app_t *app, const tiku_event_t *event);
/** @brief Toggle the outline or navigate to a previous/next semantic heading. */
void write_outline_action(write_app_t *app, int action);
/** @brief Paint paragraph indentation handles, measurements and a drag guide. */
void write_ruler_paint(write_app_t *app);
/** @brief Ruler bounds, empty when hidden or in page preview. */
tiku_rect_t write_ruler_bounds(const write_app_t *app);
/** @brief Handle ruler capture and keyboard editing; return consumed input. */
int write_ruler_event(write_app_t *app, const tiku_event_t *event);
/** @brief Return a nearby enabled handle plus one, or zero. */
int write_ruler_hit(write_app_t *app, int x, int y);
/** @brief Cancel pending link navigation and discard its source positions. */
void write_links_clear(write_app_t *app);
/** @brief Start previous/next hyperlink navigation without opening its target. */
void write_links_action(write_app_t *app, int action);
/** @brief Advance hyperlink navigation and select a generation-checked result. */
void write_links_step(write_app_t *app);

#endif
