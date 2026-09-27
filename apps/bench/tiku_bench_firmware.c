/* tiku_bench_firmware.c -- the Firmware tab.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_bench_private.h"

/* A front end over `tikubench firmware --events tsv`.  The tool owns the
 * options (the Makefile's own), the build, the flash and the checks that
 * make flashing safe; this tab asks it, shows what it says, and hands it
 * the board.  Nothing here builds or flashes by itself. */

#define FW_FIELDS 40
#define FW_COPY(dst, src) snprintf(dst, sizeof(dst), "%s", src)
enum { QUERY_PRESETS = 1, QUERY_OPTIONS };
static const char *const stage_name[FW_NSTAGE] = {
    "check", "build", "flash", "verify"
};

/** Append one word to @p c's argv; 0 when it is full. */
static int arg(command_t *c, const char *word)
{
    if (c->argc >= 127) return 0;
    c->argv[c->argc++] = (char *)word; c->argv[c->argc] = NULL; return 1;
}

/**
 * @brief One row of what the desktop keeps about the board.
 * @return 1 with the row, 0 when the board has none, -1 when nothing is
 *         kept about it yet.
 */
static int stat_row(const bench_t *st, const char *label, char *out,
                    size_t cap)
{
    char text[1024];
    size_t n = strlen(label);
    out[0] = '\0';
    if (!st->services->device_stats || !*st->target_name) return -1;
    text[0] = '\0';
    st->services->device_stats(st->services->ctx, st->target_name, text,
                               sizeof text);
    if (!text[0]) return -1;
    for (const char *p = text; *p;) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len > n && !strncmp(p, label, n) && p[n] == '\t') {
            snprintf(out, cap, "%.*s", (int)(len - n - 1), p + n + 1);
            return 1;
        }
        if (!nl) break;
        p = nl + 1;
    }
    return 0;
}

/** The preset the dropdown shows, or NULL. */
static const fw_preset_t *fw_selected(const bench_t *st)
{
    int i = st->fw.choice.selected;
    return i >= 0 && i < st->fw.npreset ? &st->fw.preset[i] : NULL;
}

/** The --set words: the options changed here, then the Extra field's. */
static int fw_sets(bench_t *st, command_t *c, int extra)
{
    for (int i = 0; i < st->fw.nset; i++) {
        if (c->nword == 32) return 0;
        snprintf(c->words[c->nword], sizeof c->words[0], "%s=%d",
                 st->fw.set[i].name, st->fw.set[i].value);
        arg(c, "--set"); arg(c, c->words[c->nword++]);
    }
    if (!extra) return 1;
    char vars[TIKU_FIELD_MAX], *save = NULL;
    snprintf(vars, sizeof vars, "%s", st->field[F_FWVARS - F_FILTER].text);
    for (char *p = strtok_r(vars, " \t", &save); p;
         p = strtok_r(NULL, " \t", &save)) {
        char *eq = strchr(p, '=');
        if (!eq || eq == p || c->nword == 32) return 0;
        for (char *q = p; q < eq; q++)
            if (!isalnum((unsigned char)*q) && *q != '_') return 0;
        snprintf(c->words[c->nword], sizeof c->words[0], "%s", p);
        arg(c, "--set"); arg(c, c->words[c->nword++]);
    }
    return 1;
}

/** argv for one `tikubench firmware` action; 0 when it cannot be said. */
static int fw_command(bench_t *st, const char *action, command_t *c)
{
    const char *base[] = {"python3", "-u", "-m", "tikubench", "firmware",
                          action};
    memset(c, 0, sizeof *c);
    for (size_t i = 0; i < sizeof base / sizeof base[0]; i++)
        arg(c, base[i]);
    if (!strcmp(action, "presets")) {
        /* The die's name, and the board the image says it was built
         * for, name the board without opening its port. */
        if (!*st->mcu) return 0;
        arg(c, "--mcu"); arg(c, st->mcu);
        if (stat_row(st, "board", c->words[c->nword],
                     sizeof c->words[0]) > 0) {
            arg(c, "--os-board"); arg(c, c->words[c->nword++]);
        }
        if (*st->port) { arg(c, "--port"); arg(c, st->port); }
    } else {
        const fw_preset_t *p = fw_selected(st);
        if (!p || !*st->fw.board) return 0;
        arg(c, "--board"); arg(c, st->fw.board);
        arg(c, "--preset"); arg(c, p->key);
        if (strcmp(action, "options")) {
            if (!*st->port) return 0;
            arg(c, "--port"); arg(c, st->port);
        }
        if (!fw_sets(st, c, strcmp(action, "options") != 0)) return 0;
    }
    arg(c, "--events"); arg(c, "tsv");
    return 1;
}

