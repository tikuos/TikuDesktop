/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_private.h - what the code view's files share: the view
 * itself and the one walk over a line that painting, hit testing and
 * measuring all use.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CODEVIEW_PRIVATE_H_
#define TIKU_CODEVIEW_PRIVATE_H_

#include "tiku_codeview.h"
#include "tiku_editdoc_private.h"
#include "tiku_text_input.h"

/* Pixels between the bounds' left edge and a line's x 0, and between the
 * bounds' top and the first row. */
#define TIKU_CV_PAD_X 5
#define TIKU_CV_PAD_Y 3
/* Pixels added below the face's height to make a row. */
#define TIKU_CV_ROW_GAP 2
/* Pixels each side of a control character's name, inside its cell. */
#define TIKU_CV_NAME_PAD 2
/* The line-number gutter: its pixels besides the digits, its separator
 * included, and the gap from it to a line's x 0. */
#define TIKU_CV_GUTTER_PAD 12
#define TIKU_CV_GUTTER_GAP 4
/* The fold column's width, at the gutter's right edge. */
#define TIKU_CV_FOLD_W 12
/* A scrollbar's thickness. */
#define TIKU_CV_BAR 15
/* Pixels past the widest line the text may scroll, room for the caret. */
#define TIKU_CV_SLACK 12
/* A line of at least TIKU_CV_LONG bytes keeps a checkpoint every
 * TIKU_CV_MARK bytes, so a walk can start near where it is going, and is
 * coloured only as far as it is drawn.  TIKU_CV_LONG_LINES such lines keep
 * checkpoints, the least recently walked given up first. */
#define TIKU_CV_LONG 4096
#define TIKU_CV_MARK 2048
#define TIKU_CV_LONG_LINES 4

/* A place a walk can resume from: a character boundary and its x. */
typedef struct {
    tiku_edit_pos_t pos;
    int64_t x;
} tiku_cv_mark_t;

/* Where the rows of a line that wraps start, kept for the TIKU_CV_WRAPS
 * lines of more than one row asked for last. */
#define TIKU_CV_WRAPS 64
typedef struct {
    int64_t line;                   /* -1: unused                       */
    uint64_t used;                  /* when last asked for              */
    tiku_cv_mark_t *rows;           /* each row's first byte, counted
                                       from the line's start, and its x */
    size_t nrows, cap;              /* rows kept, and room for them     */
} tiku_cv_wrap_t;

/* A line's fold state: inside a folded region, so it has no rows, and
 * heading one. */
#define TIKU_CV_HIDDEN 0x1u
#define TIKU_CV_FOLDED 0x2u

/* One row of a line. */
typedef struct {
    int64_t line;                   /* the line it shows                */
    int64_t sub;                    /* its place among the line's rows  */
    tiku_edit_pos_t start;          /* its first byte                   */
    tiku_edit_pos_t end;            /* the next row's first byte, or the
                                       line's end                       */
    int64_t x0;                     /* line x of its first byte         */
    int last;                       /* the line's last row of text      */
    int64_t ann;                    /* -1 a row of text, else the row of
                                       the line's annotations it shows  */
} tiku_cv_row_t;

/* The checkpoints of one long line, in order from its start. */
typedef struct {
    int64_t line;                   /* -1: unused                       */
    tiku_edit_pos_t start;          /* the line's first byte            */
    uint32_t key;                   /* the metrics they were taken in   */
    uint64_t used;                  /* when last walked                 */
    tiku_cv_mark_t *marks;
    size_t nmarks, cap;
} tiku_cv_long_t;

