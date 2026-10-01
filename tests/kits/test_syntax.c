/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_syntax.c - telling a line of TikuOS BASIC apart: which stretches
 * of it are reserved words, numbers, quoted text and remarks, and -- the
 * half that matters more -- which stretches are NOT.
 *
 * A highlighter's failures are all of one kind: it says a thing is
 * something it is not, and the reader believes it, because the machine
 * said so.  So most of what is pinned here is the negative: PRINTER is
 * not PRINT, a label after GOTO is not a keyword, a word inside a remark
 * is not a word, and `.5` is not a number in this language however much
 * it looks like one elsewhere.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_syntax.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] syntax  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/*
 * The whole line as one string of class letters, one per byte: 'p' plain,
 * 'f' faint, 'w' word, 'n' number, 's' string, 'r' remark.  Comparing
 * that against a hand-written expectation pins the BOUNDARIES, which is
 * where a highlighter actually goes wrong -- an off-by-one that paints
 * the space after a keyword passes any "is PRINT a word" check.
 */
static const char *
classify(const char *line)
{
    static char map[256];
    tiku_span_t span[64];
    int n = tiku_syntax_spans(TIKU_SYNTAX_BASIC, line, span, 64);
    size_t at = 0u, len = strlen(line);
    int i;

    memset(map, 'p', sizeof map);
    for (i = 0; i < n && at < sizeof map - 1u; i++) {
        char c = span[i].ink == TIKU_INK_FAINT  ? 'f'
               : span[i].ink == TIKU_INK_WORD   ? 'w'
               : span[i].ink == TIKU_INK_NUM    ? 'n'
               : span[i].ink == TIKU_INK_STR    ? 's'
               : span[i].ink == TIKU_INK_REMARK ? 'r'
               : span[i].ink == TIKU_INK_PATH   ? 'a' : 'p';
        int k;

        for (k = 0; k < span[i].len && at < sizeof map - 1u; k++) {
            map[at++] = c;
        }
    }
    /* Whatever the table did not reach is plain by contract, so the
     * expectation is written for the whole line either way. */
    if (len < sizeof map) {
        map[len] = '\0';
    } else {
        map[sizeof map - 1u] = '\0';
    }
    return map;
}

/** @brief The same map for C, which has to be told where it came from. */
static const char *
classify_c(const char *line, tiku_syntax_state_t in,
           tiku_syntax_state_t *out)
{
    static char map[256];
    tiku_span_t span[64];
    int n = tiku_syntax_spans_on(TIKU_SYNTAX_C, line, in, out, span, 64);
    size_t at = 0u, len = strlen(line);
    int i;

    memset(map, 'p', sizeof map);
    for (i = 0; i < n && at < sizeof map - 1u; i++) {
        char c = span[i].ink == TIKU_INK_FAINT  ? 'f'
               : span[i].ink == TIKU_INK_WORD   ? 'w'
               : span[i].ink == TIKU_INK_NUM    ? 'n'
               : span[i].ink == TIKU_INK_STR    ? 's'
               : span[i].ink == TIKU_INK_REMARK ? 'r' : 'p';
        int k;

        for (k = 0; k < span[i].len && at < sizeof map - 1u; k++) {
            map[at++] = c;
        }
    }
    if (len < sizeof map) {
        map[len] = '\0';
    } else {
        map[sizeof map - 1u] = '\0';
    }
    return map;
}

static void
same_c(const char *line, tiku_syntax_state_t in, const char *want,
       const char *what)
{
    tiku_syntax_state_t out = TIKU_SYNTAX_OPEN;
    const char *got = classify_c(line, in, &out);

    check(strcmp(got, want) == 0, what);
    if (strcmp(got, want) != 0) {
        printf("         line: %s\n         want: %s\n         got:  %s\n",
               line, want, got);
    }
}

static void
same(const char *line, const char *want, const char *what)
{
    const char *got = classify(line);

    check(strcmp(got, want) == 0, what);
    if (strcmp(got, want) != 0) {
        printf("         line: %s\n         want: %s\n         got:  %s\n",
               line, want, got);
    }
}

