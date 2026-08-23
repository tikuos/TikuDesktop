/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_about_window.c - the About box, in the R5 manner.
 *
 * The original About BeOS put the identity on the left and the facts
 * beside it.  This one keeps that shape and keeps the facts SHORT: the
 * release it is, and then only what the machine can answer for itself --
 * how many processors there are and how much memory is free.
 *
 * What is not here was taken out on purpose.  A build date, a kernel
 * string, a processor model and the name of the computer tell a person
 * nothing they can act on, and every one of them says more about the
 * host than about the system that is being asked to describe itself.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_logo.h"
#include "tiku_trk_task.h"
#include "tiku_trk_about_window.h"
#include "tiku_gfx.h"
#include "tiku_font.h"

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
 * could be any number -- it is how many repaints of the desktop the
 * flourish is allowed to cost.  Sixteen over 1.2 seconds is about
 * thirteen a second, which reads as continuous.
 */
#define HOLD_US     3000000
#define FADE_US     1200000
#define CYCLE_US    (HOLD_US + FADE_US)
#define FADE_STEPS  16

/** @brief Which set, and how far into the fade, at @p now. */
static void
cycle_at(const tiku_trk_about_window_t *c, int64_t now, int *set,
         float *toward)
{
    int64_t span = now - c->opened_us;
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
cycle_step(const tiku_trk_about_window_t *c, int64_t now)
{
    int set;
    float t;

    cycle_at(c, now, &set, &t);
    return set * (FADE_STEPS + 1) + (int)(t * (float)FADE_STEPS);
}

int
tiku_trk_about_window_pulse(tiku_trk_about_window_t *controller,
                            int64_t now_us)
{
    int step;

    if (controller == NULL || controller->window == NULL ||
        !controller->window->open) {
        return 0;
    }
    step = cycle_step(controller, now_us);
    if (step == controller->step) {
        return 0;               /* holding: nothing a person could see */
    }
    controller->step = step;
    return 1;
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

/**
 * @brief How many processors, from /proc.
 *
 * The count and nothing else.  The model name used to be read too --
 * and on a machine whose cpuinfo does not carry one, the device tree
 * was asked instead, which answers with the name of the COMPUTER.  That
 * is how an About box for an operating system ended up announcing the
 * laptop it happened to be running on.
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

/*
 * Nothing this window does changes the face; the Fonts window next door
 * does, and this one has to notice on its own -- so the fit is taken on
 * every draw, the way Get Info takes its own.  The corner stays where it
 * is: a window that re-placed itself would jump under the pointer.
 */
static void
refit(tiku_window_t *window, int *dirty)
{
    int want_w = about_body_w();
    int want_h = about_body_h() + tiku_window_tab_h() +
                 2 * TIKU_WINDOW_BORDER;

    if (window == NULL || !window->open ||
        (window->frame.w == want_w && window->frame.h == want_h)) {
        return;
    }
    window->frame.w = want_w;
    window->frame.h = want_h;
    if (dirty != NULL) {
        *dirty = 1;
    }
}

static void
draw(tiku_window_t *window, tiku_surface_t *surface,
     tiku_rect_t content, void *context)
{
    tiku_trk_about_window_t *controller = context;
    const tiku_font_t *plain = tiku_font_plain();
    const tiku_font_t *big = tiku_font_at(28);
    const tiku_font_t *bold = tiku_font_bold();
    int left = content.x + 18;
    int facts_x = content.x + 150;
    int y;
    char line[160];

    refit(window, controller->dirty);
    content = tiku_window_content(window);
    tiku_fill(surface, content, TIKU_C_PANEL);
    {   /* The identity column: the MARK, then the wordmark under it over
         * its accent rule, the way the original wore its logo.  The mark
         * is drawn rather than stamped, so it is as sharp here at eighty
         * pixels as it is at sixteen on the Deskbar. */
        int mark = 80;
        int base;

        tiku_logo_palette_t p;
        int set;
        float toward;

        cycle_at(controller, tiku_trk_now(), &set, &toward);
        tiku_logo_palette(set, toward, &p);
        tiku_logo_paint_with(surface, (tiku_rect_t){ left, content.y + 16,
                                                     mark, mark },
                             TIKU_LOGO_GROUND, &p, 1.0f);
        base = content.y + 16 + mark + 14 + big->ascent;

        /* The wordmark reads across the rule -- it was "Tiku" over "OS
         * desktop", which is the product name split at the wrong place.
         * The name is TikuOS; what is under the rule is what this one of
         * them is. */
        tiku_text(surface, big, left, base, "TikuOS", TIKU_C_TEXT);
        tiku_fill(surface, (tiku_rect_t){ left,
                       base + 6, 96, 3 }, TIKU_C_TAB);
        tiku_text(surface, bold, left,
                       base + 14 + plain->ascent, "desktop",
                       TIKU_C_TEXT);
    }
    y = content.y + 22;
    tiku_text(surface, bold, facts_x, y + plain->ascent,
                   "TikuDesktop " ABOUT_VERSION, TIKU_C_TEXT);
    y += plain->height + 10;
    snprintf(line, sizeof line, "Processors: %d", controller->ncpu);
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
        tiku_fill(surface, gauge, TIKU_C_DOC);
        tiku_frame(surface, gauge,
                        tiku_tint(TIKU_C_PANEL, 1.40f));
        if (total > 0) {
            tiku_rect_t fillr = { gauge.x + 1, gauge.y + 1,
                (int)((long)(gauge.w - 2) * used / total), gauge.h - 2 };

            tiku_fill(surface, fillr, TIKU_C_SELECT);
        }
    }
    {   /* Whose it is, where it lives, and on what terms.  Two lines
         * rather than one because the three together are 405 pixels at
         * the 12 px face and 526 at 16, against the 394 the window has
         * between its margins -- so the licence goes under the rest
         * instead of running off the side of the box that states it. */
        int foot = content.y + content.h - 14;

        tiku_text(surface, plain, left, foot - plain->height - 4,
                       "(c) 2026 the TikuOS project   http://tiku-os.org",
                       TIKU_C_TEXT);
        tiku_text(surface, plain, left, foot,
                       "Apache License 2.0", TIKU_C_TEXT);
    }
}

void
tiku_trk_about_window_init(tiku_trk_about_window_t *controller,
    tiku_workspace_t *workspace, int screen_w, int screen_h,
    int *dirty)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
}