struct tiku_codeview {
    tiku_editdoc_t *doc;            /* referenced while the view lives  */
    tiku_edit_view_t view;          /* the document's selection for it  */
    tiku_edit_listener_t listener;  /* told of every change             */
    const tiku_font_t *font;        /* NULL: the monospaced face        */
    int tab_width;                  /* columns between tab stops        */
    int focused;                    /* draw the caret                   */
    tiku_rect_t bounds;             /* on the surface                   */
    int64_t top;                    /* line in the first row            */
    int64_t left;                   /* pixels scrolled left             */
    int64_t goal;                   /* x vertical moves keep; -1 none   */
    int dragging;                   /* button 1 is down on the view     */
    int unit;                       /* a drag selects 1 characters,
                                       2 words, 3 lines                 */
    tiku_edit_range_t pressed;      /* the word or line first selected  */
    int clicks;                     /* presses counted as one click     */
    int64_t click_at;               /* the last press's time, in us     */
    int click_x, click_y;           /* and where it was                 */
    unsigned options;               /* TIKU_CODEVIEW_LINE_NUMBERS ...   */
    int held_bar;                   /* thumb held: 1 vertical, 2 across */
    int grab;                       /* where in the thumb it was taken  */
    int arrow_bar;                  /* arrow held: 1 vertical, 2 across */
    int arrow_dir;                  /* -1 back, +1 on                   */
    int arrow_pulses;               /* ticks since it was pressed       */
    int64_t *widths;                /* each line's width, -1 unknown    */
    int64_t nwidths, widths_cap;    /* entries in use and allocated     */
    int64_t widest;                 /* -1 until found again             */
    int widths_ok;                  /* the table follows the document   */
    uint32_t widths_key;            /* the metrics it was measured in   */
    tiku_codeview_host_t host;      /* clipboard and invalidation       */
    char newline[3];                /* what Return inserts              */
    tiku_edit_status_t status;      /* the last event's edit            */
    tiku_text_input_t input;        /* a text-input commit arriving     */
    tiku_syntax_lang_t lang;        /* how the text is coloured         */
    unsigned char *states;          /* each line's starting C state     */
    int64_t nstates, states_cap;    /* lines it follows, and room       */
    int64_t sty_valid;              /* states up to here are right      */
    int64_t sty_hint;               /* and from there up to here are
                                       old ones that may still be       */
    int64_t sty_dirty;              /* the last line a change touched   */
    char *line_buf;                 /* a line copied out, and a NUL     */
    size_t line_cap;
    tiku_span_t *runs;              /* the coloured runs of a line      */
    size_t nruns, runs_cap;
    int runs_failed;                /* the runs could not all be kept   */
    tiku_cv_long_t longs[TIKU_CV_LONG_LINES];
    uint64_t walks;                 /* walks of long lines so far       */
    tiku_span_t *kept;              /* the runs of the last long line
                                       coloured, read again unchanged   */
    size_t nkept, kept_cap;
    int64_t kept_line;              /* that line, -1 none               */
    tiku_edit_pos_t kept_start;     /* where it started                 */
    uint64_t kept_gen;              /* the document's generation then   */
    int kept_in;                    /* the state it started in          */
    tiku_syntax_lang_t kept_lang;   /* and the language                 */
    tiku_edit_pos_t kept_cut;       /* the byte the runs reach to       */
    uint32_t fill_kinds;            /* marker kinds whose rows are
                                       filled                           */
    tiku_rgb_t fills[TIKU_EDIT_MARKER_KINDS]; /* and their colours      */
    tiku_rgb_t exec_fill;           /* the execution line's fill        */
    int list_shown;                 /* a list of words is shown         */
    int list_inserts;               /* its keys and clicks insert       */
    char **list_items;              /* its words, in one allocation     */
    size_t list_count;              /* how many                         */
    size_t list_current;            /* the one chosen                   */
    tiku_edit_anchor_t list_start;  /* the range the word replaces      */
    tiku_edit_anchor_t list_end;
    int tip_shown;                  /* a tip is shown                   */
    char *tip_text;                 /* what it says                     */
    tiku_edit_anchor_t tip_at;      /* where it was shown               */
    tiku_codeview_rules_t rules;    /* the application's own rules      */
    int own_rules;                  /* use them, not the language's     */
    int indent_spaces;              /* spaces a level; 0 a tab          */
    uint64_t brace_gen;             /* the generation the bracket by the
                                       caret was found in, 0 none       */
    tiku_edit_pos_t brace_caret;    /* the caret it was found for       */
    const void *brace_rules;        /* and the rules                    */
    tiku_edit_pos_t brace_at;       /* the bracket, -1 none             */
    tiku_edit_pos_t brace_match;    /* its match, -1 none, -2 too far   */
    int64_t top_sub;                /* rows of the top line above the
                                       first row shown                  */
    tiku_edit_pos_t upstream;       /* a caret here, at the end of a row,
                                       shows on that row and not at the
                                       next one's start; -1 none        */
    tiku_ed_lines_t row_map;        /* each line's first row, kept while
                                       lines wrap                       */
    unsigned char *row_known;       /* each line's rows counted         */
    unsigned char *row_fold;        /* each line's TIKU_CV_HIDDEN and
                                       TIKU_CV_FOLDED                   */
    int64_t folds;                  /* lines heading a folded region    */
    int64_t row_lines;              /* lines the map follows, 0: none   */
    int64_t row_cap;                /* room in row_known and row_fold   */
    int64_t row_width;              /* the width the rows break at      */
    uint32_t row_key;               /* and the metrics                  */
    int64_t unknown_from;           /* no line before it is uncounted   */
    tiku_cv_wrap_t wraps[TIKU_CV_WRAPS]; /* rows of lines that wrap     */
    uint64_t wrap_uses;             /* asks of the wraps so far         */
    int ann_shown;                  /* annotations add rows             */
    uint64_t ann_serial;            /* the annotations they were counted
                                       for                              */
    tiku_codeview_mark_t marks[TIKU_EDIT_INDICATOR_KINDS]; /* how each
                                       kind of indicator is drawn       */
    tiku_rgb_t mark_colors[TIKU_EDIT_INDICATOR_KINDS]; /* and in what   */
    tiku_edit_selection_t *sels;    /* the selections read in order     */
    size_t sels_cap;                /* room in sels                     */
    tiku_edit_selection_t sel_one;  /* the main one, read when there is
                                       no room for them all             */
    int rect;                       /* the drag makes a rectangle       */
    int64_t rect_x;                 /* the press's x on its row         */
    int64_t rect_line, rect_sub;    /* and its row                      */
    tiku_edit_selection_t *held;    /* the selections a Command press
                                       added one to, and room for it    */
    size_t nheld;                   /* how many it added to             */
};

