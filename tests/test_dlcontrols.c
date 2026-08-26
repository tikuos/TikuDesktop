/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_dlcontrols.c - the control commands, end to end.
 *
 * The display list's bargain is that a window can travel as what was
 * DRAWN instead of as pixels, and that the far end puts back the same
 * window.  Every control that learned to record itself is checked the
 * only way that claim can be: draw it here, send it, draw it from the
 * stream at the other end, and compare the two surfaces byte for byte.
 *
 * It also pins the thing that went wrong when these were written: a
 * painter that records ONE command and then suppresses the calls it is
 * made of sends a window that arrives looking finished and is not -- a
 * gauge with no bar in it, a tip with no words.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include "tiku_dl.h"
#include "tiku_ui.h"
#include "tiku_alert.h"
#include "tiku_tabs.h"
#include "tiku_slider.h"
#include "tiku_syntax.h"
#include "tiku_font.h"

static int fails;
static void check(int c, const char *w){ printf("  [%s] %s\n", c?"PASS":"FAIL", w); if(!c) fails++; }

static int same(tiku_surface_t *a, tiku_surface_t *b){
    return a->w==b->w && a->h==b->h &&
           memcmp(a->px, b->px, (size_t)a->w*(size_t)a->h*sizeof *a->px)==0;
}
static int differing(tiku_surface_t *a, tiku_surface_t *b){
    int i, n = a->w*a->h, d = 0;
    for (i=0;i<n;i++) if (a->px[i]!=b->px[i]) d++;
    return d;
}

#define RT(name, drawcalls) do {                                   \
    tiku_surface_t *direct = tiku_surface_new(W,H,TIKU_C_PANEL);   \
    tiku_surface_t *replay = tiku_surface_new(W,H,TIKU_C_PANEL);   \
    tiku_dl_t *dl = tiku_dl_new(); tiku_dl_t *back;                \
    unsigned char wire[65536]; size_t wrote=0;                     \
    tiku_surface_t *s;                                             \
    s = direct; drawcalls;                                         \
    s = direct; s->record = dl; drawcalls; s->record = NULL;       \
    check(tiku_dl_misses(dl)==0, name ": nothing was missed");     \
    check(tiku_dl_flatten(dl, wire, sizeof wire, &wrote) && wrote, \
          name ": flattens to the wire");                          \
    back = tiku_dl_unflatten(wire, wrote, NULL);                   \
    check(back && tiku_dl_count(back)==tiku_dl_count(dl),          \
          name ": comes back with every command");                 \
    if (back) { s = replay; (void)tiku_dl_play(back, replay); }    \
    check(same(direct, replay),                                    \
          name ": the far end's pixels are THIS end's, to the byte");\
    if (!same(direct,replay))                                      \
        printf("        (%d pixels differ)\n", differing(direct,replay)); \
    tiku_dl_free(dl); if(back) tiku_dl_free(back);                 \
    tiku_surface_free(direct); tiku_surface_free(replay);          \
} while (0)

