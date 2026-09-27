/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_private.h - what the PDF kit's files share: the object
 * model, the document, and the seams between lexer, cross-reference,
 * filters, pages, text, and the drawing side: raster, colour, glyphs,
 * images and the interpreter that runs a page through them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PDF_PRIVATE_H_
#define TIKU_PDF_PRIVATE_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_pdf.h"
#include "tiku_cff.h"
#include "tiku_ttf.h"
#include "tiku_type1.h"

/* The file as a whole, and any one decoded stream. */
#define PDF_FILE_MAX    (256L * 1024L * 1024L)
#define PDF_STREAM_MAX  (64L * 1024L * 1024L)
#define PDF_NEST_MAX    64          /* arrays in dicts in arrays ...     */
#define PDF_OBJECTS_MAX 4000000     /* a /Size past this is a lie        */
#define PDF_PAGES_MAX   20000

typedef enum {
    PDF_NULL, PDF_BOOL, PDF_INT, PDF_REAL, PDF_STRING, PDF_NAME,
    PDF_ARRAY, PDF_DICT, PDF_REF, PDF_STREAM, PDF_OP
} pdf_kind_t;

typedef struct pdf_obj pdf_obj_t;

/**
 * @brief One object, of any kind.
 *
 * Strings and names own their bytes; a name is kept without its slash
 * and with #xx pairs resolved.  A stream is its dictionary and where its
 * raw bytes lie in the file (a stream is never inside an object stream).
 * An operator (PDF_OP) only occurs while reading a content stream.
 */
struct pdf_obj {
    pdf_kind_t kind;                /* which member of u is live         */
    union {
        int    b;                   /* PDF_BOOL                          */
        long   i;                   /* PDF_INT                           */
        double r;                   /* PDF_REAL                          */
        struct { unsigned char *p; size_t n; } s;         /* string, name, op */
        struct { pdf_obj_t **item; int n; } a;
        struct { char **key; pdf_obj_t **val; int n; } d;
        struct { long num, gen; } ref;
        struct { pdf_obj_t *dict; long off, len, num, gen; } st;
                                    /* len < 0 unknown; num < 0 unnumbered */
    } u;
};

/** @brief Where an indirect object lives, from the cross-reference. */
typedef struct {
    long off;                       /* file offset, or the object stream */
    long idx;                       /* index within that stream          */
    unsigned char kind;             /* 0 free, 1 in file, 2 in a stream  */
    unsigned char set;              /* an entry has been read for it     */
    unsigned char seen;             /* set while parsing: a cycle guard  */
} pdf_xref_t;

/** @brief One object stream, decoded once and kept while it is used. */
typedef struct {
    long           num;             /* the stream's object number        */
    unsigned char *data;            /* decoded, or NULL for a free slot  */
    long           len, first, count;   /* bytes; /First; /N            */
} pdf_objstm_t;

#define PDF_OBJSTM_KEEP 8

typedef struct pdf_crypt pdf_crypt_t;

/** @brief A page's dictionary and its number, sorted by address. */
typedef struct {
    pdf_obj_t *dict;                /* the page's dictionary, cached     */
    int        index;               /* its number, from 0                */
} pdf_pageref_t;

struct tiku_pdf {
    unsigned char *data;            /* the whole file, NUL-terminated    */
    long           len;
    pdf_xref_t    *xref;            /* one entry per object number       */
    long           nxref;
    pdf_obj_t    **cache;           /* parsed indirect objects, by number */
    pdf_obj_t     *trailer;         /* the newest, with Root and Info    */
    pdf_obj_t     *old_trailer;     /* the one before a repair, kept     */
    pdf_obj_t    **grave;           /* the cache before a repair, kept   */
    long           ngrave;
    pdf_objstm_t   objstm[PDF_OBJSTM_KEEP];  /* decoded object streams  */
    int            objstm_next;     /* the slot the next one takes       */
    pdf_obj_t    **page;            /* each page's dictionary, cached    */
    int            npage;
    pdf_pageref_t *pageref;         /* the same, by object; made on use  */
    int            repaired;        /* the table was rebuilt by scanning */
    int            encrypted;
    pdf_crypt_t   *crypt;           /* its key, when it is encrypted     */
    int            depth;           /* resolve() recursion, a cycle guard */
    char           fontdir[512];    /* faces to stand in for fonts     */
    tiku_ttf_t    *subst[8];        /* the stand-ins opened so far      */
    char           subst_path[8][512];  /* each one's file             */
    int            nsubst;
    char           subst_pick[6][512];  /* sans, serif, fixed; x bold  */
    unsigned char  subst_chosen[6]; /* that pick was made ("" is none)  */
};