/* What a character of a line is on the screen. */
typedef enum {
    TIKU_CV_GLYPH = 0,              /* drawn from the face              */
    TIKU_CV_TAB,                    /* space to the next stop           */
    TIKU_CV_CONTROL                 /* a control character, by name     */
} tiku_cv_kind_t;

typedef struct {
    tiku_edit_pos_t pos;            /* its first byte                   */
    int len;                        /* its bytes, 1 to 4                */
    unsigned cp;                    /* its code point                   */
    tiku_cv_kind_t kind;
    int64_t x;                      /* its left edge, from x 0          */
    int adv;                        /* its width                        */
    char bytes[5];                  /* its UTF-8 and a NUL              */
} tiku_cv_cell_t;

/* Called for each character in order; nonzero stops the walk. */
typedef int (*tiku_cv_cell_fn)(void *ctx, const tiku_cv_cell_t *cell);

/* The face in force. */
const tiku_font_t *tiku_cv_font(const tiku_codeview_t *v);
/* Pixels between tab stops in the face in force, at least 1. */
int tiku_cv_tab_px(const tiku_codeview_t *v);
/* The name a control character is shown by, in @p name (at least 5
 * bytes); 0 when @p cp is not a control character. */
int tiku_cv_control_name(unsigned cp, char *name);

/* Visit @p line's characters from its start, @p fn may be NULL.  Returns
 * the x where the walk ended: the left edge of the character @p fn
 * stopped at, or the line's width; -1 when there is no such line. */
int64_t tiku_cv_walk(tiku_codeview_t *v, int64_t line, tiku_cv_cell_fn fn,
                     void *ctx);
/* The same walk started at the line's last checkpoint at or before
 * @p pos, and at or before line x @p x: every character it skips lies
 * wholly before both.  Pass INT64_MAX for either that does not matter. */
