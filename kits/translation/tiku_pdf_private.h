/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_private.h - what the PDF kit's files share: the object
 * model, the document, and the seams between lexer, cross-reference,
 * filters, pages and text.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PDF_PRIVATE_H_
#define TIKU_PDF_PRIVATE_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_pdf.h"

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
        struct { pdf_obj_t *dict; long off, len; } st;    /* len < 0: unknown */
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

struct tiku_pdf {
    unsigned char *data;            /* the whole file, NUL-terminated    */
    long           len;
    pdf_xref_t    *xref;            /* one entry per object number       */
    long           nxref;
    pdf_obj_t    **cache;           /* parsed indirect objects, by number */
    pdf_obj_t     *trailer;         /* the newest, with Root and Info    */
    pdf_objstm_t   objstm[PDF_OBJSTM_KEEP];  /* decoded object streams  */
    int            objstm_next;     /* the slot the next one takes       */
    pdf_obj_t    **page;            /* each page's dictionary, cached    */
    int            npage;
    int            repaired;        /* the table was rebuilt by scanning */
    int            encrypted;
    int            depth;           /* resolve() recursion, a cycle guard */
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
/** @brief The next @p word at or after @p from, or -1. */
long pdf_find(const unsigned char *p, long n, long from, const char *word);

/* The document, its table and its objects: tiku_pdf_doc.c                  */

/** @brief @p o with references followed; NULL for one that goes nowhere. */
pdf_obj_t *pdf_resolve(tiku_pdf_t *doc, pdf_obj_t *o);
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

#endif /* TIKU_PDF_PRIVATE_H_ */