/* The lexer and the objects it makes: tiku_pdf_lex.c                        */

/** @brief A cursor over bytes; the lexer and parser read through one. */
typedef struct {
    const unsigned char *p;         /* the bytes                         */
    long                 n, pos;    /* how many, and where the reader is */
} pdf_cur_t;

/** @brief A new object of @p kind, empty; NULL when memory is short. */
pdf_obj_t *pdf_new(pdf_kind_t kind);
/** @brief Free @p o and everything it holds; a stream's dictionary too. */
void pdf_free(pdf_obj_t *o);
/**
 * @brief One object at the cursor, or NULL at the end or on a failure.
 *
 * A keyword the syntax has no place for (an operator in a content
 * stream, "endobj", "stream") comes back as PDF_OP for the caller to
 * judge; the cursor is always moved on, so a caller never loops.
 */
pdf_obj_t *pdf_parse(pdf_cur_t *c, int depth);
/** @brief Skip white space and comments.  @return 0 at the end. */
int pdf_skip_ws(pdf_cur_t *c);
/** @brief Whether @p word stands whole at the cursor. */
int pdf_keyword_at(const pdf_cur_t *c, const char *word);
/** @brief Set @p key of @p d to @p val, which @p d now owns; -1 when full. */
int pdf_dict_set(pdf_obj_t *d, const char *key, pdf_obj_t *val);
/** @brief The value under @p key, unresolved; NULL when absent. */
pdf_obj_t *pdf_dict_raw(const pdf_obj_t *d, const char *key);
/** @brief Append @p item to @p a, which owns it from here; -1 when full. */
int pdf_array_push(pdf_obj_t *a, pdf_obj_t *item);
/** @brief Whether @p o is the name @p name. */
int pdf_is_name(const pdf_obj_t *o, const char *name);
/** @brief @p o as a number, or @p dflt when it is not one. */
double pdf_number(const pdf_obj_t *o, double dflt);
/** @brief @p o as an int, or @p dflt when it is not a number or is one
 *         no int holds: a count or a size that large is damage. */
int pdf_int(const pdf_obj_t *o, int dflt);
/** @brief The next @p word at or after @p from, or -1. */
long pdf_find(const unsigned char *p, long n, long from, const char *word);

/* The document, its table and its objects: tiku_pdf_doc.c                  */

/** @brief @p o with references followed; NULL for one that goes nowhere. */
pdf_obj_t *pdf_resolve(tiku_pdf_t *doc, pdf_obj_t *o);
/** @brief A text string (a title, an outline entry) as UTF-8 the caller
 *        frees: UTF-16 or UTF-8 with its mark, else PDFDocEncoding. */
char *pdf_text_string(const pdf_obj_t *s);
/** @brief The value under @p key, resolved; NULL when absent. */
pdf_obj_t *pdf_get(tiku_pdf_t *doc, pdf_obj_t *dict, const char *key);
/**
 * @brief Indirect object @p num, parsed once and kept by the document.
 *
 * A table that points at something else is rebuilt from the file once,
 * and the object asked for again.
 */
pdf_obj_t *pdf_object(tiku_pdf_t *doc, long num);
/** @brief The catalogue, or NULL. */
pdf_obj_t *pdf_root(tiku_pdf_t *doc);
/**
 * @brief The raw bytes of @p stream in the file.
 *
 * Length may be indirect and may lie; when "endstream" is not where it
 * says, the keyword is searched for from the start of the data and the
 * bytes before it are the stream.
 */
