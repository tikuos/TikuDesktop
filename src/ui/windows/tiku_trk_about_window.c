/*
 * The new Tracker for TikuOS.
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

#include "tiku_trk_about_window.h"
#include "tiku_gfx.h"
#include "tiku_font.h"

#define ABOUT_W 430
#define ABOUT_H 240

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
    {   /* The identity column: wordmark over its accent rule, the way
         * the original wore its logo. */
        int base = content.y + 24 + big->ascent;

        tiku_text(surface, big, left, base, "Tiku", TIKU_C_TEXT);
        tiku_fill(surface, (tiku_rect_t){ left,
                       base + 6, 96, 3 }, TIKU_C_TAB);
        tiku_text(surface, bold, left,
                       base + 14 + plain->ascent, "OS desktop",
                       TIKU_C_TEXT);
    }
    y = content.y + 22;
    snprintf(line, sizeof line, "Tracker, built %s", __DATE__);
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
    frame.w = ABOUT_W;
    frame.h = ABOUT_H + tiku_window_tab_h() + 2 * TIKU_WINDOW_BORDER;
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