/** Forget what the last run said: its steps, preview and states. */
static void fw_stages_clear(bench_t *st)
{
    for (int i = 0; i < FW_NSTAGE; i++)
        st->fw.stage[i][0] = st->fw.stage_text[i][0] = '\0';
    st->fw.preview[0] = st->fw.pnote[0] = st->fw.image[0] = '\0';
    st->fw.status[0] = '\0'; st->fw.ok = 0;
}

/** Ask the tool for the presets or the options; the board stays ours. */
static void fw_query(bench_t *st, int which)
{
    command_t c;
    if (!fw_command(st, which == QUERY_PRESETS ? "presets" : "options", &c))
        return;
    if (which == QUERY_PRESETS) {
        /* The changes made here outlive the asking unless the answer
         * names another board (tiku_bench_fw_line). */
        const fw_preset_t *was = fw_selected(st);
        if (was) FW_COPY(st->fw.keep, was->key);
        st->fw.npreset = st->fw.noption = st->fw.nrow = 0;
        st->fw.error[0] = '\0';
        tiku_choice_init(&st->fw.choice);
        tiku_list_set_count(&st->list[TAB_FIRMWARE], 0);
    }
    st->fw.want_options = 0;
    st->fw.step = FWS_QUERY; st->fw.query = which;
    if (!tiku_bench_process_spawn(st, c.argv)) {
        st->fw.step = FWS_IDLE;
        snprintf(st->fw.error, sizeof st->fw.error,
                 "Could not start tikubench (python3 in %.200s)",
                 st->directory);
    }
    st->dirty = 1;
}

void tiku_bench_fw_poll(bench_t *st)
{
    char mcu[64] = "", port[256] = "", osb[64] = "", key[256];
    if (st->tabs.current != TAB_FIRMWARE || st->child || st->busy ||
        st->loading || st->closing || st->confirm.open) return;
    if (*st->target_name && st->services->board_mcu)
        st->services->board_mcu(st->services->ctx, st->target_name, mcu,
                                sizeof mcu);
    if (*st->target_name && st->services->board_port)
        st->services->board_port(st->services->ctx, st->target_name, port,
                                 sizeof port);
    for (char *p = mcu; *p; p++) *p = (char)tolower((unsigned char)*p);
    (void)stat_row(st, "board", osb, sizeof osb);
    snprintf(key, sizeof key, "%.60s|%.180s|%.60s", mcu, port, osb);
    if (strcmp(key, st->fw.asked)) {
        /* Another board, or the same one answering differently: ask
         * again, once, whatever the answer. */
        snprintf(st->fw.asked, sizeof st->fw.asked, "%s", key);
        if (strcmp(mcu, st->mcu) || strcmp(port, st->port))
            tiku_bench_target_capture(st);
        fw_stages_clear(st);
        if (!*mcu) {
            st->fw.npreset = st->fw.noption = st->fw.nrow = 0;
            st->fw.board[0] = '\0'; tiku_choice_init(&st->fw.choice);
            snprintf(st->fw.error, sizeof st->fw.error,
                     "No board. Connect one through Devices.");
            st->dirty = 1; return;
        }
        fw_query(st, QUERY_PRESETS);
    } else if (st->fw.want_options) fw_query(st, QUERY_OPTIONS);
}

/** Split a record at its tabs, in place; @return how many fields. */
static int fw_split(char *line, char **f)
{
    int n = 0;
    for (char *p = line; n < FW_FIELDS;) {
        f[n++] = p;
        char *tab = strchr(p, '\t');
        if (!tab) break;
        *tab = '\0'; p = tab + 1;
    }
    return n;
}