int pdf_stream_raw(tiku_pdf_t *doc, pdf_obj_t *stream,
                   const unsigned char **p, long *n);

/* The filters: tiku_pdf_filter.c                                            */

/**
 * @brief The decoded bytes of @p stream, through every standard filter.
 *
 * An image filter (DCT, CCITT, JPX, JBIG2) ends the chain: what is left
 * is returned as it stands and @p image names the filter; NULL when the
 * chain ended in bytes.  The caller frees @p out.
 */
int pdf_stream_data(tiku_pdf_t *doc, pdf_obj_t *stream, unsigned char **out,
                    long *n, const char **image);
/** @brief The same for @p raw bytes that @p dict describes (an inline
 *        image: its filters may go by their short names). */
int pdf_filter_bytes(tiku_pdf_t *doc, pdf_obj_t *dict,
                     const unsigned char *raw, long rawn, unsigned char **out,
                     long *n, const char **image);

/* Encryption: tiku_pdf_crypt.c                                             */

/**
 * @brief The standard security handler for @p doc with @p password
 *        (UTF-8; "" is the empty password most documents use).
 * @return 0 with doc->crypt set; -1 for a handler or cipher this does
 *         not read; -2 when the password opens it neither as its user
 *         nor as its owner.
 */
int pdf_crypt_open(tiku_pdf_t *doc, const char *password);
void pdf_crypt_free(pdf_crypt_t *c);
/** @brief Whether the author lets text be copied out (P bit 5), or the
 *         owner's password opened it; 1 for a document not encrypted. */
int pdf_crypt_may_copy(const pdf_crypt_t *c);
/** @brief The strings of object @p num @p gen, as read, decrypted. */
void pdf_crypt_object(tiku_pdf_t *doc, pdf_obj_t *o, long num, long gen);
/** @brief Stream @p st's @p n raw bytes decrypted in place.  @return how
 *         many remain: AES drops its IV and padding. */
long pdf_crypt_stream(tiku_pdf_t *doc, pdf_obj_t *st, unsigned char *data,
                      long n);

/* The pages: tiku_pdf_page.c                                                */

/** @brief Walk the page tree into the document's page list. */
int pdf_pages_build(tiku_pdf_t *doc);
/** @brief The dictionary of @p page (from 0), or NULL. */
pdf_obj_t *pdf_page_dict(tiku_pdf_t *doc, int page);
/** @brief @p key on @p page or the nearest ancestor that has it. */
pdf_obj_t *pdf_page_inherited(tiku_pdf_t *doc, pdf_obj_t *page,
                              const char *key);
/** @brief The page's content streams decoded and joined; caller frees. */
int pdf_page_content(tiku_pdf_t *doc, pdf_obj_t *page, unsigned char **out,
                     long *n);
/** @brief Which page @p page (a reference, dictionary or number) is, or
 *        -1 for none of this document's. */
int pdf_page_index(tiku_pdf_t *doc, pdf_obj_t *page);

/* Fonts, as far as their text goes: tiku_pdf_font.c                        */

/** @brief A font as text extraction needs it: codes to text and widths. */
typedef struct {
    int       two_byte;             /* codes are two bytes (CID fonts)    */
    int       type3;
    double    missing;              /* width of a code without one       */
    double    matrix_x;             /* Type 3: FontMatrix's x scale      */
    uint32_t *uni;                  /* code -> Unicode, 0 for none       */
    uint32_t *uni2;                 /* a second character (a ligature)   */
    double   *width;                /* code -> advance in text space, -1 */
    long      codes;                /* 256 or 65536 entries              */
} pdf_tfont_t;

/** @brief Read @p font's mapping and widths; NULL when memory is short. */
pdf_tfont_t *pdf_tfont_load(tiku_pdf_t *doc, pdf_obj_t *font);
/** @brief Free what pdf_tfont_load() gave. */
void pdf_tfont_free(pdf_tfont_t *f);
/** @brief The character a glyph name stands for, or 0 for none known. */
uint32_t pdf_glyph_unicode(const char *name);

/* The text of a page: tiku_pdf_text.c holds tiku_pdf_page_text()          */

