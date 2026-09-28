/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_saslprep.c - SASLprep (RFC 4013): the RFC's own seven examples,
 * then each of its steps -- the two mappings, NFKC's decomposing,
 * ordering and joining, Hangul, Corrigendum 4, what is prohibited and
 * the direction check -- each result as Python's stringprep and Unicode
 * 3.2 give it; then what is not UTF-8, and what does not fit.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_saslprep.h"

static int fails;

/** @brief One check's line, and its failure counted. */
static void
check(int cond, const char *what)
{
    printf("  [%s] SASL-001  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/** @brief Each input to its result, or to none, as the reference has it. */
static void
vector_test(void)
{
    static const struct {
        const char *in, *out;       /* UTF-8; NULL when it is refused    */
        const char *what;
    } v[] = {
        {"I\xC2\xADX", "IX",
         "RFC 4013 1: a soft hyphen is mapped to nothing"},
        {"user", "user",
         "RFC 4013 2: user is itself"},
        {"USER", "USER",
         "RFC 4013 3: case is kept"},
        {"\xC2\xAA", "a",
         "RFC 4013 4: the ordinal a is a"},
        {"\xE2\x85\xA8", "IX",
         "RFC 4013 5: Roman nine is I and X"},
        {"\x07", NULL,
         "RFC 4013 6: a control is prohibited"},
        {"\xD8\xA7" "1", NULL,
         "RFC 4013 7: Arabic then a digit fails the direction check"},
        {"a\xC2\xA0" "b", "a b",
         "a no-break space is a space"},
        {"\xE2\x80\x8Bx", "x",
         "a zero-width space is nothing"},
        {"\xEF\xBC\xB0\xEF\xBD\x81\xEF\xBD\x93\xEF\xBD\x93", "Pass",
         "full-width letters are the letters"},
        {"e\xCC\x81", "\xC3\xA9",
         "a letter and its accent are one"},
        {"d\xCC\x87\xCC\xA3", "\xE1\xB8\x8D\xCC\x87",
         "marks after a letter are put in order, then joined"},
        {"\xEF\xAC\x81", "fi",
         "a ligature is its letters"},
        {"\xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8", "\xEA\xB0\x81",
         "Hangul jamo make their syllable"},
        {"\xEA\xB0\x81", "\xEA\xB0\x81",
         "a Hangul syllable stays"},
        {"\xE3\x8C\x80", "\xE3\x82\xA2\xE3\x83\x91\xE3\x83\xBC\xE3\x83\x88",
         "a squared word is its kana"},
        {"\xF0\xAF\xA1\xA8", "\xF0\xA1\x8D\xAA",
         "Corrigendum 4's fix is taken"},
        {"\xC8\xA1", "\xC8\xA1",
         "a code point unassigned in 3.2 passes a query"},
        {"\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D", "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D",
         "Hebrew alone passes"},
        {"\xD7\xA9" "1", NULL,
         "Hebrew ending in a digit fails"},
        {"\xD7\xA9" "a\xD7\xA9", NULL,
         "Hebrew with a Latin letter fails"},
        {"\xEE\x80\x80", NULL,
         "private use is prohibited"},
        {"", "",
         "nothing is nothing"},
    };
    char out[128];
    size_t i;

    for (i = 0; i < sizeof v / sizeof v[0]; i++) {
        long n = tiku_saslprep(v[i].in, out, sizeof out);

        check(v[i].out == NULL ? n < 0
                               : n == (long)strlen(v[i].out) &&
                                 strcmp(out, v[i].out) == 0, v[i].what);
    }
}

/** @brief What is not UTF-8, what does not fit, and what is too long:
 *         each refused, not guessed at. */
static void
refusal_test(void)
{
    static const char *const bad[] = {
        "\xC3", "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "a\xFFz",
    };
    static char big[1030], out[16], wide[4096];
    size_t i;
    int all = 1;

    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        all &= tiku_saslprep(bad[i], out, sizeof out) < 0;
    }
    check(all, "cut, overlong, surrogate, past U+10FFFF and stray bytes "
               "are not UTF-8");
    check(tiku_saslprep("IX", out, 2) < 0 &&
          tiku_saslprep("IX", out, 3) == 2,
          "a result is refused when it and its NUL do not fit");
    memset(big, 'a', 1024);
    check(tiku_saslprep(big, wide, sizeof wide) == 1024,
          "a password of 1024 characters is prepared");
    memset(big, 'a', sizeof big - 1);
    check(tiku_saslprep(big, wide, sizeof wide) < 0,
          "one of more is refused, though its result would fit");
}

int
main(void)
{
    printf("SASLprep\n");
    vector_test();
    refusal_test();
    if (fails) {
        printf("%d FAILED\n", fails);
        return 1;
    }
    puts("PASS: SASLprep as RFC 4013, stringprep and Unicode 3.2 have it");
    return 0;
}
