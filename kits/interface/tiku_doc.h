/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_doc.h - a documentation page, read and laid out, with no pixels.
 *
 * A help dialect of HTML becomes blocks of inline runs, and a width
 * becomes lines of PIECES: the run/line intersections, each one a byte
 * range with a measured x and width.  The piece table is the one thing
 * the painter draws from and the one thing the hit test searches, so a
 * link is where it was drawn.  What a page LOOKS like is not here: the
 * ink vocabulary belongs to the painter, and the words belong to the
 * application's strings.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DOC_H_
#define TIKU_DOC_H_

#include <stdint.h>

#include "tiku_geom.h"
#include "tiku_measure.h"

/*
 * The caps, each with its own #ifndef so a command-line override takes
 * the one it names and leaves the rest.  A table that fills sets its bit
 * and the read goes on: one link too many costs that link, not the rest
 * of the page.
 */
#ifndef TIKU_DOC_TEXT_MAX
#define TIKU_DOC_TEXT_MAX    16384  /* the page's words, entities decoded */
#endif
#ifndef TIKU_DOC_BLOCKS_MAX
#define TIKU_DOC_BLOCKS_MAX  256
#endif
#ifndef TIKU_DOC_RUNS_MAX
#define TIKU_DOC_RUNS_MAX    (TIKU_DOC_TEXT_MAX / 16)
#endif
#ifndef TIKU_DOC_LINES_MAX
#define TIKU_DOC_LINES_MAX   1024
#endif
#ifndef TIKU_DOC_PIECES_MAX
#define TIKU_DOC_PIECES_MAX  2048
#endif
#ifndef TIKU_DOC_LINKS_MAX
#define TIKU_DOC_LINKS_MAX   256
#endif
#ifndef TIKU_DOC_ANCHORS_MAX
#define TIKU_DOC_ANCHORS_MAX 64
#endif
#ifndef TIKU_DOC_IMAGES_MAX
#define TIKU_DOC_IMAGES_MAX  32
#endif
/* How deep the elements may nest while reading.  Parse-time only. */
#define TIKU_DOC_DEPTH_MAX   16

/** @brief Nothing: no link, no line, no block, no piece.  One spelling. */
#define TIKU_DOC_NONE (-1)

/** @brief What a block is, which decides its face, indent and spacing. */
typedef enum {
    TIKU_DOC_BLOCK_P = 0,
    TIKU_DOC_BLOCK_H1,
    TIKU_DOC_BLOCK_H2,
    TIKU_DOC_BLOCK_H3,
    TIKU_DOC_BLOCK_ITEM,        /* a list item; depth and index carried  */
    TIKU_DOC_BLOCK_PRE,
    TIKU_DOC_BLOCK_QUOTE,
    TIKU_DOC_BLOCK_RULE,
    TIKU_DOC_BLOCK_IMAGE,
    /*
     * What a cap cost, as a block of its own.  A page that was cut gets
     * one at the end, so the cut takes room in the line table and is
     * read back by anything that reads the window's words.  A bit a
     * caller may forget to look at is how a listing of 128 came to look
     * complete.
     */
    TIKU_DOC_BLOCK_NOTICE,
    TIKU_DOC_BLOCK_KINDS
} tiku_doc_block_kind_t;

/**
 * @brief What a run of words means, which the painter turns into ink.
 *
 * tiku_ink_t is the painter's, in tiku_gfx.h, which a file that compiles
 * for a board may not include.  EMPHASIS has no italic face to take:
 * the painter gives it an ink instead.
 */
typedef enum {
    TIKU_DOC_INK_PLAIN = 0,
    TIKU_DOC_INK_FAINT,         /* the notice, and an image's alt text   */
    TIKU_DOC_INK_CODE,
    TIKU_DOC_INK_EMPHASIS,
    TIKU_DOC_INK_LINK,
    TIKU_DOC_INK_INKS
} tiku_doc_ink_t;