/** The value of a record's @p key=, or "" when it has none. */
static const char *fw_get(char **f, int n, const char *key)
{
    size_t len = strlen(key);
    for (int i = 1; i < n; i++)
        if (!strncmp(f[i], key, len) && f[i][len] == '=') return f[i] + len + 1;
    return "";
}

/** Group the options under their headings, in the order they came. */
static void fw_rows(bench_t *st)
{
    fw_t *fw = &st->fw;
    fw->nrow = fw->ngroup = 0;
    for (int i = 0; i < fw->noption; i++) {
        int g = 0;
        while (g < fw->ngroup && strcmp(fw->group[g], fw->option[i].group)) g++;
        if (g == fw->ngroup && fw->ngroup < 16)
            FW_COPY(fw->group[fw->ngroup++], fw->option[i].group);
    }
    for (int g = 0; g < fw->ngroup; g++) {
        fw->row[fw->nrow++] = -1 - g;
        for (int i = 0; i < fw->noption; i++)
            if (!strcmp(fw->option[i].group, fw->group[g]))
                fw->row[fw->nrow++] = i;
    }
    tiku_list_set_count(&st->list[TAB_FIRMWARE], fw->nrow);
}

/** A step began or ended: kept for the pills, logged, and noted. */
static void fw_stage(bench_t *st, char **f, int n)
{
    const char *name = fw_get(f, n, "stage"), *state = fw_get(f, n, "state");
    const char *text = fw_get(f, n, "text");
    char line[LINE_MAXIMUM];
    for (int i = 0; i < FW_NSTAGE; i++) {
        if (strcmp(name, stage_name[i])) continue;
        FW_COPY(st->fw.stage[i], state); FW_COPY(st->fw.stage_text[i], text);
    }
    snprintf(line, sizeof line, "[%s] %s%s%s", name,
             !strcmp(state, "start") ? ".." : !strcmp(state, "fail")
             ? "FAILED" : state, *text ? " " : "", text);
    tiku_bench_log_line(st, line, 1);
    snprintf(st->note, sizeof st->note, "%.500s", line);
}

/** Whether @p line is a record: a type of lowercase words, a tab after. */
static int fw_is_record(const char *line)
{
    size_t n = strcspn(line, "\t");
    if (!n || !line[n]) return 0;
    for (size_t i = 0; i < n; i++)
        if (!islower((unsigned char)line[i]) && line[i] != '_' && line[i] != ':')
            return 0;
    return 1;
}

void tiku_bench_fw_line(bench_t *st, char *line)
{
    fw_t *fw = &st->fw;
    char *f[FW_FIELDS];
    if (!fw_is_record(line)) {
        /* Python or make speaking for itself, which is how a failure
         * that never reached the tool is seen. */
        if (*line) tiku_bench_log_line(st, line, 1);
        return;
    }
    int n = fw_split(line, f);
    const char *type = f[0];
    if (!strcmp(type, "presets")) {
        if (strcmp(fw->board, fw_get(f, n, "board"))) {
            fw->nset = 0; fw->keep[0] = '\0';     /* another board */
        }
        FW_COPY(fw->board, fw_get(f, n, "board")); fw->npreset = 0;
    } else if (!strcmp(type, "presets:presets")) {
        if (fw->npreset == NFWP) return;
        fw_preset_t *p = &fw->preset[fw->npreset++];
        FW_COPY(p->key, fw_get(f, n, "key"));
        FW_COPY(p->label, fw_get(f, n, "label"));
        FW_COPY(p->note, fw_get(f, n, "note"));
        FW_COPY(p->error, fw_get(f, n, "error"));
    } else if (!strcmp(type, "options")) {
        fw->noption = 0; FW_COPY(fw->error, fw_get(f, n, "error"));
    } else if (!strcmp(type, "options:options")) {
        if (fw->noption == NFWO || strcmp(fw_get(f, n, "applies"), "1")) return;
        fw_option_t *o = &fw->option[fw->noption++];
        FW_COPY(o->name, fw_get(f, n, "name"));
        FW_COPY(o->value, fw_get(f, n, "value"));
        FW_COPY(o->kind, fw_get(f, n, "kind"));
        FW_COPY(o->group, fw_get(f, n, "group"));
        FW_COPY(o->help, fw_get(f, n, "help"));
    } else if (!strcmp(type, "stage")) fw_stage(st, f, n);
    else if (!strcmp(type, "log")) tiku_bench_log_line(st, fw_get(f, n, "text"), 0);
    else if (!strcmp(type, "note")) tiku_bench_log_line(st, fw_get(f, n, "text"), 1);
    else if (!strcmp(type, "status")) {
        const char *build = fw_get(f, n, "status.build");
        if (!*fw_get(f, n, "status.mcu")) {
            snprintf(fw->status, sizeof fw->status, "%s: no shell answers",
                     strcmp(fw_get(f, n, "phase"), "after") ? "Before" : "After");
            return;
        }
        snprintf(fw->status, sizeof fw->status,
                 "%s: runs %.64s, boot %.16s, store %.16s%s%.40s",
                 strcmp(fw_get(f, n, "phase"), "after") ? "Before" : "After",
                 *build ? build : "an image older than /sys/device/build",
                 fw_get(f, n, "status.boot_count"), fw_get(f, n, "status.store"),
                 *fw_get(f, n, "status.installed.preset") ? ", installed as " : "",
                 fw_get(f, n, "status.installed.preset"));
    } else if (!strcmp(type, "image")) FW_COPY(fw->image, fw_get(f, n, "build"));
    else if (!strcmp(type, "preview")) {
        FW_COPY(fw->preview, fw_get(f, n, "text"));
        FW_COPY(fw->pnote, fw_get(f, n, "note"));
    } else if (!strcmp(type, "done")) fw->ok = !strcmp(fw_get(f, n, "ok"), "1");
    st->dirty = 1;
}

