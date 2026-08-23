/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_about_window.c - the About box, in the R5 manner.
 *
 * The original About BeOS put the identity on the left and the facts
 * beside it: release, kernel, processors, memory.  This one keeps that
 * shape and reads its facts from the host underneath -- uname for the
 * kernel, /proc for the processors, the memory and the uptime -- because
 * an About box that recited constants would be decoration, not
 * information.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

#include "tiku_logo.h"
#include "tiku_trk_task.h"
#include "tiku_trk_about_window.h"
#include "tiku_gfx.h"
#include "tiku_font.h"

#define ABOUT_W 430

/*
 * The facts column already grew with the face -- every line of it steps
 * by plain->height -- but the window it was written into did not, so at
 * 16 px the last lines were drawn below their own frame.
 *
 * The column takes SIX steps of a line height between its first fact and
 * its uptime, so the window moves by six times whatever the line height
 * moved by.  Written this way rather than by adding the parts up: 240 was
 * chosen for the margin it leaves as much as for the text it holds, and
 * re-deriving it from the text alone would quietly throw that margin
 * away.  At 12 px, the face it was chosen against, it is still 240.
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

static int about_body_h(void)
{
    int facts = 240 + 6 * (tiku_font_plain()->height - 15);
    /* The identity column has the mark in it now: sixteen above, eighty
     * of mark, then the wordmark and its rule and the second line.  The
     * taller of the two columns is what the window has to be. */
    int identity = 16 + 80 + 14 + tiku_font_at(28)->height + 6 + 3 + 14 +
                   tiku_font_plain()->height + 20;

    return (identity > facts) ? identity : facts;
}

/** @brief First "model name" and the processor count, from /proc. */
static void
read_cpu(char *out, size_t max, int *count)
{
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[256];

    *count = 0;
    out[0] = '\0';
    while (f != NULL && fgets(line, sizeof line, f) != NULL) {
        if (strncmp(line, "processor", 9) == 0) {
            (*count)++;
        }
        if (out[0] == '\0' &&
            (strncmp(line, "model name", 10) == 0 ||
             strncmp(line, "Model", 5) == 0)) {
            const char *colon = strchr(line, ':');

            if (colon != NULL) {
                size_t n;

                colon++;
                while (*colon == ' ') { colon++; }
                snprintf(out, max, "%s", colon);
                n = strlen(out);
                if (n > 0u && out[n - 1u] == '\n') {
                    out[n - 1u] = '\0';
                }
            }
        }
    }
    if (f != NULL) {
        (void)fclose(f);
    }
    if (out[0] == '\0') {
        /* An ARM host names itself in the device tree, not in cpuinfo. */
        FILE *dt = fopen("/proc/device-tree/model", "r");

        if (dt != NULL) {
            size_t n = fread(out, 1u, max - 1u, dt);

            out[n] = '\0';
            (void)fclose(dt);
        }
    }
    if (out[0] == '\0') {
        snprintf(out, max, "unknown processor");
    }
    if (*count <= 0) {
        *count = 1;
    }
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

/** @brief Seconds this host has been up. */
static long
read_uptime(void)
{
    FILE *f = fopen("/proc/uptime", "r");
    double up = 0.0;

    if (f != NULL) {
        (void)fscanf(f, "%lf", &up);
        (void)fclose(f);
    }
    return (long)up;
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

    (void)window;
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

        tiku_text(surface, big, left, base, "Tiku", TIKU_C_TEXT);
        tiku_fill(surface, (tiku_rect_t){ left,
                       base + 6, 96, 3 }, TIKU_C_TAB);
        tiku_text(surface, bold, left,
                       base + 14 + plain->ascent, "OS desktop",
                       TIKU_C_TEXT);
    }
    y = content.y + 22;
    snprintf(line, sizeof line, "TikuTracker, built %s", __DATE__);
    tiku_text(surface, bold, facts_x, y + plain->ascent, line,
                   TIKU_C_TEXT);
    y += plain->height + 10;
    tiku_text(surface, plain, facts_x, y + plain->ascent,
                   controller->kernel, TIKU_C_TEXT);
    y += plain->height + 10;
    snprintf(line, sizeof line, "%d processor%s:", controller->ncpu,
             controller->ncpu == 1 ? "" : "s");
    tiku_text(surface, plain, facts_x, y + plain->ascent, line,
                   TIKU_C_TEXT);
    y += plain->height + 2;
    tiku_text(surface, plain, facts_x + 12, y + plain->ascent,
                   controller->cpu, TIKU_C_TEXT);
    y += plain->height + 10;
    {   /* Memory, with its gauge: what is in use, over what there is. */
        long total = 0, avail = 0;
        tiku_rect_t gauge = { facts_x, y + plain->height + 4,
                                   ABOUT_W - 150 - 36, 10 };
        long used;

        read_memory(&total, &avail);
        used = (total > avail) ? total - avail : 0;
        snprintf(line, sizeof line, "%ld MB of %ld MB in use",
                 used / 1024, total / 1024);
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
        y = gauge.y + gauge.h + 12;
    }
    {   /* The uptime ticks while the window stands. */
        long up = read_uptime();

        snprintf(line, sizeof line, "up %ld day%s, %ld:%02ld",
                 up / 86400, (up / 86400) == 1 ? "" : "s",
                 (up % 86400) / 3600, (up % 3600) / 60);
        tiku_text(surface, plain, facts_x, y + plain->ascent, line,
                       TIKU_C_TEXT);
    }
    tiku_text(surface, plain, left,
                   content.y + content.h - 14,
                   "(c) 2026 the TikuOS project", TIKU_C_TEXT);
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
    struct utsname un;

    if (controller->window != NULL && controller->window->open) {
        tiku_workspace_activate(controller->workspace,
                                     controller->window);
        *controller->dirty = 1;
        return;
    }
    if (uname(&un) == 0) {
        snprintf(controller->kernel, sizeof controller->kernel,
                 "%s %s (%s)", un.sysname, un.release, un.machine);
    } else {
        snprintf(controller->kernel, sizeof controller->kernel,
                 "unknown kernel");
    }
    read_cpu(controller->cpu, sizeof controller->cpu, &controller->ncpu);
    controller->opened_us = tiku_trk_now();
    controller->step = -1;
    frame.w = ABOUT_W;
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