/* Drawing: tiku_pdf_raster.c fills and strokes, tiku_pdf_render.c reads   */

/** @brief A matrix as PDF writes one: [a b c d e f], row vectors. */
typedef struct { double a, b, c, d, e, f; } pdf_mat_t;

/** @brief @p m then @p n, as "cm" composes them; and one from its six. */
pdf_mat_t pdf_mat_mul(pdf_mat_t m, pdf_mat_t n);
pdf_mat_t pdf_mat(double a, double b, double c, double d, double e,
                  double f);
/** @brief The point (x, y) through @p m. */
void pdf_mat_apply(const pdf_mat_t *m, double x, double y, double *ox,
                   double *oy);
/** @brief How much @p m scales a length, on average. */
double pdf_mat_scale(const pdf_mat_t *m);
/**
 * @brief The matrix laying @p page out at @p scale, rotation applied and
 *        y downwards, with the window's corner (@p x0, @p y0) at (0, 0);
 *        and the whole page's size in pixels.  Text boxes and links use
 *        it at scale 1: the page as drawn, in points.
 */
int pdf_page_matrix(tiku_pdf_t *doc, int page, double scale, int x0, int y0,
                    pdf_mat_t *m, int *w, int *h);

/** @brief A page's pixels, 0xAARRGGBB, and the blend mode paints use on
 *         them now; and a coverage mask over them. */
typedef struct { int w, h; uint32_t *px; int blend; } pdf_pix_t;
typedef struct { int x0, y0, w, h; unsigned char *a; } pdf_mask_t;

/**
 * @brief A path flattened to straight pieces in device space.
 *
 * Subpaths are runs of points; a closed one is marked, and an open one
 * is closed when it is filled, as PDF fills it.
 */
typedef struct {
    float *pt;                      /* x, y pairs                        */
    int    n, cap;                  /* points, room                      */
    int   *start;                   /* where each subpath begins         */
    int   *closed;                  /* per subpath                       */
    int    nsub, subcap;
    float  cx, cy;                  /* the current point                 */
    float  sx, sy;                  /* the subpath's first point         */
    int    failed;
} pdf_path_t;

/** @brief A path's life: made empty, emptied again, given back. */
void pdf_path_init(pdf_path_t *p);
void pdf_path_clear(pdf_path_t *p);
void pdf_path_free(pdf_path_t *p);
/** @brief The builders, in device space: a new subpath, a line, a cubic
 *        flattened as its size needs, and the subpath shut. */
void pdf_path_moveto(pdf_path_t *p, double x, double y);
void pdf_path_lineto(pdf_path_t *p, double x, double y);
void pdf_path_curveto(pdf_path_t *p, double x1, double y1, double x2,
                      double y2, double x3, double y3);
/** @brief Shut the subpath: a line back to where it began. */
void pdf_path_close(pdf_path_t *p);
/** @brief The path's extent in whole pixels, clipped to @p w by @p h. */
int pdf_path_bounds(const pdf_path_t *p, int w, int h, int *x0, int *y0,
                    int *bw, int *bh);

/**
 * @brief Coverage of @p p within the box, by @p evenodd or nonzero.
 * @return a mask the caller frees with pdf_mask_free(), or a mask with
 *         no bytes when nothing is covered.
 */
pdf_mask_t pdf_path_fill(const pdf_path_t *p, int evenodd, int x0, int y0,
                         int w, int h);
/** @brief The outline of stroking @p p, as a path to fill nonzero. */
void pdf_path_stroke(const pdf_path_t *p, double width, int cap, int join,
                     double miter, const double *dash, int ndash,
                     double phase, pdf_path_t *out);
/** @brief Give a mask's bytes back; one with none is fine. */
void pdf_mask_free(pdf_mask_t *m);
/** @brief @p m narrowed to what @p clip also covers (clip may be NULL). */
void pdf_mask_intersect(pdf_mask_t *m, const pdf_mask_t *clip);
/** @brief A mask over the whole of @p w by @p h, all covered. */
pdf_mask_t pdf_mask_full(int w, int h);
/** @brief Paint @p colour through @p m (and @p clip) at @p alpha. */
void pdf_paint(pdf_pix_t *pix, const pdf_mask_t *m, const pdf_mask_t *clip,
               uint32_t colour, double alpha);