/** @brief A list item's own marker, laid out in the hanging indent. */
#define TIKU_DOC_RUN_MARKER  0x01u
/** @brief This run ends its line, wherever it got to: a <br>. */
#define TIKU_DOC_RUN_BREAK   0x02u
/** @brief Spaces and newlines inside this run are the page's own. */
#define TIKU_DOC_RUN_PRE     0x04u

/** @brief An ordered list: the item's number is its own. */
#define TIKU_DOC_BLOCK_ORDERED 0x01u

/*
 * Which table filled, as bits.  tiku_doc_full_first() hands back the
 * first of them as an untranslated noun and the cap it hit, because the
 * sentence a person reads is the application's to write.
 */
#define TIKU_DOC_FULL_TEXT    0x001u
#define TIKU_DOC_FULL_BLOCKS  0x002u
#define TIKU_DOC_FULL_RUNS    0x004u
#define TIKU_DOC_FULL_LINES   0x008u
#define TIKU_DOC_FULL_PIECES  0x010u
#define TIKU_DOC_FULL_LINKS   0x020u
#define TIKU_DOC_FULL_ANCHORS 0x040u
#define TIKU_DOC_FULL_IMAGES  0x080u
#define TIKU_DOC_FULL_DEPTH   0x100u
/**
 * @brief A heading wanted a size the measuring end cannot answer.
 *
 * tiku_measure_width() resolves the three named faces and answers
 * anything else at the plain face's current size, and a board measures
 * from one baked table, so a heading laid out at one size and drawn at
 * another would overflow its box.  Headings are the bold face at the
 * body size until tiku_measure grows the display list's face ladder;
 * this bit says so rather than leaving it to be noticed.
 */
#define TIKU_DOC_FACE_FLAT    0x200u
/** @brief A word was broken between codepoints to fit a narrow box. */
#define TIKU_DOC_NARROW       0x400u

/**
 * @brief One stretch of one face, one ink and one link.
 *
 * @c off names a NUL-terminated string in the page's own text, so
 * nothing that reads a run can walk past its end.
 */
typedef struct {
    int32_t off;                /* into the page's text                  */
    int16_t len;
    int16_t link;               /* into the link table, or TIKU_DOC_NONE */
    uint8_t face;               /* a TIKU_MEASURE_* number               */
    uint8_t ink;                /* tiku_doc_ink_t                        */
    uint8_t flags;
    uint8_t spare;
} tiku_doc_run_t;

/** @brief A paragraph, a heading, an item: what the runs hang from. */
typedef struct {
    int32_t y;                  /* set by layout; the space above is in it */
    int16_t h;
    int16_t run0, nrun;
    int16_t line0, nline;       /* set by layout                         */
    int16_t index;              /* an ordered item's number, else 0      */
    int16_t image;              /* into the image table, or TIKU_DOC_NONE */
    uint8_t kind, depth, flags, spare;
} tiku_doc_block_t;

/**
 * @brief One run met by one line: the unit drawn, and the unit hit.
 *
 * The face, the ink and the link are read back through @c run rather
 * than copied here, since a copy is a thing that can come to disagree
 * with what it copied.  @c w was measured while laying out, in that
 * run's face, so a rectangle is arithmetic and never a second measure.
 */
typedef struct {
    int32_t off;                /* into the page's text: the bytes drawn */
    int16_t len;
    int16_t x;                  /* from the line's own x                 */
    int16_t w;
    int16_t run;
} tiku_doc_piece_t;

/** @brief A line of a block: where it sits and which pieces are on it. */
typedef struct {
    int32_t y;
    int16_t h, ascent;
    int16_t block;
    int16_t piece0, npiece;
    int16_t x, w;               /* the indent, and the advance used      */
} tiku_doc_line_t;

/** @brief A link, and every piece of it, which stay next to each other. */
typedef struct {
    int32_t href;               /* into the page's text                  */
    int16_t run0, nrun;
    int16_t piece0, npiece;     /* set by layout                         */
} tiku_doc_link_t;

/** @brief A name a link may reach, and the block wearing it. */
typedef struct {
    int32_t id;
    int16_t block, spare;
} tiku_doc_anchor_t;