/** A private log for one handoff, as a test run keeps one. */
static int fw_log_open(bench_t *st)
{
    char pattern[] = "/tmp/tikubench-desktop-XXXXXX";
    int fd = mkstemp(pattern);
    if (st->log) { fclose(st->log); st->log = NULL; }
    if (fd >= 0) { st->log = fdopen(fd, "w"); if (!st->log) close(fd); }
    snprintf(st->log_path, sizeof st->log_path, "%s",
             st->log ? pattern : "unavailable");
    return st->log != NULL;
}

void tiku_bench_fw_start(bench_t *st, int install)
{
    command_t c;
    const fw_preset_t *p = fw_selected(st);
    char line[512];
    if (!tiku_bench_editable(st) || st->child) return;
    tiku_bench_target_capture(st);
    if (!*st->port || !*st->fw.board || !p) {
        snprintf(st->note, sizeof st->note,
                 "Choose a connected board and a preset first.");
        st->dirty = 1; return;
    }
    if (*p->error || *st->fw.error) {
        snprintf(st->note, sizeof st->note, "The Makefile refuses this: %.440s",
                 *p->error ? p->error : st->fw.error);
        st->dirty = 1; return;
    }
    if (!fw_command(st, "preview", &c)) {
        snprintf(st->note, sizeof st->note,
                 "Extra takes KEY=VALUE words, at most 24 changes in all.");
        tiku_dialog_focus(&st->dialog, F_FWVARS); st->dialog.keyboard = 1;
        st->dirty = 1; return;
    }
    if (!fw_log_open(st)) {
        snprintf(st->note, sizeof st->note, "Cannot create a private run log");
        st->dirty = 1; return;
    }
    fw_stages_clear(st);
    st->fw.install = install; st->fw.step = FWS_PREVIEW; st->fw.handoff = 1;
    st->busy = 1; st->cancelled = 0;
    st->released = st->reclaiming = 0;
    st->waiting = 1; st->deadline = st->now + 10000000;
    snprintf(line, sizeof line, "Firmware: %s %.40s on %.64s (%.32s)",
             install ? "install" : "preview", p->key, st->target_name,
             st->fw.board);
    tiku_bench_log_line(st, line, 1);
    snprintf(st->note, sizeof st->note, "%s", line);
    if (!st->services->board_release || !st->services->board_reclaim ||
        !st->services->board_release(st->services->ctx, st->target_name)) {
        st->waiting = st->busy = st->fw.handoff = st->fw.install = 0;
        st->fw.step = FWS_IDLE;
        snprintf(st->note, sizeof st->note,
                 "The desktop would not let the board go; nothing was run.");
    }
    st->dirty = 1;
}