/** @brief The blend modes (ISO 32000 11.3.5), Normal first. */
enum { PDF_BM_NORMAL, PDF_BM_MULTIPLY, PDF_BM_SCREEN, PDF_BM_OVERLAY,
       PDF_BM_DARKEN, PDF_BM_LIGHTEN, PDF_BM_DODGE, PDF_BM_BURN,
       PDF_BM_HARDLIGHT, PDF_BM_SOFTLIGHT, PDF_BM_DIFFERENCE,
       PDF_BM_EXCLUSION, PDF_BM_HUE, PDF_BM_SATURATION, PDF_BM_COLOR,
       PDF_BM_LUMINOSITY };
/** @brief The mode named @p name; Normal for a name that is not one. */
int pdf_blend_mode(const char *name);
/** @brief @p colour laid over pixel @p d of @p pix with coverage @p a,
 *         0 to 255, through the pixmap's blend mode. */
void pdf_blend(const pdf_pix_t *pix, uint32_t *d, uint32_t colour, int a);

/* Shadings: tiku_pdf_shade.c                                                */

typedef struct pdf_shade pdf_shade_t;
/** @brief Shading @p dict, its colour space named in @p res: types 1 to 3
 *        drawn, the meshes as their Background. */
pdf_shade_t *pdf_shade_load(tiku_pdf_t *doc, pdf_obj_t *dict, pdf_obj_t *res);
void pdf_shade_free(pdf_shade_t *s);
/**
 * @brief Paint @p s, its space taken to the device by @p to, through
 *        coverage @p m, or over all @p clip leaves when @p m is NULL.
 *
 * With @p background its Background fills what the shading leaves out,
 * as a pattern's fill wants and sh does not.
 */
void pdf_shade_paint(pdf_pix_t *pix, const pdf_shade_t *s,
                     const pdf_mat_t *to, const pdf_mask_t *m,
                     const pdf_mask_t *clip, double alpha, int background);

/* Glyphs: tiku_pdf_glyph.c                                                  */

enum { GF_NONE, GF_TTF, GF_CFF, GF_T1, GF_T3 };
/** @brief A font as drawing needs it: its program, and how a code
 *        reaches a glyph in it. */
typedef struct {
    int            kind;            /* GF_TTF, GF_CFF, GF_T1, GF_T3       */
    tiku_ttf_t    *ttf;             /* TrueType, OpenType, or a stand-in  */
    tiku_cff_t    *cff;             /* a bare CFF (FontFile3)             */
    tiku_t1_t     *t1;              /* Type 1 (FontFile)                  */
    unsigned char *blob;            /* the program's bytes the CFF borrows */
    long           bloblen;
    int            two_byte, symbolic, substitute, owns_ttf;
    char          *names[256];      /* a simple font's code to glyph name */
    int            names_from_pdf;  /* the PDF gave an Encoding           */
    const uint32_t *uni;            /* code to character, borrowed        */
    const double  *width;           /* code to advance, borrowed          */
    unsigned short *cid2gid;        /* a CID font's map, NULL: identity   */
    long           ncid2gid;
    double         upem;            /* the program's units per em         */
    pdf_obj_t     *charprocs, *t3res;   /* Type 3: its glyph programs    */
    pdf_mat_t      fontmatrix;
} pdf_gfont_t;

/** @brief Font @p dict as drawn, borrowing its text view @p tf. */
pdf_gfont_t *pdf_gfont_load(tiku_pdf_t *doc, pdf_obj_t *dict,
                            const pdf_tfont_t *tf);
/** @brief Given back; the borrowed text view is left alone. */
void pdf_gfont_free(pdf_gfont_t *g);
/**
 * @brief Widths for a simple font that gives none (the standard 14 as
 *        old files use them): the stand-in's advances, which faces made
 *        metric-compatible with those 14 make the right ones.
 * @return 1 when any were set into @p width, by code through @p uni.
 */
