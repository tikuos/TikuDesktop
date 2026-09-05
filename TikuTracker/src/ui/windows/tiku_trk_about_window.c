/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_about_window.c - the About box, in the R5 manner.
 *
 * The original About BeOS put the identity on the left and the facts
 * beside it.  This one keeps that shape and keeps the facts short: the
 * release, then only what the machine can answer for itself, how many
 * processors and how much memory is free.  A build date, a kernel
 * string, a processor model and the computer's name say more about the
 * host than about the system asked to describe itself, so they are out.
 * Written as a descriptor against the services contract, as a
 * third-party application is: it paints its own surface, asks for the
 * ANNOUNCE role rather than a position, and owns its colour cycle.  The
 * shell only hosts it, which is the boundary this panel demonstrates.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_logo.h"
#include "tiku_trk_task.h"
#include "tiku_trk_about_window.h"
#include "tiku_gfx.h"
#include "tiku_font.h"
#include "tiku_ui.h"
#include "tiku_window.h"

#define ABOUT_W 430

/* The one place the number lives. */
#define ABOUT_VERSION "0.1"

/*
 * The facts column already grew with the face -- every line of it steps
 * by plain->height -- but the window it was written into did not, so at
 * 16 px the last lines were drawn below their own frame.
 *
 * The column takes THREE steps of a line height now, where it took six:
 * the release, the processor count, and the memory over its gauge.  So
 * the window moves by three times whatever the line height moved by.
 * Written this way rather than by adding the parts up -- 171 is the old
 * 240 less the three lines that went, and what is left over the text is
 * margin that was chosen rather than derived.  Re-deriving the whole
 * from the lines alone would quietly throw it away.  At 12 px, the face
 * it was chosen against, it is 171.
 */
/*
 * The colour cycle.  A set is held, then carried into the next over a
 * little over a second, and the whole thing comes back round to the
 * amber it started on.  Held far longer than it fades, because the point
 * is a mark that is quietly alive, not one that is busy.
 *
 * FADE_STEPS is how many distinct pictures the fade is made of.  It is
 * NOT how smooth the fade can be -- the colours are interpolated, so it
 * could be any number -- it is how many frames the flourish is allowed
 * to cost.  Sixteen over 1.2 seconds is about thirteen a second, which
 * reads as continuous.
 */
#define HOLD_US     3000000
#define FADE_US     1200000
#define CYCLE_US    (HOLD_US + FADE_US)
#define FADE_STEPS  16

typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t            *surface;
    uint32_t                        id;
    /* Counted once at start; the memory is re-read per paint. */
    int                        ncpu;
    int64_t                    opened_us;
    /*
     * Which step of the colour cycle was last painted, and the second the
     * gauge was last read on.  Both are remembered so the tick can say
     * whether anything a person could see has actually changed: the
     * Deskbar's clock keeps the same discipline, and for the same reason
     * -- a window standing open must not cost a frame per tick.
     */
    int                        step;
    int64_t                    gauge_s;
} about_state_t;

/** @brief Which set, and how far into the fade, at @p now. */
static void
cycle_at(const about_state_t *st, int64_t now, int *set, float *toward)
{
    int64_t span = now - st->opened_us;
    int64_t into;

    if (span < 0) {
        span = 0;
    }
    into = span % CYCLE_US;
    *set = (int)(span / CYCLE_US);
    *toward = (into < HOLD_US)
                  ? 0.0f
                  : (float)(into - HOLD_US) / (float)FADE_US;
}

/** @brief The whole visible state of the cycle, as one number. */
static int
cycle_step(const about_state_t *st, int64_t now)
{
    int set;
    float t;

    cycle_at(st, now, &set, &t);
    return set * (FADE_STEPS + 1) + (int)(t * (float)FADE_STEPS);
}

/*
 * The window was 430 wide at the 12 px face and did not grow with it.
 * The facts column starts 150 in, so at 16 px "Memory: ... free" ran
 * off the side of the very box that was stating it -- the same fault
 * the heights had, in the other direction.
 *
 * Measured against a template of the widest numbers a host could have
 * rather than against the ones THIS host has, so the window does not
 * change width with the memory in it.  Never narrower than the 430 it
 * was: that number carries the identity column's margins, which are not
 * derived from any text.
 */
static int about_body_w(void)
{
    int longest = tiku_text_width(tiku_font_plain(),
                                  "Memory: 000000 MB of 000000 MB free");
    int need = 150 + longest + 18;

    return (need > ABOUT_W) ? need : ABOUT_W;
}

/**
 * @brief The body's height: the taller of the two columns, plus the
 *        two-line footer.
 */
static int about_body_h(void)
{
    int facts = 171 + 3 * (tiku_font_plain()->height - 15);
    /* The identity column has the mark in it now: sixteen above, eighty
     * of mark, then the wordmark and its rule and the second line.  The
     * taller of the two columns is what the window has to be. */
    int identity = 16 + 80 + 14 + tiku_font_at(28)->height + 6 + 3 + 14 +
                   tiku_font_plain()->height + 20;

    /* The footer is two lines now, not one -- the copyright with the
     * project page, then the licence -- and it runs under BOTH columns,
     * so whichever of them is the taller has to find the extra line. */
    return ((identity > facts) ? identity : facts)
           + tiku_font_plain()->height + 4;
}