void tiku_bench_fw_spawn(bench_t *st)
{
    command_t c;
    int flash = st->fw.step == FWS_FLASH;
    if (!fw_command(st, flash ? "flash" : "preview", &c) ||
        !tiku_bench_process_spawn(st, c.argv)) {
        tiku_bench_log_line(st, "The firmware runner could not start", 1);
        st->fw.step = FWS_IDLE; st->fw.install = 0;
        tiku_bench_reclaim(st); return;
    }
    for (int i = 0; i < c.argc; i++) tiku_bench_log_line(st, c.argv[i], 0);
    snprintf(st->note, sizeof st->note, "%s %s...",
             flash ? "Installing on" : "Reading and building for",
             st->target_name);
    st->dirty = 1;
}

/** The install question, the preview in it, Cancel first. */
static void fw_ask(bench_t *st)
{
    const fw_preset_t *p = fw_selected(st);
    char question[512];
    snprintf(question, sizeof question,
             "Install \"%.60s\" on %.48s? The image is %.40s. %.200s%s%.120s "
             "The board stays with the tool until the flash is checked.",
             p ? p->label : "?", st->target_name,
             *st->fw.image ? st->fw.image : "built", st->fw.preview,
             *st->fw.pnote ? " " : "", st->fw.pnote);
    tiku_alert_open(&st->confirm, TIKU_ALERT_WARN, FW_CONFIRM, question,
                    "Cancel|Install");
    st->confirm.dflt = 0; st->confirm_button = -1; st->fw.asking = 1;
}

void tiku_bench_fw_exit(bench_t *st)
{
    fw_t *fw = &st->fw;
    int step = fw->step;
    int clean = WIFEXITED(st->exit_status) && !WEXITSTATUS(st->exit_status);
    fw->step = FWS_IDLE;
    if (step == FWS_QUERY) {
        if (fw->query == QUERY_PRESETS) {
            tiku_choice_init(&fw->choice);
            for (int i = 0; i < fw->npreset; i++)
                tiku_choice_add(&fw->choice, fw->preset[i].label,
                                !*fw->preset[i].error);
            /* The one chosen before, else the first the Makefile takes. */
            for (int pass = 0; pass < 2 && fw->choice.selected < 0; pass++)
                for (int i = 0; i < fw->npreset && fw->choice.selected < 0; i++)
                    if (!*fw->preset[i].error &&
                        (pass || !strcmp(fw->preset[i].key, fw->keep)))
                        fw->choice.selected = i;
            if (fw->choice.selected < 0 && fw->npreset)
                fw->choice.selected = 0;
            fw->want_options = fw->npreset > 0 && *fw->board;
            if (!fw->npreset && !*fw->error)
                snprintf(fw->error, sizeof fw->error, "%s",
                         clean ? "tikubench builds nothing for this board"
                               : "tikubench could not name this board "
                                 "(see Output)");
        } else {
            fw_rows(st);
            if (!clean && !*fw->error)
                snprintf(fw->error, sizeof fw->error,
                         "Could not read the options (see Output)");
        }
        st->dirty = 1; return;
    }
    if (step == FWS_PREVIEW && clean && fw->ok && fw->install &&
        !st->cancelled) {
        fw_ask(st); st->dirty = 1; return;
    }
    if (step == FWS_FLASH && clean && fw->ok)
        snprintf(st->note, sizeof st->note, "Installed %.64s on %.64s. Log: %.300s",
                 fw->image, st->target_name, st->log_path);
    else if (step == FWS_PREVIEW && clean && fw->ok)
        snprintf(st->note, sizeof st->note, "Preview done; nothing was flashed. "
                 "Log: %.300s", st->log_path);
    else snprintf(st->note, sizeof st->note, "%s. Log: %.400s",
                  st->cancelled ? "Stopped" : step == FWS_FLASH
                  ? "The install failed" : "The preview failed",
                  st->log_path);
    if (st->log) { fclose(st->log); st->log = NULL; }
    fw->install = 0;
    tiku_bench_reclaim(st);
}