/** @brief A picture: where it comes from, what it says, how big it is. */
typedef struct {
    int32_t src, alt;
    int16_t w, h;
    int16_t block, spare;
} tiku_doc_image_t;

/** @brief What a point landed on. */
typedef enum {
    TIKU_DOC_HIT_NONE = 0,
    TIKU_DOC_HIT_TEXT,
    TIKU_DOC_HIT_LINK,
    TIKU_DOC_HIT_IMAGE
} tiku_doc_hit_kind_t;

/** @brief A point, answered: what is there, and which of it. */
typedef struct {
    int kind;                   /* tiku_doc_hit_kind_t                   */
    int link;                   /* TIKU_DOC_NONE unless kind is LINK     */
    int block;
    int line;
    int piece;
} tiku_doc_hit_t;

/**
 * @brief Where a reader is, in terms a re-layout cannot move.
 *
 * A y is a number about one width.  A block and how far down it the
 * reader had got survives a resize and a change of text size, which is
 * what keeps a paragraph under the eye while a window is dragged.
 */
typedef struct {
    int block;
    int into;                   /* lines into that block                 */
} tiku_doc_place_t;

/**
 * @brief How big a picture is, asked of whoever can decode one.
 *
 * The interface kit may not read an image: the decoders are the
 * translation kit's, and this kit includes nothing outside itself.  A
 * caller that has them answers here; one that has not returns 0 and the
 * page draws the alt text in a frame.  The same @p src must be given
 * the same size throughout one layout, or the page drawn and the page
 * hit are two different pages.
 */
typedef int (*tiku_doc_image_fn)(void *ctx, const char *src,
                                 int *w, int *h);

typedef struct {
    char text[TIKU_DOC_TEXT_MAX];
    int  ntext;
    tiku_doc_block_t  block[TIKU_DOC_BLOCKS_MAX];
    tiku_doc_run_t    run[TIKU_DOC_RUNS_MAX];
    tiku_doc_line_t   line[TIKU_DOC_LINES_MAX];
    tiku_doc_piece_t  piece[TIKU_DOC_PIECES_MAX];
    tiku_doc_link_t   link[TIKU_DOC_LINKS_MAX];
    tiku_doc_anchor_t anchor[TIKU_DOC_ANCHORS_MAX];
    tiku_doc_image_t  image[TIKU_DOC_IMAGES_MAX];
    int nblock, nrun, nline, npiece, nlink, nanchor, nimage;
    int title;                  /* into the text, or TIKU_DOC_NONE       */
    unsigned full;              /* which caps filled, and how            */
    /* What the tables describe: the width they were laid for, and the
     * plain face's height when they were.  A layout asked for again at
     * both is the one already there. */
    int laid_width, laid_height;
    int height;
    tiku_doc_image_fn image_fn;
    void *image_ctx;
} tiku_doc_t;

/*
 * A page is around eighty kilobytes with these caps, so it is held
 * where a window's other state is not: through a pointer, not by value.
 * Every window controller in TikuTracker is embedded in the desktop's
 * own struct, which is already megabytes of stack.
 */

/** @brief An empty page: no blocks, no text, nothing laid out. */
void tiku_doc_init(tiku_doc_t *d);

/** @brief Say who can size a picture.  Both may be NULL. */
void tiku_doc_images(tiku_doc_t *d, tiku_doc_image_fn fn, void *ctx);

/**
 * @brief Read @p html into blocks and runs, and forget what was there.
 *
 * Entities are decoded, runs of blank space in flow content become one
 * space, and the space inside <pre> is the page's own.  A tag this
 * dialect does not know keeps its text and loses its effect; script,
 * style, head and comments lose both.
 *
 * @return 1 when the whole page fitted; 0 when a cap filled, which
 *         tiku_doc_full() then names.
 */
int tiku_doc_read(tiku_doc_t *d, const char *html);