void
tiku_trk_about_window_open(tiku_trk_about_window_t *controller)
{
    tiku_rect_t frame;

    if (controller->window != NULL && controller->window->open) {
        tiku_workspace_activate(controller->workspace,
                                     controller->window);
        *controller->dirty = 1;
        return;
    }
    controller->ncpu = count_cpus();
    controller->opened_us = tiku_trk_now();
    controller->step = -1;
    frame.w = about_body_w();
    frame.h = about_body_h() + tiku_window_tab_h() +
              2 * TIKU_WINDOW_BORDER;
    frame.x = (controller->screen_w - frame.w) / 2;
    /* The UPPER part of the screen, as the guideline places a panel that
     * announces something. */
    frame.y = (controller->screen_h - frame.h) / 3;
    controller->window = tiku_workspace_open(controller->workspace,
        "About this system", frame, draw, controller, controller);
    if (controller->window != NULL) {
        tiku_window_set_size_controls(controller->window, 0, 0);
    }
    *controller->dirty = 1;
}

void
tiku_trk_about_window_close(tiku_trk_about_window_t *controller)
{
    if (controller->window != NULL) {
        tiku_workspace_close(controller->workspace,
                                  controller->window);
        controller->window = NULL;
        *controller->dirty = 1;
    }
}

tiku_window_t *
tiku_trk_about_window_handle(tiku_trk_about_window_t *controller)
{
    return (controller != NULL && controller->window != NULL &&
            controller->window->open) ? controller->window : NULL;
}

int
tiku_trk_about_window_key(tiku_trk_about_window_t *controller,
                          unsigned key)
{
    if (tiku_trk_about_window_handle(controller) == NULL ||
        tiku_workspace_focused(controller->workspace) !=
            controller->window) {
        return 0;
    }
    if (key == TIKU_KEY_ESCAPE) {
        tiku_trk_about_window_close(controller);
        return 1;
    }
    return 0;
}