#define W 500
#define H 300
int main(void){
    setvbuf(stdout,NULL,_IOLBF,0);
    printf("what was drawn, drawn again at the other end\n");

    RT("gauge", tiku_ui_gauge(s,(tiku_rect_t){10,10,200,12},0.42f));
    RT("tip",   tiku_ui_tip(s,(tiku_rect_t){10,10,160,20},"the whole name"));
    RT("field", tiku_ui_textfield(s,(tiku_rect_t){10,10,200,20},"typed",5,0u));
    RT("scroll",tiku_ui_scrollbar(s,(tiku_rect_t){10,10,15,200},0.3f,0.4f,0));
    { tiku_slider_t sl; tiku_slider_init(&sl,0,100,50,5);
      RT("slider", tiku_slider_draw(&sl,s,(tiku_rect_t){10,10,200,16})); }
    { tiku_tabs_t t; tiku_tabs_init(&t);
      tiku_tabs_add(&t,"Info"); tiku_tabs_add(&t,"Permissions");
      tiku_tabs_add(&t,"Attributes"); tiku_tabs_select(&t,1);
      RT("tabs", tiku_tabs_draw(&t,s,(tiku_rect_t){10,10,300,22})); }
    /* The one that was silently wrong: its marker is drawn pixel by
     * pixel, and pixels are what the list cannot carry.  Recorded as its
     * parts it went over with no marker and NOTHING said so -- zero
     * misses and a wrong window. */
    RT("menufield", tiku_ui_menufield(s,(tiku_rect_t){10,10,90,20},"Name",0u));

    { tiku_alert_t a; tiku_alert_reset(&a);
      tiku_alert_open(&a,TIKU_ALERT_WARN,0,"Discard changes? Nothing saved them.","Cancel|Discard");
      RT("alert", tiku_alert_draw(&a,s,(tiku_rect_t){10,10,420,150})); }
    /* A line of a program: several runs, each meaning something, drawn
     * with one call and travelling as the runs it is made of.
     *
     * It fills its own ground first, and must: RT draws twice into the
     * same surface, so anything ANTIALIASED against what is already
     * there -- which is all text -- blends over itself the second time
     * and stops matching a replay that drew it once.  Every widget above
     * paints its ground for its own reasons and is idempotent by
     * accident; a bare line of text is the case that shows the rule. */
    { tiku_span_t sp[16];
      int nsp = tiku_syntax_spans(TIKU_SYNTAX_BASIC, "10 PRINT \"hi\"",
                                  sp, 16);
      RT("styled line",
         tiku_fill(s, (tiku_rect_t){0, 0, W, H}, TIKU_C_DOC);
         tiku_ui_text_spans(s, tiku_font_plain(), 10, 30,
                            "10 PRINT \"hi\"", sp, nsp)); }

    /* A picture: a bitmap with its own mask, which used to be the one
     * thing a window could only send as pixels. */
    { uint32_t art[16 * 16];
      int k;

      for (k = 0; k < 16 * 16; k++) {
          /* A shape, not a square: the corners are left transparent so
           * the mask has something to carry. */
          int px = k % 16, py = k / 16;
          int inside = (px > 2 && px < 13) || (py > 2 && py < 13);

          art[k] = inside ? (0xff000000u | (uint32_t)(px * 16) << 16 |
                             (uint32_t)(py * 16) << 8 | 0x40u)
                          : 0u;
      }
      RT("picture",
         tiku_fill(s, (tiku_rect_t){0, 0, W, H}, TIKU_C_PANEL);
         tiku_picture(s, 20, 20, 16, 16, art)); }

    printf("\na picture crosses as a picture, mask and all\n");
    {
        tiku_surface_t *s2 = tiku_surface_new(64, 64, TIKU_C_PANEL);
        tiku_dl_t *dl2 = tiku_dl_new();
        uint32_t art[8 * 8];
        char lines[512];
        int k;

        for (k = 0; k < 8 * 8; k++) {
            art[k] = (k == 0) ? 0u : (0xff000000u | 0x00204080u);
        }
        s2->record = dl2;
        tiku_picture(s2, 4, 4, 8, 8, art);
        s2->record = NULL;
        check(tiku_dl_pictures(dl2) == 1 && tiku_dl_misses(dl2) == 0,
              "a small picture is carried, not confessed");
        check(tiku_dl_count(dl2) == 1,
              "as ONE command -- the pixels under it are part of it, not "
              "sixty-four more commands");
        (void)tiku_dl_say(dl2, lines, sizeof lines);
        check(strstr(lines, "a picture, 8 by 8") != NULL,
              "and a reader is told there is a picture and how big it is");
        /* The far end must draw the mask the same way: the corner pixel
         * this list leaves transparent must stay the ground it was. */
        {
            tiku_surface_t *back = tiku_surface_new(64, 64, TIKU_C_PANEL);
            unsigned char wire[8192];
            size_t wrote = 0u;
            tiku_dl_t *again;

            check(tiku_dl_flatten(dl2, wire, sizeof wire, &wrote) &&
                  wrote > 0u, "it flattens onto the wire");
            again = tiku_dl_unflatten(wire, wrote, NULL);
            check(again != NULL && tiku_dl_pictures(again) == 1,
                  "and comes back still a picture -- a fact of the list, "
                  "whichever end is holding it");
            if (again != NULL) {
                (void)tiku_dl_play(again, back);
                tiku_dl_free(again);
            }
            check(tiku_peek(back, 4, 4) == TIKU_C_PANEL,
                  "the pixel the mask left out is the ground it was, not "
                  "a square of black around the shape");
            check(tiku_peek(back, 5, 5) == TIKU_RGB(0x20, 0x40, 0x80),
                  "and the pixel it carried is the colour it carried");
            tiku_surface_free(back);
        }
        /*
         * Bigger than a picture command may carry: refused, and SAID so.
         * 100 by 100 is the size that matters -- it is 40 008 bytes,
         * which the wire's own u16 length would take happily, so what
         * refuses it is the cap and nothing else.
         */
        {
            static uint32_t big[100 * 100];
            tiku_dl_t *dl3 = tiku_dl_new();

            s2->record = dl3;
            tiku_picture(s2, 0, 0, 100, 100, big);
            s2->record = NULL;
            check(tiku_dl_pictures(dl3) == 0 && tiku_dl_misses(dl3) > 0,
                  "a picture past the cap marks the list not-whole "
                  "rather than crossing as a command nobody sized for");
            tiku_dl_free(dl3);
        }
        tiku_dl_free(dl2);
        tiku_surface_free(s2);
    }

    printf("\nwhat a run means decides its ink, and the ink is what crosses\n");
    {
        /*
         * Not "some colours appeared": the command carrying PRINT must
         * carry the shade the theme names for a reserved word, and the
         * command carrying the line number must carry the receded ink.
         * A highlighter that painted every run the same would pass any
         * check that only counted commands.
         */
        tiku_surface_t *s2 = tiku_surface_new(W, H, TIKU_C_DOC);
        tiku_dl_t *dl2 = tiku_dl_new();
        tiku_span_t sp[16];
        int nsp = tiku_syntax_spans(TIKU_SYNTAX_BASIC, "10 PRINT \"hi\"",
                                    sp, 16);
        unsigned char wire[8192];
        size_t wrote = 0u, at = 0u;
        int saw_word = 0, saw_faint = 0, saw_str = 0, texts = 0;

        s2->record = dl2;
        tiku_ui_text_spans(s2, tiku_font_plain(), 10, 30, "10 PRINT \"hi\"",
                           sp, nsp);
        s2->record = NULL;
        check(nsp >= 4, "the line is told apart into runs before it is drawn");
        check(tiku_dl_flatten(dl2, wire, sizeof wire, &wrote) && wrote > 0u,
              "and the drawing of it flattens onto the wire");
        /* Walk the commands by hand: [u16 op][u16 len][payload], and for
         * a text command the colour is a u32 at payload+4 and the string
         * begins at payload+9. */
        while (at + 4u <= wrote) {
            unsigned op = (unsigned)wire[at] | ((unsigned)wire[at + 1] << 8);
            unsigned len = (unsigned)wire[at + 2] |
                           ((unsigned)wire[at + 3] << 8);
            const unsigned char *pay = wire + at + 4;

            if (at + 4u + len > wrote) {
                break;
            }
            if (op == 6u && len > 9u) {          /* OP_TEXT */
                tiku_rgb_t c = (tiku_rgb_t)pay[4] |
                               ((tiku_rgb_t)pay[5] << 8) |
                               ((tiku_rgb_t)pay[6] << 16) |
                               ((tiku_rgb_t)pay[7] << 24);
                const char *t = (const char *)pay + 9;

                texts++;
                if (strcmp(t, "PRINT") == 0 && c == tiku_ink(TIKU_INK_WORD)) {
                    saw_word = 1;
                }
                if (strcmp(t, "10") == 0 && c == tiku_ink(TIKU_INK_FAINT)) {
                    saw_faint = 1;
                }
                if (strcmp(t, "\"hi\"") == 0 &&
                    c == tiku_ink(TIKU_INK_STR)) {
                    saw_str = 1;
                }
            }
            at += 4u + len;
        }
        check(texts >= 4,
              "each run crosses as its own text command, the way every "
              "other coloured thing on this desktop travels");
        check(saw_word,
              "the command carrying PRINT carries the shade the theme "
              "names for a word the language reserves");
        check(saw_faint,
              "the one carrying the line number carries the receded ink, "
              "which is derived and not named");
        check(saw_str, "and the quoted text carries the quoted-text ink");
        check(tiku_ink(TIKU_INK_WORD) != tiku_ink(TIKU_INK_PLAIN) &&
              tiku_ink(TIKU_INK_FAINT) != tiku_ink(TIKU_INK_PLAIN) &&
              tiku_ink(TIKU_INK_STR) != tiku_ink(TIKU_INK_WORD),
              "and the three are three different shades -- a highlighter "
              "that painted them alike would satisfy every check above");
        tiku_dl_free(dl2);
        tiku_surface_free(s2);
    }

    printf("\nthe window, narrated from the wire alone\n");
    {
        /*
         * The stage-4 claim, made good at the smallest honest size: a
         * reader that never touches a surface takes the same bytes the
         * pixels came from and says what the window SHOWS.  Every fact
         * asserted below reaches the narration through tiku_dl_read,
         * from the flattened stream, with no pixels consulted anywhere.
         */
        tiku_surface_t *sf = tiku_surface_new(W, H, TIKU_C_PANEL);
        tiku_dl_t *dl = tiku_dl_new();
        unsigned char wire[65536];
        static char story[4096];
        size_t wrote = 0;
        tiku_alert_t a;
        tiku_tabs_t t;
        tiku_slider_t sl;

        sf->record = dl;
        tiku_alert_reset(&a);
        tiku_alert_open(&a, TIKU_ALERT_WARN, 0,
                        "Discard unsaved changes?", "Cancel|Discard");
        tiku_alert_draw(&a, sf, (tiku_rect_t){ 10, 10, 420, 150 });
        tiku_tabs_init(&t);
        (void)tiku_tabs_add(&t, "Info");
        (void)tiku_tabs_add(&t, "Permissions");
        (void)tiku_tabs_select(&t, 1);
        tiku_tabs_draw(&t, sf, (tiku_rect_t){ 10, 170, 200, 22 });
        tiku_ui_gauge(sf, (tiku_rect_t){ 10, 200, 200, 10 }, 0.42f);
        tiku_ui_textfield(sf, (tiku_rect_t){ 10, 220, 200, 20 },
                          "renamed.txt", 11, TIKU_S_FOCUS);
        tiku_slider_init(&sl, 0, 100, 65, 5);
        tiku_slider_draw(&sl, sf, (tiku_rect_t){ 10, 250, 200, 16 });
        sf->record = NULL;

        check(tiku_dl_flatten(dl, wire, sizeof wire, &wrote) && wrote > 0,
              "narrate: the window flattens");
        (void)tiku_dl_narrate(wire, wrote, story, sizeof story);
        check(strstr(story, "a warning") != NULL,
              "narrate: the alert's kind arrives as a word, not a disc");
        check(strstr(story, "text \"Discard unsaved changes?\"") != NULL,
              "narrate: the question itself is readable");
        check(strstr(story, "button \"Discard\", the default") != NULL,
              "narrate: the default button says it is the default");
        check(strstr(story, "button \"Cancel\"") != NULL,
              "narrate: and the safe road is listed beside it");
        check(strstr(story,
              "tabs: \"Info\" \"Permissions\" (current)") != NULL,
              "narrate: which tab of which names is current");
        check(strstr(story, "gauge at 42%") != NULL,
              "narrate: the gauge is its number");
        check(strstr(story, "text field \"renamed.txt\", focused") != NULL,
              "narrate: the field, what is typed in it, and the focus");
        check(strstr(story, "slider at 65 of 0..100") != NULL,
              "narrate: the slider is a value on its scale");
        sf->record = dl;    /* detached above: reattach for the addendum */
        tiku_ui_checkbox(sf, (tiku_rect_t){ 10, 275, 120, 16 },
                              "Read only",
                              TIKU_S_ON | TIKU_S_DISABLED);
        sf->record = NULL;
        check(tiku_dl_flatten(dl, wire, sizeof wire, &wrote) && wrote > 0,
              "narrate: reflattens with the dead checkbox in it");
        (void)tiku_dl_narrate(wire, wrote, story, sizeof story);
        check(strstr(story, "checkbox \"Read only\", on, disabled")
                  != NULL,
              "narrate: a dead control says it is dead -- the wire always "
              "carried the bit, and only the button's line spoke it");
        {
            /* A button whose label never terminates: thirteen bytes of
             * payload and no NUL among them.  Read from a wire, that is
             * an attack or a corruption; either way the answer is to
             * STOP, not to walk past the payload's end reading whatever
             * memory says next. */
            unsigned char bad[4 + 13];
            size_t bat = 0;
            tiku_dl_fact_t bf;

            bad[0] = 19; bad[1] = 0;        /* the button op */
            bad[2] = 13; bad[3] = 0;        /* thirteen of payload */
            memset(bad + 4, 'X', 13);       /* and no terminator */
            check(!tiku_dl_read(bad, sizeof bad, &bat, &bf),
                  "narrate: a label with no end stops the reader rather "
                  "than sending it past the payload");
        }
        check(strstr(story, "FILL") == NULL &&
              strstr(story, "fill") == NULL,
              "narrate: the paint is not in the story");

        tiku_dl_free(dl);
        tiku_surface_free(sf);
    }

    printf("\n%s\n", fails?"FAILURES":"the stream is the window");
    return fails?1:0;
}
