/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_pdf_crypt.c - encrypted PDFs made here, as a writer makes them,
 * and read back: RC4 at 40 and 128 bits (revisions 2 and 3), AES-128
 * (4) and AES-256 (6); the empty password, a user's, an owner's, a
 * wrong one; the author's word on copying; passwords typed otherwise
 * than they were hashed (SASLprep, PDFDocEncoding); two damaged files.
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

/**
 * @brief Passwords typed otherwise than the writer hashed them: for
 *        AES-256 what SASLprep makes the same (7.6.4.3.3), and one a
 *        writer hashed as typed; for revisions 3 and 4 PDFDocEncoding's
 *        bytes (7.6.4.3.1), and UTF-8 a writer took as it was.
 */
static void
typed(void)
{
    static const struct {
        enc_t       e;              /* as written: its passwords, hashed */
        const char *typed;          /* what is typed to open it          */
        int         owner;          /* it is the owner's                 */
        const char *what;
    } cases[] = {
        {{"sasl-hyphen", 5, 6, 256, 1, "IX", "boss", -20}, "I\xC2\xADX", 0,
         "AES-256: a soft hyphen in a password is nothing to SASLprep"},
        {{"sasl-nine", 5, 6, 256, 1, "IX", "boss", -20}, "\xE2\x85\xA8", 0,
         "a Roman numeral nine is I and X to it"},
        {{"sasl-wide", 5, 6, 256, 1, "Pass", "boss", -20},
         "\xEF\xBC\xB0\xEF\xBD\x81\xEF\xBD\x93\xEF\xBD\x93", 0,
         "full-width letters are the letters"},
        {{"sasl-mark", 5, 6, 256, 1, "caf\xC3\xA9", "boss", -20},
         "cafe\xCC\x81", 0,
         "an accent typed after its letter is the accented letter"},
        {{"sasl-owner", 5, 6, 256, 1, "user", "IX", -20}, "\xE2\x85\xA8", 1,
         "and an owner's password is prepared alike"},
        {{"sasl-raw", 5, 6, 256, 1, "\xE2\x85\xA8", "boss", -20},
         "\xE2\x85\xA8", 0,
         "a password a writer hashed as typed still opens as typed"},
        {{"docenc-accent", 4, 4, 128, 1, "caf\xE9", "owner", -20},
         "caf\xC3\xA9", 0,
         "AES-128: an accent typed in UTF-8 is hashed in PDFDocEncoding"},
        {{"docenc-euro", 2, 3, 128, 0, "\xA0" "5", "owner", -20},
         "\xE2\x82\xAC" "5", 0,
         "RC4: PDFDocEncoding's euro is its own byte, not Latin-1's"},
        {{"docenc-utf8", 4, 4, 128, 1, "caf\xC3\xA9", "owner", -20},
         "caf\xC3\xA9", 0,
         "a password a writer took as UTF-8 still opens as typed"},
    };
    static const enc_t nine = {"sasl-nine", 5, 6, 256, 1, "IX", "boss", -20};
    tiku_pdf_err_t err;
    tiku_pdf_t *doc;
    encw_t b;
    size_t i;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        b = enc_make(&cases[i].e);
        doc = open_with(&b, cases[i].typed, &err);
        check(err == TIKU_PDF_OK && reads(doc) &&
              tiku_pdf_may_copy(doc) == cases[i].owner, "ENC-008",
              cases[i].what);
        tiku_pdf_close(doc);
        free(b.p);
    }
    b = enc_make(&nine);
    doc = open_with(&b, "I\x07X", &err);
    check(err == TIKU_PDF_PASSWORD && doc == NULL, "ENC-008",
          "a control character, which SASLprep refuses, does not open it");
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

/** @brief Damaged files: a table that lies about the Encrypt dictionary,
 *         which is found by reading the file; a version and a revision
 *         that disagree about the key's length, which is refused. */
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
    b = enc_make(&rc4);
    ok = patch(&b, "/V 2 /R 3", "/V 5 /R 3");
    doc = open_with(&b, "", &err);
    check(ok && err == TIKU_PDF_UNSUPPORTED && doc == NULL, "ENC-007",
          "a 32-byte key (V 5) for a revision 3 password is refused");
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
    typed();
    damaged();
    printf("%s\n", fails ? "FAIL" : "PASS: RC4 40/128, AES 128/256, "
                                     "passwords and permissions");
    return fails ? 1 : 0;
}