/**
 * @brief Lay the page out for @p width, and say how tall it came to.
 *
 * Laying out again at the same width and text size is free.  Otherwise
 * it costs one pass over the text, measuring each word once in its own
 * face; a window that lays out while a drag resizes it should let the
 * drag settle first.
 */
int tiku_doc_layout(tiku_doc_t *d, int width);

/** @brief How tall the page is, and what width it was laid for. */
int tiku_doc_height(const tiku_doc_t *d);
int tiku_doc_width(const tiku_doc_t *d);
/** @brief Whether there is a layout to draw or hit at all. */
int tiku_doc_laid(const tiku_doc_t *d);

/** @brief Which caps filled, as TIKU_DOC_FULL_* bits. */
unsigned tiku_doc_full(const tiku_doc_t *d);

/**
 * @brief The first cap that filled, as a noun and the number it hit.
 *
 * The noun is untranslated and the application writes the sentence:
 * a kit that composed one would have started owning words.
 *
 * @return 1 when something filled.
 */
int tiku_doc_full_first(const tiku_doc_t *d, const char **what, int *cap);

/** @brief The first heading's words, or NULL. */
const char *tiku_doc_title(const tiku_doc_t *d);

int tiku_doc_block_count(const tiku_doc_t *d);
int tiku_doc_line_count(const tiku_doc_t *d);
int tiku_doc_piece_count(const tiku_doc_t *d);
const tiku_doc_block_t *tiku_doc_block(const tiku_doc_t *d, int i);
const tiku_doc_run_t   *tiku_doc_run(const tiku_doc_t *d, int i);
const tiku_doc_line_t  *tiku_doc_line(const tiku_doc_t *d, int i);
const tiku_doc_piece_t *tiku_doc_piece(const tiku_doc_t *d, int i);

/** @brief The text at @p off, or "" when there is none there. */
const char *tiku_doc_text(const tiku_doc_t *d, int off);

/** @brief Where a piece sits, in page coordinates.  The one answer. */
tiku_rect_t tiku_doc_piece_rect(const tiku_doc_t *d, int piece);
/** @brief Where a whole block sits: a ground, a rule, a frame. */
tiku_rect_t tiku_doc_block_rect(const tiku_doc_t *d, int block);

/** @brief The line at @p y, or TIKU_DOC_NONE above, below or between. */
int tiku_doc_line_at(const tiku_doc_t *d, int y);

/**
 * @brief What is at (@p x, @p y), in page coordinates.
 *
 * A point below the last line, in the ragged room at the end of a line,
 * or on a page with no layout is on nothing: never the piece that would
 * have been next.
 */
tiku_doc_hit_t tiku_doc_hit(const tiku_doc_t *d, int x, int y);

int tiku_doc_link_count(const tiku_doc_t *d);
const char *tiku_doc_link_href(const tiku_doc_t *d, int link);
/** @brief Every piece of a link, which lie together.  @return 1 when drawn. */
int tiku_doc_link_pieces(const tiku_doc_t *d, int link, int *first, int *n);
/**
 * @brief The link after @p from, or before it when @p back.
 *
 * TIKU_DOC_NONE asks for the first, or the last going back; the walk
 * wraps, so a keyboard reaches every link from wherever it starts.
 */
int tiku_doc_link_next(const tiku_doc_t *d, int from, int back);

/** @brief Where a name is, or TIKU_DOC_NONE when the page has none. */
int tiku_doc_anchor_y(const tiku_doc_t *d, const char *id);

/** @brief Hold a reading position across a re-layout. */
tiku_doc_place_t tiku_doc_place(const tiku_doc_t *d, int y);
int tiku_doc_place_y(const tiku_doc_t *d, tiku_doc_place_t at);

/**
 * @brief Walk the tables and say whether they agree.  For tests.
 *
 * Every piece inside its run, every line's pieces inside the table,
 * every block's lines inside it, and y never going backwards, which is
 * what the search for a line stands on.
 *
 * @return 1 when they hold.
 */
int tiku_doc_check(const tiku_doc_t *d);

#endif /* TIKU_DOC_H_ */
