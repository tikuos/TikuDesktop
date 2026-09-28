/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_pdf_sign.c - signed documents pyHanko and LibreSSL made, read as a
 * signature panel reads them: who, when, why, where; a timestamp over a
 * signature, and one over the whole; a certifying signature and the
 * revision after it; two signers; the old SHA-1 forms; then a page
 * changed, a signature value changed and a range moved, each caught; and
 * every document bent a byte at a time without a fault.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_der.h"
#include "tiku_pdf.h"
#include "../fixtures/pdfsig.inc"

static int fails;

/** @brief One check's line, and its failure counted. */
static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-7s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/** @brief A document's bytes. */
typedef struct {
    unsigned char *p;
    size_t         n;
} doc_t;

/** @brief @p b64 decoded onto the end of @p d (a revision added). */
static void
add(doc_t *d, const char *b64)
{
    static const char abc[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned v = 0;
    int bits = 0;

    d->p = realloc(d->p, d->n + strlen(b64));
    for (; *b64 && *b64 != '='; b64++) {
        v = v << 6 | (unsigned)(strchr(abc, *b64) - abc);
        if ((bits += 6) >= 8) {
            bits -= 8;
            d->p[d->n++] = (unsigned char)(v >> bits);
        }
    }
}

/** @brief Fixture @p b64, with @p more's revision after it when given. */
static doc_t
doc(const char *b64, const char *more)
{
    doc_t d = { NULL, 0 };

    add(&d, b64);
    if (more != NULL) add(&d, more);
    return d;
}

/** @brief Where @p what first is in @p d, or -1. */
static long
find(const doc_t *d, const char *what)
{
    size_t n = strlen(what), i;

    for (i = 0; i + n <= d->n; i++) {
        if (!memcmp(d->p + i, what, n)) return (long)i;
    }
    return -1;
}

/** @brief @p d's signatures, @p n of them into @p s (the document is
 *         closed; its signatures are kept).  @return how many. */
static int
sigs(const doc_t *d, tiku_pdf_signature_t **s)
{
    tiku_pdf_t *pdf = NULL;
    int n = -1;

    *s = NULL;
    if (tiku_pdf_decode(d->p, d->n, &pdf) == TIKU_PDF_OK) {
        n = tiku_pdf_signatures(pdf, s);
    }
    tiku_pdf_close(pdf);
    return n;
}

/** @brief Whether @p a is @p b, both text or both none. */
static int
is(const char *a, const char *b)
{
    return a != NULL && b != NULL && strcmp(a, b) == 0;
}

/** @brief @p t as "2026-09-28 12:34:56 UTC". */
static const char *
when(int64_t t)
{
    static char out[40];

    tiku_der_time_text(t, out, sizeof out);
    return out;
}

/** @brief One approval signature by RSA, with all a panel shows. */
static void
approval_test(void)
{
    doc_t d = doc(sg_rsa, NULL);
    tiku_pdf_signature_t *s;
    int n = sigs(&d, &s);

    check(n == 1 && s[0].status == TIKU_PDF_SIG_GOOD && s[0].whole &&
          s[0].kind == TIKU_PDF_SIG_APPROVAL && is(s[0].field, "Author"),
          "SIG-001", "one approval signature: good, and nothing after it");
    check(n == 1 && is(s[0].signer, "Tiku Test Signer") &&
          is(s[0].org, "TikuOS Tests") && is(s[0].issuer, "Tiku Test Signer") &&
          s[0].self_signed && is(s[0].reason, "I wrote it") &&
          is(s[0].location, "Singapore") &&
          is(s[0].contact, "tiku@example.org") && s[0].signed_at > 0 &&
          s[0].stamp == 0, "SIG-002",
          "its signer, organisation, issuer, reason, place and contact");
    check(n == 1 && s[0].page == 0 && s[0].x0 == 20 && s[0].x1 == 200 &&
          s[0].y0 == 140 && s[0].y1 == 180, "SIG-003",
          "its field's box on the page, y down from the top");
    tiku_pdf_signatures_free(s, n);
    free(d.p);
}

/** @brief PAdES by ECDSA with a timestamp; certifying; two signers; a
 *         document timestamp; and the old SHA-1 forms. */
static void
kinds_test(void)
{
    doc_t d = doc(sg_pades, NULL);
    tiku_pdf_signature_t *s;
    int n = sigs(&d, &s);

    check(n == 1 && s[0].status == TIKU_PDF_SIG_GOOD &&
          is(s[0].signer, "Tiku EC Signer") &&
          s[0].stamp == TIKU_PDF_SIG_GOOD &&
          is(when(s[0].stamped_at), "2026-09-28 12:34:56 UTC") &&
          is(s[0].stamp_by, "Tiku Test TSA"), "SIG-004",
          "PAdES by ECDSA: good, dated by a timestamp over it");
    tiku_pdf_signatures_free(s, n);
    free(d.p);
    d = doc(sg_certify, NULL);
    n = sigs(&d, &s);
    check(n == 1 && s[0].status == TIKU_PDF_SIG_GOOD && !s[0].whole &&
          s[0].kind == TIKU_PDF_SIG_CERTIFY, "SIG-005",
          "a certifying signature, and a revision added after it");
    tiku_pdf_signatures_free(s, n);
    free(d.p);
    d = doc(sg_rsa, sg_two);
    n = sigs(&d, &s);
    check(n == 2 && is(s[0].field, "Author") && !s[0].whole &&
          s[0].status == TIKU_PDF_SIG_GOOD && is(s[1].field, "Reviewer") &&
          s[1].whole && s[1].status == TIKU_PDF_SIG_GOOD &&
          is(s[1].signer, "Tiku EC Signer"), "SIG-006",
          "two signers, in the order they signed");
    tiku_pdf_signatures_free(s, n);
    free(d.p);
    d = doc(sg_rsa, sg_doctime);
    n = sigs(&d, &s);
    check(n == 2 && s[0].status == TIKU_PDF_SIG_GOOD && !s[0].whole &&
          s[1].kind == TIKU_PDF_SIG_TIMESTAMP &&
          s[1].status == TIKU_PDF_SIG_GOOD && s[1].whole &&
          is(s[1].signer, "Tiku Test TSA") &&
          is(when(s[1].stamped_at), "2026-09-28 12:34:56 UTC"), "SIG-007",
          "a document timestamp over a signed document");
    tiku_pdf_signatures_free(s, n);
    free(d.p);
    d = doc(sg_sha1, NULL);
    n = sigs(&d, &s);
    check(n == 1 && s[0].status == TIKU_PDF_SIG_GOOD &&
          is(s[0].reason, "The old way") &&
          !is(when(s[0].signed_at), "2026-09-28 04:00:00 UTC") &&
          s[0].signed_at > 0, "SIG-008",
          "adbe.pkcs7.sha1: good; its time the one it signed, not /M's");
    tiku_pdf_signatures_free(s, n);
    free(d.p);
    d = doc(sg_x509, NULL);
    n = sigs(&d, &s);
    check(n == 1 && s[0].status == TIKU_PDF_SIG_GOOD &&
          is(s[0].signer, "Tiku Test Signer") &&
          is(when(s[0].signed_at), "2026-09-28 04:00:00 UTC"), "SIG-009",
          "adbe.x509.rsa_sha1: good, by /Cert; its time /M's, +08'00' off");
    tiku_pdf_signatures_free(s, n);
    free(d.p);
}

/** @brief The status of the one signature of @p d. */
static int
status_of(const doc_t *d)
{
    tiku_pdf_signature_t *s;
    int n = sigs(d, &s), st = n == 1 ? s[0].status : -1;

    tiku_pdf_signatures_free(s, n);
    return st;
}

/** @brief A letter of the page changed after signing, a digit of the
 *         value changed, a range moved: each caught. */
static void
tamper_test(void)
{
    static const char *const each[] = { sg_rsa, sg_sha1, sg_x509 };
    doc_t d;
    long at, end;
    size_t i;
    int changed = 1;

    for (i = 0; i < sizeof each / sizeof each[0]; i++) {
        d = doc(each[i], NULL);
        at = find(&d, "Signed by its author");
        d.p[at + 10] = 'I';             /* as pdfsig was shown it */
        changed &= status_of(&d) == TIKU_PDF_SIG_CHANGED;
        free(d.p);
    }
    check(changed, "SIG-010", "the page changed after signing: changed, "
                              "in each of the three forms");
    d = doc(sg_rsa, NULL);
    at = find(&d, "/Contents <") + 11;
    for (end = at; end < (long)d.n && memcmp(d.p + end, "0000000000", 10);
         end++) {}
    d.p[end - 5] = d.p[end - 5] == '0' ? '1' : '0';
    check(status_of(&d) == TIKU_PDF_SIG_BROKEN, "SIG-011",
          "a digit of its signature value changed: broken, as pdfsig says");
    free(d.p);
    d = doc(sg_sha1, NULL);
    at = find(&d, "/ByteRange [0 ") + 14;
    d.p[at + 9] = (unsigned char)(d.p[at + 9] == '9' ? '8' : d.p[at + 9] + 1);
    check(status_of(&d) == TIKU_PDF_SIG_BROKEN, "SIG-012",
          "a range that no longer leaves out its value: broken");
    free(d.p);
}

/** @brief Every document bent at a spread of bytes: no fault, and none
 *         bent in the page it signs reads as good. */
static void
bend_test(void)
{
    static const char *const each[] = { sg_rsa, sg_pades, sg_sha1, sg_x509 };
    tiku_pdf_signature_t *s;
    size_t i, at;
    int n, good = 0;
    long page;

    for (i = 0; i < sizeof each / sizeof each[0]; i++) {
        doc_t d = doc(each[i], NULL);

        page = find(&d, "Signed by its author");
        for (at = 0; at < d.n; at += 1 + d.n / 600) {
            unsigned char was = d.p[at];

            d.p[at] ^= 0x21;
            n = sigs(&d, &s);
            good += (long)at >= page && (long)at < page + 20 && n > 0 &&
                    s[0].status == TIKU_PDF_SIG_GOOD;
            tiku_pdf_signatures_free(s, n);
            d.p[at] = was;
        }
        free(d.p);
    }
    check(good == 0, "SIG-013", "bent a byte at a time: no fault, and no "
                                "changed page reads as signed");
}

int
main(void)
{
    printf("Signed documents others made, checked\n");
    approval_test();
    kinds_test();
    tamper_test();
    bend_test();
    printf("%s\n", fails ? "FAIL" : "PASS: CMS, PAdES, SHA-1 and X.509 "
           "signatures, timestamps, and what changed");
    return fails ? 1 : 0;
}