void tiku_bench_fw_confirmed(bench_t *st, int install)
{
    if (st->confirm.open && st->confirm.tag == FW_CONFIRM)
        tiku_alert_reset(&st->confirm);
    st->fw.asking = 0;
    if (!install) {
        snprintf(st->note, sizeof st->note,
                 "Nothing was installed; the board is handed back.");
        if (st->log) { fclose(st->log); st->log = NULL; }
        st->fw.install = 0; tiku_bench_reclaim(st); return;
    }
    for (int i = FW_CHECK; i < FW_NSTAGE; i++)
        st->fw.stage[i][0] = st->fw.stage_text[i][0] = '\0';
    st->fw.ok = 0; st->fw.step = FWS_FLASH;
    tiku_bench_fw_spawn(st);
}

void tiku_bench_fw_toggle(bench_t *st, int row)
{
    fw_t *fw = &st->fw;
    if (row < 0 || row >= fw->nrow || fw->row[row] < 0 ||
        !tiku_bench_editable(st)) return;
    fw_option_t *o = &fw->option[fw->row[row]];
    if (strcmp(o->kind, "bool")) {
        snprintf(st->note, sizeof st->note,
                 "%s takes a value: give %s=... under Extra.", o->name, o->name);
        st->dirty = 1; return;
    }
    int value = strcmp(o->value, "1") != 0, i = 0;
    while (i < fw->nset && strcmp(fw->set[i].name, o->name)) i++;
    if (i == fw->nset) {
        if (fw->nset == 24) {
            snprintf(st->note, sizeof st->note, "At most 24 changes at once.");
            st->dirty = 1; return;
        }
        FW_COPY(fw->set[fw->nset++].name, o->name);
    }
    fw->set[i].value = value;
    FW_COPY(o->value, value ? "1" : "0");    /* shown now, checked by make */
    fw->want_options = 1; fw_stages_clear(st);
    st->dirty = 1;
}

void tiku_bench_fw_preset_changed(bench_t *st)
{
    fw_stages_clear(st);
    st->fw.error[0] = '\0'; st->fw.want_options = 1; st->dirty = 1;
}

int tiku_bench_fw_ready(const bench_t *st)
{
    return tiku_bench_editable(st) && !st->child && fw_selected(st) &&
           *st->fw.board && *st->port;
}

tiku_rect_t tiku_bench_fw_preset_rect(const bench_t *st)
{ (void)st; return (tiku_rect_t){RX + 60, 118, 300, 24}; }

/** The four steps as pills, the latest one's words beside them. */
static void fw_pills(bench_t *st, int y)
{
    static const char *const label[FW_NSTAGE] = {"Check", "Build", "Flash",
                                                  "Verify"};
    char text[48];
    const char *said = "";
    int x = RX;
    for (int i = 0; i < FW_NSTAGE; i++) {
        const char *s = st->fw.stage[i];
        int tone = !strcmp(s, "ok") ? TIKU_TONE_PASS : !strcmp(s, "fail")
                   ? TIKU_TONE_FAIL : !strcmp(s, "start") ? TIKU_TONE_WARN
                   : TIKU_TONE_IDLE;
        snprintf(text, sizeof text, "%s%s", label[i],
                 !strcmp(s, "start") ? " ..." : !strcmp(s, "fail") ? " failed"
                 : !strcmp(s, "skip") ? " skipped" : "");
        int w = tiku_ui_pill_w(text);
        tiku_ui_pill(st->surface, (tiku_rect_t){x, y, w, 20}, text, tone);
        x += w + 8;
        if (*s) said = st->fw.stage_text[i];
    }
    tiku_text_line_draw(st->surface, (tiku_rect_t){x + 4, y, RX + RW - x - 4, 20},
                        TIKU_MEASURE_PLAIN, said, TIKU_C_TEXT);
}