/*
 * The pre-descriptor window declared its FRAME width as the body width,
 * chrome not counted -- an asymmetry (its height did count the chrome)
 * that every measured pixel in about.script stands on.  The content the
 * services give back is the body less two borders, so asking for that
 * puts the outer box, and every glyph in it, exactly where it has
 * always been.
 */
static int about_content_w(void)
{
    return about_body_w() - 2 * TIKU_WINDOW_BORDER;
}

/**
 * @brief How many processors, from /proc.
 *
 * The count and nothing else, not the model name: on a machine whose
 * cpuinfo does not carry one, the device tree answers instead with the
 * name of the COMPUTER, and an About box for an operating system then
 * announces the laptop it happens to be running on.
 */
static int
count_cpus(void)
{
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[256];
    int n = 0;

    while (f != NULL && fgets(line, sizeof line, f) != NULL) {
        if (strncmp(line, "processor", 9) == 0) {
            n++;
        }
    }
    if (f != NULL) {
        (void)fclose(f);
    }
    return (n > 0) ? n : 1;
}

/** @brief MemTotal and MemAvailable, in kilobytes. */
static void
read_memory(long *total_kb, long *avail_kb)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char line[128];

    *total_kb = 0;
    *avail_kb = 0;
    while (f != NULL && fgets(line, sizeof line, f) != NULL) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            (void)sscanf(line + 9, "%ld", total_kb);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            (void)sscanf(line + 13, "%ld", avail_kb);
        }
    }
    if (f != NULL) {
        (void)fclose(f);
    }
}

/**
 * @brief Paint the box: the mark and wordmark, the facts beside them, and
 *        the footer with the licence and the build.
 */
static void
paint(about_state_t *st)
{
    tiku_surface_t *surface = st->surface;
    const tiku_font_t *plain = tiku_font_plain();
    const tiku_font_t *big = tiku_font_at(28);
    const tiku_font_t *bold = tiku_font_bold();
    int left = 18;
    int facts_x = 150;
    int y;
    char line[160];

    tiku_fill(surface, (tiku_rect_t){ 0, 0, surface->w, surface->h },
                   TIKU_C_PANEL);
    {   /* The identity column: the MARK, then the wordmark under it over
         * its accent rule, the way the original wore its logo.  The mark
         * is drawn rather than stamped, so it is as sharp here at eighty
         * pixels as it is at sixteen on the Deskbar. */
        int mark = 80;
        int base;

        tiku_logo_palette_t p;
        int set;
        float toward;

        cycle_at(st, tiku_trk_now(), &set, &toward);
        tiku_logo_palette(set, toward, &p);
        tiku_logo_paint_with(surface, (tiku_rect_t){ left, 16, mark, mark },
                             TIKU_LOGO_GROUND, &p, 1.0f);
        base = 16 + mark + 14 + big->ascent;

        /* The wordmark reads across the rule -- it was "Tiku" over "OS
         * desktop", which is the product name split at the wrong place.
         * The name is TikuOS; what is under the rule is what this one of
         * them is. */
        tiku_text(surface, big, left, base, "TikuOS", TIKU_C_TEXT);
        tiku_fill(surface, (tiku_rect_t){ left, base + 6, 96, 3 },
                       TIKU_C_TAB);
        tiku_text(surface, bold, left,
                       base + 14 + plain->ascent, "desktop",
                       TIKU_C_TEXT);
    }
    y = 22;
    tiku_text(surface, bold, facts_x, y + plain->ascent,
                   "TikuDesktop " ABOUT_VERSION, TIKU_C_TEXT);
    y += plain->height + 10;
    snprintf(line, sizeof line, "Processors: %d", st->ncpu);
    tiku_text(surface, plain, facts_x, y + plain->ascent, line,
                   TIKU_C_TEXT);
    y += plain->height + 10;
    {   /* Memory, with its gauge: what is in use, over what there is. */
        long total = 0, avail = 0;
        tiku_rect_t gauge = { facts_x, y + plain->height + 4,
                                   about_body_w() - 150 - 36, 10 };
        long used;

        read_memory(&total, &avail);
        used = (total > avail) ? total - avail : 0;
        /* The number says what is FREE; the bar fills with what is not,
         * which is the direction a bar is read in.  They are the two
         * ends of the same figure, so neither has to be worked out from
         * the other. */
        snprintf(line, sizeof line, "Memory: %ld MB of %ld MB free",
                 avail / 1024, total / 1024);
        tiku_text(surface, plain, facts_x, y + plain->ascent, line,
                       TIKU_C_TEXT);
        tiku_ui_gauge(surface, gauge,
                      (total > 0) ? (float)used / (float)total : 0.0f);
    }
    {   /* Whose it is, where it lives, and on what terms.  Two lines
         * rather than one because the three together are 405 pixels at
         * the 12 px face and 526 at 16, against the 394 the window has
         * between its margins -- so the licence goes under the rest
         * instead of running off the side of the box that states it. */
        int foot = surface->h - 14;

        tiku_text(surface, plain, left, foot - plain->height - 4,
                       "(c) 2026 the TikuOS project   http://tiku-os.org",
                       TIKU_C_TEXT);
        tiku_text(surface, plain, left, foot,
                       "Apache License 2.0", TIKU_C_TEXT);
        /* WHICH build, on the box itself: a development machine grows
         * copies, and the one question worth answering at a glance is
         * whether the window in front of you is today's. */
        {
            char built[64];
            int w;

            snprintf(built, sizeof built, "built %s %s", __DATE__,
                     __TIME__);
            w = tiku_text_width(plain, built);
            tiku_text(surface, plain, surface->w - 14 - w, foot,
                           built, TIKU_C_TEXT);
        }
    }
}

