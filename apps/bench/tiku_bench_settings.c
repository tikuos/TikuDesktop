/* tiku_bench_settings.c -- settings.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_bench_private.h"

static void settings_path(bench_t *st);
static void settings_line(bench_t *st, char *line);

/* The run options remembered between sessions: one key=value file under
 * the user's config directory, the keys the GTK frontend's own. Read once
 * at start, written whenever the options differ from what was written. */
static void settings_path(bench_t *st)
{
    const char *env = getenv("TIKU_BENCH_SETTINGS"), *home = getenv("HOME");
    if (env && *env) {
        snprintf(st->settings, sizeof st->settings, "%s", env); return;
    }
    if (!home || !*home) { st->settings[0] = '\0'; return; }
    snprintf(st->settings, sizeof st->settings,
             "%.400s/.config/tikubench-desktop/settings", home);
}

/** The options as the file spells them; the same text decides a rewrite. */
void tiku_bench_settings_text(const bench_t *st, char *out, size_t cap)
{
    snprintf(out, cap,
             "board=%s\nbaud=%s\nflash=%s\nmake_vars=%s\nextra_cflags=%s\n"
             "pk_hw=%s\nmem_model=%s\nconsole=%s\nepaper_enable=%s\n"
             "epaper_panel=%d\nepaper_pattern=%d\n",
             st->target_name, st->field[1].text, st->flash ? "true" : "false",
             st->field[2].text, st->field[3].text, st->pk ? "true" : "false",
             st->large ? "large" : "small", st->usb ? "usb" : "uart",
             st->epaper ? "true" : "false",
             st->epanel.selected < 0 ? 0 : st->epanel.selected,
             st->epattern.selected < 0 ? 0 : st->epattern.selected);
}

/** Write the options when they changed; the directory is made on the way. */
void tiku_bench_settings_flush(bench_t *st)
{
    char text[sizeof st->saved], dir[sizeof st->settings];
    if (!*st->settings) return;
    tiku_bench_settings_text(st, text, sizeof text);
    if (!strcmp(text, st->saved)) return;
    snprintf(dir, sizeof dir, "%s", st->settings);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = '\0';
        char *up = strrchr(dir, '/');
        if (up && up != dir) { *up = '\0'; (void)mkdir(dir, 0777); *up = '/'; }
        (void)mkdir(dir, 0777);
    }
    FILE *f = fopen(st->settings, "w");
    if (!f) return;
    (void)fputs(text, f);
    if (fclose(f) == 0) snprintf(st->saved, sizeof st->saved, "%s", text);
}

/** One line of the file: a known key sets its option, the rest is skipped. */
static void settings_line(bench_t *st, char *line)
{
    char *eq = strchr(line, '='), *nl = strchr(line, '\n');
    if (nl) *nl = '\0';
    if (!eq) return;
    *eq = '\0';
    const char *key = line, *value = eq + 1;
    int on = !strcmp(value, "true") || !strcmp(value, "1");
    if (!strcmp(key, "board"))
        snprintf(st->wanted, sizeof st->wanted, "%s", value);
    else if (!strcmp(key, "baud") && *value)
        tiku_field_start(&st->field[1], value);
    else if (!strcmp(key, "make_vars"))
        tiku_field_start(&st->field[2], value);
    else if (!strcmp(key, "extra_cflags"))
        tiku_field_start(&st->field[3], value);
    else if (!strcmp(key, "flash")) st->flash = on;
    else if (!strcmp(key, "pk_hw")) st->pk = on;
    else if (!strcmp(key, "epaper_enable")) st->epaper = on;
    else if (!strcmp(key, "mem_model")) st->large = !strcmp(value, "large");
    else if (!strcmp(key, "console")) st->usb = !strcmp(value, "usb");
    else if (!strcmp(key, "epaper_panel") || !strcmp(key, "epaper_pattern")) {
        tiku_choice_t *c = !strcmp(key, "epaper_panel") ? &st->epanel
                                                        : &st->epattern;
        int i = atoi(value);
        if (i >= 0 && i < c->count) c->selected = i;
    }
}

/** Read the file at start; a missing file is the defaults, nothing more. */
void tiku_bench_settings_load(bench_t *st)
{
    char line[TIKU_FIELD_MAX + 32];
    settings_path(st);
    if (!*st->settings) return;
    FILE *f = fopen(st->settings, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) settings_line(st, line);
    (void)fclose(f);
}