int
main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("the interpreter's own words, and only those\n");
    check(tiku_syntax_basic_word("PRINT") && tiku_syntax_basic_word("print"),
          "a reserved word is reserved in either case");
    check(tiku_syntax_basic_word("STR$"),
          "the sigil is part of the word: STR$ is one of them");
    check(!tiku_syntax_basic_word("PRINTER"),
          "PRINTER is not PRINT -- the whole word has to match");
    check(!tiku_syntax_basic_word("PRIN"), "and neither is a prefix of one");
    check(tiku_syntax_basic_word("BLEOBSERVE") &&
          tiku_syntax_basic_word("HTTPSTATUS"),
          "a word the interpreter spells out rather than crunches is "
          "still reserved");
    check(tiku_syntax_basic_word("GCD") && tiku_syntax_basic_word("ROMAN$"),
          "and so is one the bundled extension kit registers at boot");
    check(!tiku_syntax_basic_word("VFSWRITELONG"),
          "a long name is not the short word it starts with");
    check(!tiku_syntax_basic_word("") && !tiku_syntax_basic_word(NULL),
          "nothing is not a word");

    printf("\na line is told apart into what its parts mean\n");
    same("10 PRINT A", "ffpwwwwwpp",
         "the line number stands back, the statement is reserved, the "
         "variable is the document's own ink");
    same("PRINT \"hi\"", "wwwwwpssss",
         "quoted text is quoted text, and the quotes belong to it");
    same("LET A = 42", "wwwpppppnn", "a number in the body reads as a number");
    same("PRINTER = 1", "ppppppppppn",
         "a name that merely begins with a keyword is left alone");
    same("print a", "wwwwwpp", "lower case is the same language");
    /* A path is a string that is also an ADDRESS: its body takes the
     * path ink, the quotes stay string, and a string that is NOT a
     * path is not touched. */
    same("PRINT \"/dev/led\"", "wwwwwpsaaaaaaaas",
         "a quoted name beginning with a slash is an address: the body "
         "is path ink, the quotes stay string");
    same("PRINT \"hi there\"", "wwwwwpssssssssss",
         "a quoted name that is not a path is an ordinary string, "
         "untouched");
    same("PRINT \"/x", "wwwwwpsaa",
         "an unterminated path runs to the line's end with no closing "
         "quote to wear string ink");

    printf("\nremarks swallow the rest of the line, colons and all\n");
    same("REM PRINT 1", "rrrrrrrrrrr",
         "REM takes everything after it -- the PRINT in a remark is not a "
         "statement");
    same("' PRINT 1", "rrrrrrrrr", "and so does the apostrophe");
    same("A=1 : REM x : PRINT", "ppnppprrrrrrrrrrrrr",
         "a remark mid-line ends the line: the colon after it separates "
         "nothing");
    same("PRINT \"' not a remark\"", "wwwwwpssssssssssssssss",
         "an apostrophe inside quotes is text, not the start of a remark");

    printf("\nnumbers are the forms this language has, and no others\n");
    same("A = 0x1F", "ppppnnnn", "C-style hex");
    same("A = 0b1010", "ppppnnnnnn", "C-style binary");
    same("A = &HFF", "ppppnnnn", "BASIC-style hex");
    same("A = &B01", "ppppnnnn", "BASIC-style binary");
    same("A = 1.5", "ppppnnn", "a fixed-point literal is one number");
    same("A = 1.", "ppppnn", "and a trailing point belongs to it -- Q.3");
    same("A = .5", "pppppn",
         "but a LEADING point is not a number here, whatever it is "
         "elsewhere: the digit is, the dot is not");
    same("A = 1E3", "ppppnpp",
         "and there is no exponent form -- E3 is a name, not a scale");
    same("A = -1", "pppppn",
         "the minus is an operator: colouring it into the number would "
         "paint A-1 as if it held one");
    same("A = &Z", "pppppp",
         "an ampersand that starts no literal is punctuation");

    printf("\nwhat follows a word can change what the word after it means\n");
    same("GOTO done", "wwwwppppp",
         "the target of a GOTO is a label the interpreter reads raw, so "
         "it keeps plain ink even if it were spelled like a keyword");
    same("GOTO end", "wwwwpppp",
         "and that holds when the label IS spelled like a keyword");
    same("GOTO l1, l2", "wwwwppppppp",
         "a comma after a target brings another target");
    same("IF A THEN GOTO L1 ELSE PRINT", "wwpppwwwwpwwwwppppwwwwpwwwww",
         "but only a comma does: the ELSE after a target is still the "
         "keyword it is");
    same("DATA PRINT, 1, END", "wwwwpppppppppppppp",
         "after DATA the rest of the line is items -- a word there is a "
         "word, not a statement");
    same("DATA \"a\", b", "wwwwpsssppp",
         "though a quoted item is still quoted, because READ reads it "
         "as a string");

    printf("\nlabels, line numbers, and the head of a line\n");
    same("loop: PRINT 1", "ppppppwwwwwpn",
         "a name and a colon at the head of a line is a jump target, and "
         "it is the person's own name for it");
    same("A: PRINT 1", "pppwwwwwpn",
         "one letter is not a label: A is the variable and the colon "
         "separates statements, which is what the interpreter reads");
    same("10PRINT 1", "ffwwwwwpn",
         "no space is needed after a line number, and the highlighter "
         "must not need one either");
    same("  20 END", "ppffpwww",
         "blanks before the number do not stop it being the line's name");
    same("A = 10", "ppppnn",
         "a number that is not at the head of a line is a value, not a "
         "name -- the same digits mean two things by position");

    printf("\nthe places a highlighter agrees with the interpreter or "
           "misleads\n");
    same("REM$ PRINT 1", "rrrrrrrrrrrr",
         "a sigil does not have to belong to the word: the interpreter "
         "matches REM and asks only whether the next byte could CONTINUE "
         "a word, and a $ cannot -- so REM$ starts a remark");
    same("PRINT STR$(1)", "wwwwwpwwwwpnp",
         "while a word the table HAS with its sigil keeps it");
    same("PRINT 1 ' not a comment here", "wwwwwpnpppwwwppppppppppppppp",
         "an apostrophe mid-statement is not a comment -- the "
         "interpreter refuses the line as trailing junk, and colouring "
         "the tail as a remark would say the line will run");
    same("A=1 : ' but here it is", "ppnppprrrrrrrrrrrrrrrr",
         "after a colon a statement may begin, and there it IS a remark");
    same("10 loop: PRINT 1", "ffpppppppwwwwwpn",
         "a label may follow a line number: the number names the line "
         "and the label names the body");
    same("IF A THEN GOTO 100 ELSE PRINT 2",
         "wwpppwwwwpwwwwpnnnpwwwwpwwwwwpn",
         "a NUMBER may be the jump target too, and the word after it is "
         "the keyword it is -- the target context is spent either way");
    same("GOTO 100, 200", "wwwwpnnnppnnn",
         "and a comma after a numeric target still brings another");

    printf("\nthe answer is well formed however odd the line is\n");
    {
        tiku_span_t span[64];
        int n;

        check(tiku_syntax_spans(TIKU_SYNTAX_BASIC, "", span, 64) == 0,
              "an empty line has nothing to say about it");
        check(tiku_syntax_spans(TIKU_SYNTAX_BASIC, NULL, span, 64) == 0 &&
              tiku_syntax_spans(TIKU_SYNTAX_BASIC, "PRINT", NULL, 64) == 0,
              "and neither has no line at all");
        check(tiku_syntax_spans(TIKU_SYNTAX_NONE, "PRINT 1", span, 64) == 0,
              "prose is prose: with no language there are no words");

        n = tiku_syntax_spans(TIKU_SYNTAX_BASIC, "PRINT PRINT PRINT",
                              span, 1);
        check(n == 1 && span[0].ink == TIKU_INK_WORD && span[0].len == 5,
              "a table with room for one run says one true thing and "
              "stops -- the rest is plain, which is legible");

        same("PRINT \"unterminated", "wwwwwpsssssssssssss",
             "an unterminated string runs to the end of the line, because "
             "that is exactly what the interpreter does with it");
        same("PRINT \"a\\\"b\"", "wwwwwpssssss",
             "a backslash takes the next byte with it, so an escaped "
             "quote does not end the string");
    }

    printf("\na file says which language it is by its name\n");
    check(tiku_syntax_of_path("/home/me/blink.bas") == TIKU_SYNTAX_BASIC,
          "a .bas file is BASIC");
    check(tiku_syntax_of_path("/home/me/BLINK.BAS") == TIKU_SYNTAX_BASIC,
          "and the suffix is read in either case");
    check(tiku_syntax_of_path("/home/me/letter.txt") == TIKU_SYNTAX_NONE &&
          tiku_syntax_of_path("basics") == TIKU_SYNTAX_NONE,
          "everything else is prose -- and 'basics' is not a suffix");
    check(tiku_syntax_of_path(NULL) == TIKU_SYNTAX_NONE &&
          tiku_syntax_of_path(".bas") == TIKU_SYNTAX_BASIC,
          "no name is prose; a name that is only the suffix is not");

    printf("\nand C, which carries a remark from one line to the next\n");
    check(tiku_syntax_of_path("/src/tiku_ide.c") == TIKU_SYNTAX_C &&
          tiku_syntax_of_path("tiku_app.h") == TIKU_SYNTAX_C,
          "a .c and a .h are C");
    check(tiku_syntax_of_path("/src/notes.txt") == TIKU_SYNTAX_NONE,
          "and everything else is still prose");
    same_c("int x = 42;", TIKU_SYNTAX_OPEN, "wwwpppppnnp",
           "a word the language reserves, and a number written down");
    same_c("count = total;", TIKU_SYNTAX_OPEN, "pppppppppppppp",
           "a name the language did NOT reserve is the document's own "
           "ink: a table with a library in it would claim `count`");
    same_c("// gone", TIKU_SYNTAX_OPEN, "rrrrrrr",
           "a line remark runs to the end of its line");
    {
        tiku_syntax_state_t out = TIKU_SYNTAX_OPEN;

        /* The claim the line-local signature could not make. */
        (void)classify_c("int a; /* opens", TIKU_SYNTAX_OPEN, &out);
        check(out == TIKU_SYNTAX_BLOCK,
              "a block remark left open is SAID to be open: the next "
              "line cannot tell on its own, and this is the whole "
              "reason the state is carried");
        same_c("still inside", TIKU_SYNTAX_BLOCK, "rrrrrrrrrrrr",
               "a line begun inside a remark is all remark, whatever "
               "words happen to be in it");
        (void)classify_c("still inside", TIKU_SYNTAX_BLOCK, &out);
        check(out == TIKU_SYNTAX_BLOCK,
              "and it leaves the next line inside it too");
        same_c("done */ int b;", TIKU_SYNTAX_BLOCK, "rrrrrrrpwwwppp",
               "the close belongs to the remark; what follows is the "
               "language again");
        (void)classify_c("done */ int b;", TIKU_SYNTAX_BLOCK, &out);
        check(out == TIKU_SYNTAX_OPEN, "and the carrying stops there");
        (void)classify_c("a /* b */ c", TIKU_SYNTAX_OPEN, &out);
        check(out == TIKU_SYNTAX_OPEN,
              "a remark opened and closed on one line carries nothing");
    }
    same_c("s = \"/* not a remark\";", TIKU_SYNTAX_OPEN,
           "ppppsssssssssssssssssp",
           "a remark opener inside quotes is text: the string had it "
           "first");
    {
        tiku_syntax_state_t out = TIKU_SYNTAX_BLOCK;

        (void)classify_c("s = \"/* not a remark\";", TIKU_SYNTAX_OPEN,
                         &out);
        check(out == TIKU_SYNTAX_OPEN,
              "and it opens nothing for the line after it");
    }
    same_c("#include <stdio.h>", TIKU_SYNTAX_OPEN, "wwwwwwwwpppppppppp",
           "what the preprocessor was told is reserved too");
    same_c("c = 'x';", TIKU_SYNTAX_OPEN, "ppppsssp",
           "a character is text in quotes, like any other");
    same_c("s = \"open", TIKU_SYNTAX_OPEN, "ppppsssss",
           "a quote never closed ends with its line rather than eating "
           "the next -- the same answer BASIC gives");

    {
        /* Past a full run table the line is still read for its remarks,
         * so what it leaves to the next line is right. */
        static char line[400];
        tiku_span_t span[128];
        tiku_syntax_state_t out;
        int n, total = 0;

        line[0] = '\0';
        for (int i = 0; i < 70; i++) {
            strcat(line, "1 ");
        }
        strcat(line, "/* open");
        n = tiku_syntax_spans_on(TIKU_SYNTAX_C, line, TIKU_SYNTAX_OPEN, &out,
                                 span, 128);
        check(n == 128 && out == TIKU_SYNTAX_BLOCK,
              "a remark opened after the run table is full still carries");
        line[0] = '\0';
        for (int i = 0; i < 64; i++) {
            strcat(line, "1 ");
        }
        strcat(line, "/* shut */ 1");
        n = tiku_syntax_spans_on(TIKU_SYNTAX_C, line, TIKU_SYNTAX_OPEN, &out,
                                 span, 128);
        check(n == 128 && out == TIKU_SYNTAX_OPEN,
              "and one that meets the full table and closes carries nothing");
        for (int i = 0; i < n; i++) {
            total += span[i].len;
        }
        check(total == 128, "the runs that fit cover only their own bytes");
    }

    printf("\n%s\n", fails ? "FAILURES"
                           : "the colours say what the interpreter would");
    return fails ? 1 : 0;
}