int64_t tiku_cv_walk_near(tiku_codeview_t *v, int64_t line,
                          tiku_edit_pos_t pos, int64_t x, tiku_cv_cell_fn fn,
                          void *ctx);
/* Drop what a change starting at @p from leaves wrong: the checkpoints
 * past it, and every long line it starts before. */
void tiku_cv_marks_change(tiku_codeview_t *v, tiku_edit_pos_t from);
/* Free every checkpoint. */
void tiku_cv_marks_free(tiku_codeview_t *v);
/* The x of boundary @p pos on @p line, which must hold it. */
int64_t tiku_cv_x_of(tiku_codeview_t *v, int64_t line, tiku_edit_pos_t pos);
/* The caret position on @p line nearest the line's own @p x. */
tiku_edit_pos_t tiku_cv_pos_at(tiku_codeview_t *v, int64_t line, int64_t x);
/* The first byte of the character on @p line under the line's own @p x,
 * or -1 past the line's end. */
tiku_edit_pos_t tiku_cv_char_at(tiku_codeview_t *v, int64_t line, int64_t x);

/* Rows (tiku_codeview_rows.c).  Without the wrap option, annotations or
 * folds each line is one row and nothing is kept; with them a line's
 * text breaks into rows at the text's width, its annotations' rows follow
 * them, a line in a folded region has none, and the rows are counted
 * when they are first needed.  A line not yet counted keeps its last
 * count, or one row.  Positions and x are the line's own; a row's x0 is
 * subtracted where it is drawn. */
/* Whether rows are kept: lines wrap, annotations are shown or lines are
 * folded. */
int tiku_cv_rows_kept(const tiku_codeview_t *v);
/* Whether lines break into rows at the text's width now. */
int tiku_cv_wraps(const tiku_codeview_t *v);
/* The rows of @p line's text, counted now. */
int64_t tiku_cv_text_rows(tiku_codeview_t *v, int64_t line);
/* The first line from @p line on, or with @p back from it back, that is
 * not inside a folded region; -1 when there is none. */
int64_t tiku_cv_shown_line(const tiku_codeview_t *v, int64_t line,
                           int back);
/* Hide lines @p first to @p last, or with @p show show them, setting
 * their rows; lines hidden or shown already are left as they are. */
void tiku_cv_rows_hide(tiku_codeview_t *v, int64_t first, int64_t last,
                       int show);
/* Make sure rows are kept, as folding needs; 0 when there is no room. */
int tiku_cv_rows_keep(tiku_codeview_t *v);
/* Bring the rows in line with the options, width and metrics, keep the
 * first row inside the document, and count the lines shown. */
void tiku_cv_rows_ready(tiku_codeview_t *v);
/* Rows in all. */
int64_t tiku_cv_rows_total(const tiku_codeview_t *v);
/* The index of @p line's first row. */
int64_t tiku_cv_line_row(const tiku_codeview_t *v, int64_t line);
/* The rows @p line has, counted now. */
int64_t tiku_cv_line_rows(tiku_codeview_t *v, int64_t line);
/* Row @p sub of @p line, which must have it. */
void tiku_cv_row(tiku_codeview_t *v, int64_t line, int64_t sub,
                 tiku_cv_row_t *out);
/* The row holding @p pos; a position where a row breaks is on the next
 * row, or with @p upstream at the end of the row it ends. */
void tiku_cv_row_of(tiku_codeview_t *v, tiku_edit_pos_t pos, int upstream,
                    tiku_cv_row_t *out);
/* Move (@p line, @p sub) @p n rows on, or back when @p n is negative,
 * stopping at the document's ends; @return the rows moved. */
int64_t tiku_cv_rows_step(tiku_codeview_t *v, int64_t *line, int64_t *sub,
                          int64_t n);
/* The index of the first row shown. */
int64_t tiku_cv_top_row(const tiku_codeview_t *v);
/* Make the row at index @p row the first shown. */
void tiku_cv_set_top_row(tiku_codeview_t *v, int64_t row);
/* Rows from the first shown to row @p sub of @p line, negative above it;
 * the lines between are taken as counted. */