int pdf_standin_widths(tiku_pdf_t *doc, pdf_obj_t *font, const uint32_t *uni,
                       double *width);
/**
 * @brief Coverage of glyph @p code drawn through @p m, which takes text
 *        space (one unit the em) to the device.
 * @return 1 with @p out to free with pdf_mask_free(), 0 for no ink.
 */
int pdf_gfont_mask(pdf_gfont_t *g, long code, const pdf_mat_t *m,
                   pdf_mask_t *out);

/* Images: tiku_pdf_image.c                                                  */

/** @brief An image decoded to what drawing needs: colour per sample, an
 *        alpha per sample when it has one, or a stencil alone. */
typedef struct {
    int            w, h;
    unsigned char *rgb;             /* w*h*3; NULL for a stencil        */
    unsigned char *alpha;           /* w*h, or NULL when opaque         */
    int            stencil;         /* an ImageMask: alpha is the shape */
} pdf_img_t;

/**
 * @brief Decode the image @p dict describes: an XObject stream, or with
 *        @p raw an inline image's bytes.
 *
 * What cannot be decoded (a fax, a JPEG 2000, a progressive JPEG)
 * comes back as a grey box of the right size, so the page keeps its
 * layout.  @return 0, or -1 when there is no image to draw.
 */
int pdf_image_decode(tiku_pdf_t *doc, pdf_obj_t *dict,
                     const unsigned char *raw, long rawn, pdf_obj_t *res,
                     pdf_img_t *out);
/** @brief Give a decoded image's samples back. */
void pdf_image_free(pdf_img_t *img);
/**
 * @brief Draw @p img over the unit square that @p ctm places, through
 *        @p clip at @p alpha; a stencil paints @p fill.
 */
void pdf_image_draw(pdf_pix_t *pix, const pdf_img_t *img,
                    const pdf_mat_t *ctm, const pdf_mask_t *clip,
                    uint32_t fill, double alpha);

/* Colour: tiku_pdf_color.c                                                  */

enum { CS_GRAY = 1, CS_RGB, CS_CMYK, CS_INDEXED, CS_SEP, CS_LAB, CS_PATTERN };
#define PDF_FUNC_MAX_IN  8
#define PDF_FUNC_MAX_OUT 8

typedef struct pdf_func pdf_func_t;
/** @brief A colour space: its kind, how many numbers a colour is, and
 *        for Indexed and Separation what those numbers go through. */
typedef struct pdf_cs {
    int            kind, n;
    struct pdf_cs *base;            /* Indexed and Separation: the target */
    unsigned char *lookup;          /* Indexed: hival+1 base colours     */
    int            hival;
    pdf_func_t    *tint;            /* Separation: tint transform        */
    double         initial[4];      /* the colour set with cs/CS         */
} pdf_cs_t;

/** @brief The colour space @p spec names or defines; @p res answers a
 *        name of the page's own.  NULL for one this cannot use. */
pdf_cs_t *pdf_cs_load(tiku_pdf_t *doc, pdf_obj_t *spec, pdf_obj_t *res);
void pdf_cs_free(pdf_cs_t *cs);
/** @brief The device colour for @p v, @p n numbers in @p cs. */
uint32_t pdf_cs_colour(const pdf_cs_t *cs, const double *v, int n);
/** @brief One sample of @p bpc bits at @p i, mapped to 0..255 per part. */
void pdf_cs_sample(const pdf_cs_t *cs, const unsigned char *s, int bpc,
                   long i, const double *decode, unsigned char *rgb);

/** @brief A PDF function (ISO 32000 7.10), of any of the four types,
 *        or an array of them side by side. */
pdf_func_t *pdf_func_load(tiku_pdf_t *doc, pdf_obj_t *spec);
void pdf_func_free(pdf_func_t *f);
/** @brief @p f at @p in; @return how many outputs were written. */
int pdf_func_eval(const pdf_func_t *f, const double *in, int nin,
                  double *out);

#endif /* TIKU_PDF_PRIVATE_H_ */
