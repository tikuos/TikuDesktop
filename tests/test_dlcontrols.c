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