int64_t tiku_cv_shown_row(const tiku_codeview_t *v, int64_t line,
                          int64_t sub);
/* The caret position on row @p r nearest the row's own @p x, setting
 * @p upstream when it is the end of a row that is not the line's last. */
tiku_edit_pos_t tiku_cv_row_pos_at(tiku_codeview_t *v,
                                   const tiku_cv_row_t *r, int64_t x,
                                   int *upstream);
/* The row under surface @p y, the document's first or last row past its
 * ends; @return its place among the rows shown. */
int64_t tiku_cv_row_under(tiku_codeview_t *v, int64_t y, tiku_cv_row_t *r);
/* Follow a change in the rows, as tiku_cv_widths_change() does. */
void tiku_cv_rows_change(tiku_codeview_t *v, int64_t first,
                         int64_t last_old, int64_t last_new, int64_t lines);
/* Count lines not yet counted, @p budget bytes of them at most; whether
 * any were counted. */
int tiku_cv_rows_tick(tiku_codeview_t *v, int64_t budget);
/* Free the rows. */
void tiku_cv_rows_free(tiku_codeview_t *v);
/* A number for everything a line's width depends on. */
uint32_t tiku_cv_metrics_key(const tiku_codeview_t *v);

/* Where line x 0 and the first row's top are on the surface. */
int tiku_cv_text_x(const tiku_codeview_t *v);
int tiku_cv_text_y(const tiku_codeview_t *v);
/* The line-number gutter's width, 0 when the numbers are off. */
int tiku_cv_gutter(const tiku_codeview_t *v);
/* The text's ground: the bounds less the gutter and the scrollbars. */
tiku_rect_t tiku_cv_area(const tiku_codeview_t *v);
/* Pixels of a line the text area shows across. */
int64_t tiku_cv_text_w(const tiku_codeview_t *v);
/* A scrollbar's place, empty when the bar is off or the bounds are too
 * small to hold it. */
tiku_rect_t tiku_cv_bar(const tiku_codeview_t *v, int horizontal);
/* Where a bar's thumb is, in thousandths of its travel, and the share of
 * the bar it fills, in thousandths and at least 50. */
void tiku_cv_bar_place(tiku_codeview_t *v, int horizontal, int *pos,
                       int *frac);
/* Bring the first row back inside the document and the horizontal
 * position to 0 or more.  The second also stops the text TIKU_CV_SLACK
 * pixels past the end of the widest line, measuring every line not yet
 * measured. */
void tiku_cv_clamp(tiku_codeview_t *v);
void tiku_cv_clamp_across(tiku_codeview_t *v);

/* The widest line's width, measuring every line not yet measured; with
 * no room for the table, the widest of the rows shown. */
int64_t tiku_cv_widest(tiku_codeview_t *v);
/* Follow a change in the width table: lines @p first to @p last_old
 * became @p first to @p last_new, @p lines in all. */
void tiku_cv_widths_change(tiku_codeview_t *v, int64_t first,
                           int64_t last_old, int64_t last_new,
                           int64_t lines);
/* Free the width table. */
void tiku_cv_widths_free(tiku_codeview_t *v);

/* The coloured runs of @p line, @p n of them, or NULL when the line is
 * to be drawn in the document's ink alone.  A long line's runs may end
 * before the line does, but not before @p upto, the last byte that will
 * be drawn, and up to it they colour as the whole line's would.  Valid
 * until the next call. */
const tiku_span_t *tiku_cv_style_line(tiku_codeview_t *v, int64_t line,
                                      tiku_edit_pos_t upto, size_t *n);
/* Follow a document change, as tiku_cv_widths_change() does. */
void tiku_cv_style_change(tiku_codeview_t *v, int64_t first,
                          int64_t last_old, int64_t last_new,
                          int64_t lines);
/* Forget every line's state, as after a load. */
void tiku_cv_style_reset(tiku_codeview_t *v);
/* Free what colouring holds. */
void tiku_cv_style_free(tiku_codeview_t *v);