/** @brief Paint and hand the frame over. */
static void
show(about_state_t *st)
{
    paint(st);
    (void)st->services->frame(st->services->ctx, st->id,
                              st->surface->px, st->surface->w,
                              st->surface->h);
}

/**
 * @brief Open the box through the services and paint its first frame.
 *
 * @return 0 with @p state set, -1 when the services lack open or frame or
 *         the window could not be made.
 */
static int
about_start(void **state, const tiku_app_services_t *services)
{
    about_state_t *st = calloc(1, sizeof *st);
    int w, h;

    if (st == NULL || services == NULL || services->open == NULL ||
        services->frame == NULL) {
        free(st);
        return -1;
    }
    st->services = services;
    st->ncpu = count_cpus();
    st->opened_us = tiku_trk_now();
    st->step = -1;
    w = about_content_w();
    h = about_body_h();
    st->surface = tiku_surface_new(w, h, TIKU_C_PANEL);
    if (st->surface == NULL) {
        free(st);
        return -1;
    }
    st->id = services->open(services->ctx, "About this system", w, h);
    if (st->id == 0) {
        tiku_surface_free(st->surface);
        free(st);
        return -1;
    }
    /* A role, not a position: where a panel that announces goes is the
     * HOST's guideline, and a host too old to have the service places it
     * however it places anything.  PANEL rather than ANNOUNCE: this box
     * is opened from a menu and stays until it is dismissed, so it wears
     * the tab and close a person reaches for. */
    if (services->place != NULL) {
        (void)services->place(services->ctx, st->id,
                              TIKU_APP_PLACE_PANEL);
    }
    show(st);
    *state = st;
    return 0;
}

/** @brief Close the window and free what the box held. */
static void
about_stop(void *state)
{
    about_state_t *st = state;

    if (st == NULL) {
        return;
    }
    if (st->services->close != NULL) {
        st->services->close(st->services->ctx, st->id);
    }
    tiku_surface_free(st->surface);
    free(st);
}

/** @brief Escape asks to close; every other event is watched and ignored. */
static int
about_event(void *state, const tiku_event_t *event)
{
    (void)state;
    /* Escape closes it, as a panel that announces should.  Everything
     * else -- there are no controls -- is watched and ignored. */
    if (event != NULL && event->type == TIKU_EVENT_KEY_DOWN &&
        event->key == TIKU_KEY_ESCAPE) {
        return 1;
    }
    return 0;
}

/**
 * @brief Refit the surface when the face changed, and advance the colour
 *        cycle, repainting when either moved.
 */
static void
about_tick(void *state, int64_t now_us)
{
    about_state_t *st = state;
    int step, want_w, want_h;
    int64_t sec;
    int owe = 0;

    if (st == NULL) {
        return;
    }
    /* Nothing this window does changes the face; the Fonts window next
     * door does, and this one has to notice on its own.  The host keeps
     * the corner where it is, so a refit does not jump under the
     * pointer. */
    want_w = about_content_w();
    want_h = about_body_h();
    if (want_w != st->surface->w || want_h != st->surface->h) {
        tiku_surface_t *grown = tiku_surface_new(want_w, want_h,
                                                 TIKU_C_PANEL);

        if (grown != NULL) {
            tiku_surface_free(st->surface);
            st->surface = grown;
            if (st->services->resize != NULL) {
                (void)st->services->resize(st->services->ctx, st->id,
                                           want_w, want_h);
            }
            owe = 1;
        }
    }
    step = cycle_step(st, now_us);
    if (step != st->step) {
        st->step = step;
        owe = 1;
    }
    /* The gauge follows the machine underneath, on a beat a person can
     * see and a loop cannot feel. */
    sec = now_us / 1000000;
    if (sec != st->gauge_s) {
        st->gauge_s = sec;
        owe = 1;
    }
    if (owe) {
        show(st);
    }
}

const tiku_app_descriptor_t tiku_trk_about_app = {
    .id = "org.tikuos.about",
    .name = "About",
    .start = about_start,
    .stop = about_stop,
    .event = about_event,
    .tick = about_tick,
};