/** The option list: headings, a checkbox per switch, values as words. */
static void fw_options_draw(bench_t *st, tiku_rect_t body)
{
    fw_t *fw = &st->fw;
    tiku_list_t *l = &st->list[TAB_FIRMWARE];
    int visible = tiku_list_visible_rows(l, body);
    for (int i = l->top; i < l->count && i < l->top + visible; i++) {
        tiku_rect_t row = tiku_list_row_rect(l, body, i);
        char text[256];
        if (fw->row[i] < 0) {
            tiku_fill(st->surface, row, TIKU_C_PANEL);
            tiku_hline(st->surface, row.x, row.y + row.h - 1, row.w,
                       tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
            tiku_text_line_draw(st->surface,
                (tiku_rect_t){row.x + 8, row.y, row.w - 16, row.h},
                TIKU_MEASURE_BOLD, fw->group[-1 - fw->row[i]], TIKU_C_TEXT);
            continue;
        }
        const fw_option_t *o = &fw->option[fw->row[i]];
        int focus = tiku_list_selected(l, i), is_bool = !strcmp(o->kind, "bool");
        tiku_rgb_t ink = focus ? TIKU_C_SELTEXT : TIKU_C_TEXT;
        tiku_fill(st->surface, row, focus ? TIKU_C_SELECT : TIKU_C_DOC);
        if (is_bool)
            tiku_ui_checkbox(st->surface,
                (tiku_rect_t){row.x + 8, row.y, 22, row.h}, "",
                (!strcmp(o->value, "1") ? TIKU_S_ON : 0) |
                (!tiku_bench_editable(st) ? TIKU_S_DISABLED : 0));
        if (is_bool) snprintf(text, sizeof text, "%s", o->name);
        else snprintf(text, sizeof text, "%s = %s", o->name,
                      *o->value ? o->value : "(the board's default)");
        tiku_text_line_draw(st->surface,
            (tiku_rect_t){row.x + 36, row.y, 290, row.h},
            TIKU_MEASURE_PLAIN, text, ink);
        tiku_text_line_draw(st->surface,
            (tiku_rect_t){row.x + 332, row.y, row.w - 336, row.h},
            TIKU_MEASURE_PLAIN, o->help,
            focus ? ink : tiku_dim(TIKU_C_TEXT, TIKU_C_DOC));
    }
}

void tiku_bench_fw_draw(bench_t *st, tiku_rect_t body)
{
    fw_t *fw = &st->fw;
    const fw_preset_t *p = fw_selected(st);
    char build[96], board[64], line[512];
    int have = stat_row(st, "build", build, sizeof build);
    (void)stat_row(st, "board", board, sizeof board);
    if (!*st->target_name)
        snprintf(line, sizeof line, "No board. Connect one through Devices.");
    else if (have < 0)
        snprintf(line, sizeof line, "%s runs: not known yet", st->target_name);
    else if (!have)
        snprintf(line, sizeof line, "%s runs an image older than "
                 "/sys/device/build", st->target_name);
    else snprintf(line, sizeof line, "%s runs %s%s%s", st->target_name, build,
                  *board ? ", built for " : "", board);
    tiku_bench_text(st, (tiku_rect_t){RX, 90, RW, 22}, line, 1);
    tiku_bench_text(st, (tiku_rect_t){RX, 118, 56, 24}, "Preset", 0);
    {
        const char *say = fw->step == FWS_QUERY ? "Asking tikubench..." :
                          p && *p->error ? p->error : *fw->error ? fw->error :
                          p ? p->note : "";
        int bad = (p && *p->error) || *fw->error;
        tiku_text_line_draw(st->surface,
            (tiku_rect_t){RX + 370, 118, RW - 370, 24}, TIKU_MEASURE_PLAIN,
            say, bad ? TIKU_C_DANGER : *say ? TIKU_C_WARN : TIKU_C_TEXT);
    }
    fw_options_draw(st, body);
    if (!fw->nrow)
        tiku_bench_text(st, (tiku_rect_t){RX + 10, body.y + 10, RW - 20, 24},
            fw->step == FWS_QUERY ? "Reading the Makefile's options..." :
            "The options appear here once a preset is chosen", 0);
    tiku_bench_text(st, (tiku_rect_t){RX, st->h - 198, 56, 24}, "Extra", 0);
    fw_pills(st, st->h - 166);
    snprintf(line, sizeof line, "%s%s%s%s", fw->status,
             *fw->status && *fw->preview ? "  |  " : "",
             *fw->preview ? "Durable state: " : "", fw->preview);
    tiku_text_block_draw(st->surface, (tiku_rect_t){RX, st->h - 140, RW, 50},
                         TIKU_MEASURE_PLAIN, *line ? line :
                         "Preview reads the board, builds the image and says "
                         "what the update keeps; Install asks before flashing.",
                         TIKU_C_TEXT, 2);
}