/* An editing key: 1 when the view took it. */
int tiku_cv_edit_key(tiku_codeview_t *v, const tiku_event_t *e);
/* Replace [@p lo, @p hi) with @p length bytes of @p text as one undo
 * step, joining typing when @p typing, the caret after the text (before a
 * CR the text leaves against an LF), and show it. */
tiku_edit_status_t tiku_cv_replace(tiku_codeview_t *v, tiku_edit_pos_t lo,
                                   tiku_edit_pos_t hi, const char *text,
                                   size_t length, int typing);
/* Where a word move goes from @p pos: left, back over blanks and then
 * over one run of word characters, punctuation or line breaks; right, on
 * over one run and then over blanks. */
tiku_edit_pos_t tiku_cv_word_left(const tiku_codeview_t *v,
                                  tiku_edit_pos_t pos);
/* The same to the right. */
tiku_edit_pos_t tiku_cv_word_right(const tiku_codeview_t *v,
                                   tiku_edit_pos_t pos);

/* Line commands (tiku_codeview_lines.c): the rules in force, and typing
 * @p text with a closer typed first on its line taking its opener's
 * indentation; 0 when @p text is no such closer and nothing was done. */
const tiku_codeview_rules_t *tiku_cv_rules(const tiku_codeview_t *v);
int tiku_cv_type_closer(tiku_codeview_t *v, const char *text);

/* A line's colouring read a byte at a time (tiku_codeview_brace.c). */
typedef struct {
    const tiku_span_t *runs;        /* the line's coloured runs         */
    size_t n;                       /* how many                         */
    size_t i;                       /* the run the last offset fell in  */
    int64_t start;                  /* where that run starts            */
} tiku_cv_inks_t;
/* The colouring of @p line, from its first byte through @p upto, valid
 * until a line is coloured again. */
tiku_cv_inks_t tiku_cv_inks(tiku_codeview_t *v, int64_t line,
                            tiku_edit_pos_t upto);
/* Whether the line's byte at offset @p at is code, not in quotes or a
 * remark; past the runs it is. */
int tiku_cv_code_at(tiku_cv_inks_t *k, int64_t at);

/* Brackets (tiku_codeview_brace.c): the bracket by the caret and its
 * match, found again only after a change or a caret move. */
tiku_edit_pos_t tiku_cv_brace(tiku_codeview_t *v, tiku_edit_pos_t *match);
/* The opener @p closer pairs with that is still open before @p pos; -1
 * for none and -2 when the search gave up. */
tiku_edit_pos_t tiku_cv_open_before(tiku_codeview_t *v, tiku_edit_pos_t pos,
                                    char closer);
/* Paint the pair behind the text of the first @p rows rows shown. */
void tiku_cv_brace_paint(tiku_codeview_t *v, tiku_surface_t *s,
                         int64_t rows);

/* Indicators and annotations (tiku_codeview_notes.c): the rows the
 * annotations under @p line take while they are shown. */
int64_t tiku_cv_ann_rows(const tiku_codeview_t *v, int64_t line);
/* The marks of the indicators over row @p w, drawn behind its text at
 * surface row @p y. */
void tiku_cv_marks_paint(tiku_codeview_t *v, tiku_surface_t *s,
                         const tiku_cv_row_t *w, int y);
/* Annotation row @p w drawn at surface row @p y. */
void tiku_cv_annotation_paint(tiku_codeview_t *v, tiku_surface_t *s,
                              const tiku_cv_row_t *w, int y);

/* One selection's change in a change across all of them: [start, end)
 * replaced by @c length bytes of @c text, the selection becoming a caret
 * @c caret bytes into the new text. */
typedef struct {
    tiku_edit_pos_t start;          /* the bytes replaced               */
    tiku_edit_pos_t end;
    const char *text;               /* what replaces them               */
    size_t length;
    size_t caret;                   /* the caret's place in it          */
} tiku_cv_piece_t;

/* Several selections (tiku_codeview_carets.c).  Whether the view has more
 * than one. */
