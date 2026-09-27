/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_pdf_crypt.c - encrypted PDFs made here, as a writer makes them,
 * and read back: RC4 at 40 and 128 bits (revisions 2 and 3), AES-128
 * (4) and AES-256 (6); the empty password, a user's, an owner's, a
 * wrong one; the author's word on copying; and a damaged file.
 *
 * TIKU_PDF_ENC_DIR names a folder to write the files to, so another
 * reader (poppler's pdftotext) can be asked to open them too.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_crypto.h"
#include "tiku_pdf.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

#include "../fixtures/pdf_crypt.inc"

/* -------------------------------------------------------------- tests  */

/** @brief Open @p e's file with @p pw; @return the document, or NULL
 *         with @p err set. */
static tiku_pdf_t *
open_with(const encw_t *b, const char *pw, tiku_pdf_err_t *err)
{
    tiku_pdf_t *doc = NULL;

    *err = tiku_pdf_decode_password(b->p, (size_t)b->n, pw, &doc);
    return doc;
}

/** @brief Whether @p doc says its secrets: the page's text, the title. */
static int
reads(tiku_pdf_t *doc)
{
    char *text = doc != NULL ? tiku_pdf_page_text(doc, 0) : NULL;
    char *title = doc != NULL ? tiku_pdf_title(doc) : NULL;
    int ok = text != NULL && strstr(text, "Secret text") != NULL &&
             title != NULL && strcmp(title, "Hidden title") == 0;

    free(text);
    free(title);
    return ok;
}

/** @brief One fixture: opened as its README of rows says, then kept for
 *         another reader when TIKU_PDF_ENC_DIR asks. */
static void
fixture(const enc_t *e)
{
    encw_t b = enc_make(e);
    const char *dir = getenv("TIKU_PDF_ENC_DIR");
    tiku_pdf_err_t err;
    tiku_pdf_t *doc;
    char what[160];

    if (dir != NULL) {
        char path[512];
        FILE *f;

        snprintf(path, sizeof path, "%s/%s.pdf", dir, e->name);
        f = fopen(path, "wb");
        if (f != NULL) {
            fwrite(b.p, 1, (size_t)b.n, f);
            fclose(f);
        }
    }
    doc = open_with(&b, e->user, &err);
    snprintf(what, sizeof what, "%s: the user's password opens it and "
             "its text and title read", e->name);
    check(err == TIKU_PDF_OK && reads(doc), "ENC-001", what);
    snprintf(what, sizeof what, "%s: copying is as its P says",
             e->name);
    check(doc != NULL && tiku_pdf_may_copy(doc) == ((e->p & 16) != 0),
          "ENC-002", what);
    tiku_pdf_close(doc);
    doc = open_with(&b, e->owner, &err);
    snprintf(what, sizeof what, "%s: so does the owner's, which may "
             "always copy", e->name);
    check(err == TIKU_PDF_OK && reads(doc) && tiku_pdf_may_copy(doc),
          "ENC-003", what);
    tiku_pdf_close(doc);
    if (e->user[0] != '\0') {
        doc = NULL;
        err = tiku_pdf_decode(b.p, (size_t)b.n, &doc);
        snprintf(what, sizeof what, "%s: without its password it asks for "
                 "one", e->name);
        check(err == TIKU_PDF_PASSWORD && doc == NULL, "ENC-004", what);
        doc = open_with(&b, "wrong", &err);
        snprintf(what, sizeof what, "%s: and a wrong one does not open it",
                 e->name);
        check(err == TIKU_PDF_PASSWORD && doc == NULL, "ENC-005", what);
    }
    free(b.p);
}

/** @brief Overwrite the first @p from in @p b with @p to, which is as
 *         long.  @return whether @p from was there. */
static int
patch(encw_t *b, const char *from, const char *to)
{
    size_t n = strlen(from);
    long i;

    for (i = 0; i + (long)n <= b->n; i++) {
        if (memcmp(b->p + i, from, n) == 0) {
            memcpy(b->p + i, to, n);
            return 1;
        }
    }
    return 0;
}

/** @brief A damaged file: a table that lies about the Encrypt
 *         dictionary, which is found by reading the file. */
static void
damaged(void)
{
    static const enc_t rc4 = {"rc4-128", 2, 3, 128, 0, "", "owner", -20};
    encw_t b = enc_make(&rc4);
    tiku_pdf_err_t err;
    tiku_pdf_t *doc;
    char row[32];
    int ok;

    snprintf(row, sizeof row, "%010ld 00000 n", b.off[6]);
    ok = patch(&b, row, "0000000001 00000 n");
    doc = open_with(&b, "", &err);
    check(ok && err == TIKU_PDF_OK && reads(doc), "ENC-006",
          "a table that lies about the Encrypt dictionary is rebuilt, and "
          "the file still reads");
    tiku_pdf_close(doc);
    free(b.p);
}

int
main(void)
{
    static const enc_t fixtures[] = {
        {"rc4-40", 1, 2, 40, 0, "", "owner", -4},
        {"rc4-128", 2, 3, 128, 0, "", "owner", -20},
        {"aes-128", 4, 4, 128, 1, "", "owner", -4},
        {"aes-128-pw", 4, 4, 128, 1, "secret", "owner", -20},
        {"aes-256", 5, 6, 256, 1, "", "owner", -20},
        {"aes-256-pw", 5, 6, 256, 1, "secret", "boss", -4},
    };
    size_t i;

    printf("Encrypted PDFs, made here and read back\n");
    for (i = 0; i < sizeof fixtures / sizeof fixtures[0]; i++) {
        fixture(&fixtures[i]);
    }
    damaged();
    printf("%s\n", fails ? "FAIL" : "PASS: RC4 40/128, AES 128/256, "
                                     "passwords and permissions");
    return fails ? 1 : 0;
}