int tiku_cv_many(const tiku_codeview_t *v);
/* The view's selections in order, read into a buffer the view keeps until
 * the next call: @p n of them, the main one at @p main; the main one alone
 * when there is no room. */
const tiku_edit_selection_t *tiku_cv_selections(tiku_codeview_t *v,
                                                size_t *n, size_t *main);
/* Apply @p n pieces, one for each selection in order, as one transaction
 * leaving a caret in each, the one at @p main the main one, and show it. */
tiku_edit_status_t tiku_cv_apply_pieces(tiku_codeview_t *v,
                                        const tiku_cv_piece_t *p, size_t n,
                                        size_t main, int typing);
/* Type @p length bytes of @p text over every selection. */
tiku_edit_status_t tiku_cv_insert_each(tiku_codeview_t *v, const char *text,
                                       size_t length, int typing);
/* Delete every selection, or at each caret the character, or with
 * @p words the word, before it or with @p forward after it; of two or
 * more carets one at least has something to take. */
tiku_edit_status_t tiku_cv_delete_each(tiku_codeview_t *v, int forward,
                                       int words);
/* Paste @p text over every selection: one line each when it has as many
 * lines as there are selections, else all of it each. */
tiku_edit_status_t tiku_cv_paste_each(tiku_codeview_t *v, const char *text,
                                      size_t length);
/* Offer the selections' text to the clipboard in order, a line break
 * between each two; TIKU_EDIT_NOTHING when none holds any. */
tiku_edit_status_t tiku_cv_copy_each(tiku_codeview_t *v);
/* Select the rectangle from the press's row and x to row @p sub of
 * @p line at row x @p x: on each text row between, from the place nearest
 * the one x to the place nearest the other, the pointer's row main. */
tiku_edit_status_t tiku_cv_rectangle(tiku_codeview_t *v, int64_t line,
                                     int64_t sub, int64_t x);
/* A Command press at @p pos: take away the selection there, when there
 * are several, or else add a caret there as the main one. */
tiku_edit_status_t tiku_cv_add_press(tiku_codeview_t *v, tiku_edit_pos_t pos);
/* The selection a Command press added, dragged to @p anchor, @p caret. */
tiku_edit_status_t tiku_cv_add_drag(tiku_codeview_t *v,
                                    tiku_edit_pos_t anchor,
                                    tiku_edit_pos_t caret);

/* Folds (tiku_codeview_folds.c): whether @p line heads a region, read
 * from its own text. */
int tiku_cv_fold_head(tiku_codeview_t *v, int64_t line);
/* Unfold the regions @p line is folded in. */
void tiku_cv_fold_reveal(tiku_codeview_t *v, int64_t line);
/* Follow a change to lines @p first to @p last: what it touched, and the
 * folded lines next to it, are shown. */
void tiku_cv_fold_changed(tiku_codeview_t *v, int64_t first, int64_t last);
/* The fold column's box for @p line, at surface row @p y. */
void tiku_cv_folds_paint(tiku_codeview_t *v, tiku_surface_t *s, int64_t line,
                         int y);
/* The mark after a folded line's text, at surface @p x, @p y. */
void tiku_cv_fold_mark(tiku_codeview_t *v, tiku_surface_t *s, int x, int y);
/* A press of @p e in the fold column: 1 when it was there. */
int tiku_cv_folds_press(tiku_codeview_t *v, const tiku_event_t *e);

/* Lists and tips (tiku_codeview_assist.c): an event they take, the check
 * after any event that a tip still belongs with the caret, painting them
 * over the text, and freeing them. */
int tiku_cv_assist_event(tiku_codeview_t *v, const tiku_event_t *e);
void tiku_cv_assist_follow(tiku_codeview_t *v);
void tiku_cv_assist_paint(tiku_codeview_t *v, tiku_surface_t *s);
void tiku_cv_assist_free(tiku_codeview_t *v);
/* A text-input event, given what staging made of it. */
int tiku_cv_edit_commit(tiku_codeview_t *v, int commit);

#endif /* TIKU_CODEVIEW_PRIVATE_H_ */
