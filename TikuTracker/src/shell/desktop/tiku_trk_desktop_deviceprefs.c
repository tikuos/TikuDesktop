/*
 * TikuDesktop -- connected-device preferences.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * A curated VFS settings surface. Reads and writes share the mount's worker
 * key, so disconnect cannot close a backend underneath a pending request.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_desktop_private.h"
#include "tiku_field.h"
#include "tiku_work.h"
#include <time.h>

enum { DP_NAME, DP_CPU, DP_TIME, DP_CRYPTO, DP_RADIO, DP_SLEEP, DP_COUNT };
static const struct {
    const char *label, *path, *help;
    int editable;
} settings[DP_COUNT] = {
    { "Device name", "/sys/device/name",
      "Saved on the device. Reconnect to update its menu name.", 1 },
    { "CPU clock", "/sys/cpu/freq",
      "Set by firmware; this device cannot change it through VFS.", 0 },
    { "Date and time", "/sys/time",
      "Synchronize the device clock with this computer.", 1 },
    { "Cryptography", "/sys/crypto/mode",
      "Automatic uses hardware when available. Resets at reboot.", 1 },
    { "Radio transmit power", "/sys/radio/txpower",
      "Enter dBm. The device validates supported levels. Resets at reboot.",
            1 },
    { "Idle sleep mode", "/sys/power/mode",
      "Reported by the device; changing it is not supported through VFS.", 0 }
};

typedef struct {
    char value[96];
    int present, writable;
} dp_value_t;

typedef struct {
    char target[96], mode[16];
    unsigned long rates[8];
    int count, writable;
} dp_clock_t;

struct desktop_deviceprefs {
    desktop_t *app;
    tiku_window_t *window;
    char names[TIKU_MOUNT_MAX][64];
    int count, selected, setting, busy, refs;
    dp_value_t values[DP_COUNT];
    dp_clock_t clock;
    int clock_menu;
    tiku_field_t field;
    char status[192];
};

typedef struct {
    struct desktop_deviceprefs *owner;
    tiku_backend_t *backend;
    dp_value_t values[DP_COUNT];
    dp_clock_t clock;
    char input[96], status[192];
    int write_at, completed;
} dp_job_t;

/** @brief Font-scaled row shared by paint and hit testing. */
static int dp_row(void) { return tiku_font_plain()->height + 18; }

/** @brief Whether the selected CPU rate still needs to be saved. */
static int dp_clock_dirty(const struct desktop_deviceprefs *p)
{
    return p->clock.writable && strcmp(p->field.text, p->clock.target) != 0;
}

/** @brief Distinguish a pending reboot from small clock-measurement error. */
static int dp_clock_pending(const struct desktop_deviceprefs *p)
{
    unsigned long target = strtoul(p->clock.target, NULL, 10);
    unsigned long actual = strtoul(p->values[DP_CPU].value, NULL, 10);
    unsigned long delta = target > actual ? target - actual : actual - target;
    return !strcmp(p->clock.mode, "reboot") && target &&
           p->values[DP_CPU].present && delta > target / 50UL;
}

/** @brief Explain availability and restart requirements beside the control. */
static const char *dp_clock_help(const struct desktop_deviceprefs *p)
{
    if (p->busy) return "Reading clock settings from the device…";
    if (!p->values[DP_CPU].present)
        return "Connect a device, then refresh to read its clock settings.";
    if (!p->clock.mode[0])
        return "Clock control needs newer device firmware. "
               "Flash it, reconnect, then refresh.";
    if (!strcmp(p->clock.mode, "fixed"))
        return "This firmware exposes a fixed clock; no changes are available.";
    if (!p->clock.writable)
        return "Clock settings are unavailable or read-only "
               "on this connection.";
    if (dp_clock_pending(p))
        return "Restart the device to apply the saved rate. "
               "You can choose and save a different rate below.";
    return !strcmp(p->clock.mode, "reboot")
        ? "Choose a rate, then Save for restart. Restart the device manually "
          "when ready; saving never restarts it."
        : "Choose a supported clock rate, then Apply.";
}

/** @brief Start editing the selected setting's last confirmed value. */
static void dp_edit(struct desktop_deviceprefs *p)
{
    p->clock_menu = 0;
    tiku_field_start(&p->field, p->setting == DP_CPU ? p->clock.target :
                     p->values[p->setting].value);
    tiku_field_deny(&p->field, "\r\n\t/");
}

/** @brief Read a complete scalar, refusing truncated or failed answers. */
static int dp_read(tiku_backend_t *b, const char *path, char *out, size_t max)
{
    int n = b->ops->read(b, path, out, max - 1);
    if (n < 0 || (size_t)n >= max - 1) {
        out[0] = '\0';
        return 0;
    }
    out[n] = '\0';
    out[strcspn(out, "\r\n")] = '\0';
    return out[0] != '\0';
}

/** @brief Discover clock controls; older firmware remains read-only. */
static void dp_clock_read(tiku_backend_t *b, dp_clock_t *clock)
{
    char available[192], *next;
    tiku_model_t m;
    memset(clock, 0, sizeof *clock);
    if (!dp_read(b, "/sys/cpu/freq_target", clock->target,
                 sizeof clock->target) ||
        !dp_read(b, "/sys/cpu/freq_change_mode", clock->mode,
                 sizeof clock->mode) ||
        !dp_read(b, "/sys/cpu/freq_available", available, sizeof available))
        return;
    next = available;
    while (*next && clock->count < 8) {
        char *end;
        unsigned long hz;
        while (*next == ' ') next++;
        if (*next < '0' || *next > '9') return;
        hz = strtoul(next, &end, 10);
        if (!hz || hz > 1000000000UL || (*end && *end != ' ')) return;
        clock->rates[clock->count++] = hz;
        next = end;
    }
    if (*next) return;
    clock->writable = clock->count > 0 &&
        (!strcmp(clock->mode, "reboot") || !strcmp(clock->mode, "live")) &&
        b->ops->stat(b, "/sys/cpu/freq_target", &m) == 0 &&
        tiku_model_is_writable(&m);
}

/** @brief On a worker: validate permissions, write once, then read back. */
static void dp_run(void *ctx)
{
    dp_job_t *j = ctx;
    int i, available = 0;
    if (j->write_at >= 0 && !tiku_work_cancelled(j)) {
        tiku_model_t m;
        const char *path = j->write_at == DP_CPU ? "/sys/cpu/freq_target" :
                           settings[j->write_at].path;
        char err[128] = "";
        if (j->write_at == DP_CPU) dp_clock_read(j->backend, &j->clock);
        if ((j->write_at == DP_CPU && !j->clock.writable) ||
            j->backend->ops->stat(j->backend, path, &m) != 0 ||
            !tiku_model_is_writable(&m)) {
            snprintf(j->status, sizeof j->status,
            "Setting is no longer writable.");
        } else {
            if (j->write_at == DP_TIME)
                snprintf(j->input, sizeof j->input, "%lu",
            (unsigned long)time(NULL));
            if (j->backend->ops->write(j->backend, path, j->input,
                       strlen(j->input), err, sizeof err) != 0) {
                snprintf(j->status, sizeof j->status, "Could not apply: %.150s",
                     err[0] ? err : "device unavailable or value rejected");
            } else {
                snprintf(j->status, sizeof j->status,
            "Applied; checking device…");
            }
        }
    }
    for (i = 0; i < DP_COUNT && !tiku_work_cancelled(j); i++) {
        tiku_model_t m;
        if (j->backend->ops->stat(j->backend, settings[i].path, &m) == 0 &&
            dp_read(j->backend, settings[i].path, j->values[i].value,
                    sizeof j->values[i].value)) {
            j->values[i].present = 1;
            j->values[i].writable = settings[i].editable &&
                                    tiku_model_is_writable(&m);
            available++;
        }
    }
    if (!tiku_work_cancelled(j)) dp_clock_read(j->backend, &j->clock);
    j->values[DP_CPU].writable = j->values[DP_CPU].present &&
                                  j->clock.writable;
    if (j->write_at >= 0 && strncmp(j->status, "Applied;", 8) == 0) {
        dp_value_t *v = &j->values[j->write_at];
        const char *confirmed = j->write_at == DP_CPU ? j->clock.target :
                                v->value;
        if (!v->present || !confirmed[0] ||
            (j->write_at == DP_CPU && !j->clock.writable)) {
            snprintf(j->status, sizeof j->status,
                     "Write sent; readback failed. Refresh to check.");
        } else if ((j->write_at != DP_TIME && strcmp(confirmed, j->input)) ||
                   (j->write_at == DP_TIME &&
                    (strtoul(v->value, NULL, 10) < strtoul(j->input, NULL,
            10) ||
                     strtoul(v->value, NULL, 10) - strtoul(j->input, NULL,
            10) > 60UL))) {
            snprintf(j->status, sizeof j->status,
                     "Device returned a different value. "
                     "Showing its response.");
        } else {
            snprintf(j->status, sizeof j->status, "%s",
                j->write_at == DP_CPU && !strcmp(j->clock.mode, "reboot")
                ? "Saved for next restart. Current clock is unchanged."
                : "Applied and confirmed by device.");
        }
    } else if (j->write_at < 0) {
        snprintf(j->status, sizeof j->status, "%s", available
                 ? "Select a setting to view or change it."
                 : "Device unavailable or busy. Refresh to try again.");
    }
}

/** @brief On the loop: only confirmed values become the visible state. */
static void dp_done(void *ctx)
{
    dp_job_t *j = ctx;
    struct desktop_deviceprefs *p = j->owner;
    j->completed = 1;
    if (p->app == NULL) return;
    memcpy(p->values, j->values, sizeof p->values);
    p->clock = j->clock;
    snprintf(p->status, sizeof p->status, "%s", j->status);
    dp_edit(p);
    p->app->dirty = 1;
}

/** @brief Retire a job, including cancellation on device disconnect. */
static void dp_free(void *ctx)
{
    dp_job_t *j = ctx;
    struct desktop_deviceprefs *p = j->owner;
    p->busy = 0;
    if (p->app != NULL && !j->completed) {
        memset(p->values, 0, sizeof p->values);
        memset(&p->clock, 0, sizeof p->clock);
        p->clock_menu = 0;
        snprintf(p->status, sizeof p->status,
            "Device disconnected. Refresh the list.");
        p->app->dirty = 1;
    }
    if (--p->refs == 0) free(p);
    free(j);
}

/** @brief Queue one read or explicit Apply against the selected mount. */
static void dp_request(struct desktop_deviceprefs *p, int write_at)
{
    char path[TIKU_PATH_MAX], local[TIKU_PATH_MAX];
    tiku_backend_t *b;
    tiku_work_job_t work = {0};
    dp_job_t *j;
    if (p->busy || p->selected < 0 || p->selected >= p->count) return;
    p->clock_menu = 0;
    snprintf(path, sizeof path, "/devices/%s/sys", p->names[p->selected]);
    b = tiku_backend_serving(p->app->backend, path, local, sizeof local);
    if (b == NULL || tiku_backend_tiku_identity(b) == NULL) {
        memset(p->values, 0, sizeof p->values);
        memset(&p->clock, 0, sizeof p->clock);
        snprintf(p->status, sizeof p->status,
            "Device disconnected. Refresh the list.");
        return;
    }
    j = calloc(1, sizeof *j);
    if (j == NULL) return;
    j->owner = p;
    j->backend = b;
    j->write_at = write_at;
    snprintf(j->input, sizeof j->input, "%.95s", p->field.text);
    if (write_at == DP_TIME) {
        snprintf(j->input, sizeof j->input, "%lu", (unsigned long)time(NULL));
    }
    work.run = dp_run;
    work.done = dp_done;
    work.free = dp_free;
    work.ctx = j;
    snprintf(work.key, sizeof work.key, "board:/devices/%.30s",
             p->names[p->selected]);
    if (tiku_work_post(&work) != 0) {
        free(j);
        snprintf(p->status, sizeof p->status, "Desktop is busy. Try again.");
        return;
    }
    p->refs++;
    p->busy = 1;
    snprintf(p->status, sizeof p->status, "%s",
             write_at >= 0 ? "Applying to device…"
                           : "Reading device settings…");
}

/** @brief Validate editable input before queuing a device mutation. */
static void dp_apply(struct desktop_deviceprefs *p)
{
    char *end;
    long v;
    if (p->busy || !p->values[p->setting].writable) return;
    if (p->setting == DP_CPU) {
        unsigned long hz = strtoul(p->field.text, &end, 10);
        int i;
        for (i = 0; i < p->clock.count; i++)
            if (hz == p->clock.rates[i] && !*end) break;
        if (i == p->clock.count || !dp_clock_dirty(p)) return;
    }
    if (p->field.len >= 96) {
        snprintf(p->status, sizeof p->status, "Value is too long.");
        return;
    }
    if (p->setting == DP_CRYPTO && strcmp(p->field.text, "auto") &&
        strcmp(p->field.text, "sw")) {
        snprintf(p->status, sizeof p->status,
            "Choose Automatic or Software only.");
        return;
    }
    if (p->setting == DP_NAME &&
        (p->field.len < 1 || p->field.len > 31)) {
        snprintf(p->status, sizeof p->status,
            "Use 1–31 bytes for the device name.");
        return;
    }
    if (p->setting == DP_RADIO) {
        v = strtol(p->field.text, &end, 10);
        if (end == p->field.text || *end || v < -128 || v > 127) {
            snprintf(p->status, sizeof p->status,
            "Enter a whole-number transmit power in dBm.");
            return;
        }
    }
    dp_request(p, p->setting);
}

/** @brief Refresh the cached mount list; selection never follows a rename. */
static void dp_refresh(struct desktop_deviceprefs *p)
{
    char names[512], selected[64] = "", *line, *save;
    if (p->busy) return;
    if (p->selected >= 0 && p->selected < p->count)
        snprintf(selected, sizeof selected, "%s", p->names[p->selected]);
    p->count = 0;
    p->selected = 0;
    names[0] = '\0';
    (void)desktop_board_names(p->app, names, sizeof names);
    for (line = strtok_r(names, "\n", &save); line && p->count < TIKU_MOUNT_MAX;
         line = strtok_r(NULL, "\n", &save)) {
        snprintf(p->names[p->count], sizeof p->names[0], "%s", line);
        if (strcmp(line, selected) == 0) p->selected = p->count;
        p->count++;
    }
    memset(p->values, 0, sizeof p->values);
    snprintf(p->status, sizeof p->status,
             "Connect a device from the desktop Devices menu.");
    dp_request(p, -1);
}

/** @brief Two lines of explanatory text within the details column. */
static void dp_explain(tiku_surface_t *s, int x, int y, int width,
                       const char *text, tiku_rgb_t ink)
{
    const tiku_font_t *f = tiku_font_plain();
    char line[192];
    size_t n;
    for (int row = 0; row < 2 && *text; row++) {
        snprintf(line, sizeof line, "%s", text);
        n = strlen(line);
        while (n && tiku_text_width(f, line) > width) line[--n] = '\0';
        if (!n) break;
        if (text[n] && row == 0) {
            char *space = strrchr(line, ' ');
            if (space != NULL) { n = (size_t)(space - line); *space = '\0'; }
        }
        tiku_text(s, f, x, y + f->ascent, line, ink);
        text += n;
        while (*text == ' ') text++;
        y += f->height + 2;
    }
}

/** @brief Device list, settings table, and editor for the selected option. */
static void dp_draw(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t c,
            void *ctx)
{
    struct desktop_deviceprefs *p = ctx;
    const tiku_font_t *f = tiku_font_plain();
    int row = dp_row(), x = c.x + 208, y = c.y + 18, i;
    tiku_rgb_t quiet = tiku_dim(TIKU_C_TEXT, tiku_dim(TIKU_C_TEXT,
            TIKU_C_PANEL));
    (void)w;
    tiku_fill(s, c, TIKU_C_PANEL);
    tiku_text(s, tiku_font_bold(), c.x + 16, y + f->ascent,
            "Connected devices", TIKU_C_TEXT);
    for (i = 0; i < p->count; i++) {
        tiku_rect_t r = { c.x + 8, y + row + i * (row + 10), 184, row + 8 };
        if (i == p->selected) tiku_fill(s, r, TIKU_C_SELECT);
        tiku_icon_paint(s, "microcontroller", r.x + 6, r.y + 4, 24);
        dp_explain(s, r.x + 38, r.y + 4, r.w - 44, p->names[i],
                   i == p->selected ? TIKU_C_SELTEXT : TIKU_C_TEXT);
    }
    if (!p->count) tiku_text(s, f, c.x + 16, y + row + f->ascent,
            "No devices connected", quiet);
    tiku_vline(s, c.x + 196, c.y + 12, c.h - 24, tiku_tint(TIKU_C_PANEL, 0.8f));
    tiku_text(s, tiku_font_bold(), x, y + f->ascent,
              p->count ? p->names[p->selected] : "Device settings",
            TIKU_C_TEXT);
    for (i = 0; i < DP_COUNT; i++) {
        char value[128];
        tiku_rect_t r = { x, y + row + i * row, c.w - 224, row };
        dp_value_t *v = &p->values[i];
        tiku_rgb_t ink = i == p->setting ? TIKU_C_SELTEXT : TIKU_C_TEXT;
        if (i == p->setting) tiku_fill(s, r, TIKU_C_SELECT);
        snprintf(value, sizeof value, "%s", v->present ? v->value :
                 (p->busy ? "Reading…" : "Unavailable"));
        if (i == DP_CPU && v->present) {
            double mhz = strtoul(v->value, NULL, 10) / 1000000.0;
            if (dp_clock_pending(p))
                snprintf(value, sizeof value, "%g MHz now / %g MHz saved",
                    mhz, strtoul(p->clock.target, NULL, 10) / 1000000.0);
            else snprintf(value, sizeof value, "%g MHz", mhz);
        }
        if (i == DP_RADIO && v->present) snprintf(value, sizeof value,
            "%s dBm", v->value);
        if (i == DP_TIME && v->present) {
            time_t seconds = (time_t)strtoull(v->value, NULL, 10);
            struct tm utc;
            if (gmtime_r(&seconds, &utc))
                strftime(value, sizeof value, "%Y-%m-%d %H:%M UTC", &utc);
        }
        if (i == DP_CRYPTO && v->present)
            snprintf(value, sizeof value, "%s", !strcmp(v->value,
            "auto") ? "Automatic" : "Software only");
        tiku_text(s, f, x + 8, r.y + 8 + f->ascent, settings[i].label, ink);
        tiku_text(s, f, x + 210, r.y + 8 + f->ascent, value, ink);
    }
    y += (DP_COUNT + 2) * row;
    dp_explain(s, x, y, c.w - 224,
        p->setting == DP_CPU ? dp_clock_help(p)
        : settings[p->setting].help, quiet);
    y += 2 * row;
    if (p->values[p->setting].writable) {
        int button_w = p->setting == DP_CPU ? 168 : 104;
        tiku_rect_t edit = {x, y, c.w - 240 - button_w, tiku_ui_button_h()};
        unsigned flags = p->busy ? TIKU_S_DISABLED : 0;
        if (p->setting == DP_CPU) {
            char label[80];
            snprintf(label, sizeof label, "%g MHz%s",
                strtoul(p->field.text, NULL, 10) / 1000000.0,
                dp_clock_dirty(p) ? " (not saved)" : " (saved)");
            tiku_ui_menufield(s, edit, label, flags);
        } else if (p->setting == DP_TIME) {
            tiku_text(s, f, x, y + f->ascent,
            "Use this computer's date and time", TIKU_C_TEXT);
        } else if (p->setting == DP_CRYPTO) {
            tiku_ui_button(s, edit, !strcmp(p->field.text,
            "auto") ? "Automatic" : "Software only", flags);
        } else {
            tiku_ui_textfield_sel(s, edit, p->field.text, p->field.caret,
                                  p->field.sel_a, p->field.sel_b, flags);
        }
        if (p->setting == DP_CPU && !dp_clock_dirty(p))
            flags |= TIKU_S_DISABLED;
        tiku_ui_button(s, (tiku_rect_t){c.x + c.w - button_w - 16, y, button_w,
            tiku_ui_button_h()},
                       p->setting == DP_TIME ? "Sync now" :
                       p->setting == DP_CPU &&
                       !strcmp(p->clock.mode, "reboot")
                           ? "Save for restart" : "Apply",
                       flags);
    }
    dp_explain(s, x, y + row, c.w - 224, p->status, quiet);
    tiku_ui_button(s, (tiku_rect_t){c.x + 16, c.y + c.h - 46, 164,
            tiku_ui_button_h()},
                   "Refresh devices", p->busy ? TIKU_S_DISABLED : 0);
    if (p->clock_menu) {
        for (i = 0; i < p->clock.count; i++) {
            char label[40];
            tiku_rect_t r = {x, y - (p->clock.count - i) * row,
                             c.w - 408, row};
            snprintf(label, sizeof label, "%lu MHz",
                     p->clock.rates[i] / 1000000UL);
            tiku_ui_button(s, r, label, 0);
        }
    }
}

/** @brief Select a device or setting; edits commit only through Apply. */
static int dp_event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    struct desktop_deviceprefs *p = ctx;
    tiku_rect_t c = tiku_window_content(w);
    int x = ev->x - c.x, y = ev->y - c.y, row = dp_row();
    int edit_y = 18 + (DP_COUNT + 4) * row;
    int button_x = c.w - (p->setting == DP_CPU ? 184 : 120);
    if (p->clock_menu && ev->type == TIKU_EVENT_KEY_DOWN &&
        ev->key == TIKU_KEY_ESCAPE) {
        p->clock_menu = 0;
        p->app->dirty = 1;
        return 1;
    }
    if (ev->type == TIKU_EVENT_KEY_DOWN &&
        (ev->key == TIKU_KEY_ESCAPE ||
         ((ev->modifiers & TIKU_MOD_CMD) &&
          (ev->key == 'w' || ev->key == 'q')))) {
        desktop_deviceprefs_close(p->app);
        return 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN &&
        (ev->button != 1u || x < 0 || y < 0 || x >= c.w || y >= c.h))
        return 0;
    if (ev->type == TIKU_EVENT_POINTER_DOWN && !p->busy) {
        if (p->clock_menu) {
            int top = edit_y - p->clock.count * row;
            if (x >= 208 && x < button_x - 16 && y >= top && y < edit_y) {
                char value[32];
                snprintf(value, sizeof value, "%lu",
                         p->clock.rates[(y - top) / row]);
                tiku_field_start(&p->field, value);
            }
            p->clock_menu = 0;
        } else if (x >= 16 && x < 180 && y >= c.h - 46 &&
            y < c.h - 46 + tiku_ui_button_h()) {
            dp_refresh(p);
        } else if (x >= 8 && x < 192 && y >= 18 + row) {
            int i = (y - 18 - row) / (row + 10);
            if (i < p->count) {
                p->selected = i;
                memset(p->values, 0, sizeof p->values);
                dp_request(p, -1);
            }
        } else if (x >= 208 && x < c.w - 16 && y >= 18 + row &&
            y < 18 + (DP_COUNT + 1) * row) {
            p->setting = (y - 18 - row) / row;
            dp_edit(p);
        } else if (y >= edit_y && y < edit_y + tiku_ui_button_h() && x >= 208) {
            if (x >= button_x && x < c.w - 16) dp_apply(p);
            else if (x < button_x - 16 && p->setting == DP_CPU &&
                     p->values[DP_CPU].writable) p->clock_menu = 1;
            else if (p->setting == DP_CRYPTO && p->values[DP_CRYPTO].writable)
                tiku_field_start(&p->field, !strcmp(p->field.text,
            "auto") ? "sw" : "auto");
        }
    } else if (ev->type == TIKU_EVENT_KEY_DOWN && !p->busy) {
        tiku_field_key_t key = TIKU_FIELD_CHAR;
        if (ev->key == TIKU_KEY_TAB) {
            p->setting = (p->setting + 1) % DP_COUNT;
            dp_edit(p);
        } else if (p->setting == DP_CPU && p->clock.writable &&
                   (ev->key == TIKU_KEY_UP || ev->key == TIKU_KEY_DOWN)) {
            unsigned long hz = strtoul(p->field.text, NULL, 10);
            int i;
            char value[32];
            for (i = 0; i < p->clock.count; i++)
                if (p->clock.rates[i] == hz) break;
            i = (i + (ev->key == TIKU_KEY_UP ? p->clock.count - 1 : 1)) %
                p->clock.count;
            snprintf(value, sizeof value, "%lu", p->clock.rates[i]);
            tiku_field_start(&p->field, value);
        } else if (p->setting == DP_CPU && p->clock.writable &&
                   ev->key == ' ') {
            p->clock_menu = !p->clock_menu;
        } else if (p->clock_menu && ev->key == TIKU_KEY_RETURN) {
            /* Confirm the menu choice, not a persistent device write. */
            p->clock_menu = 0;
        } else if (ev->key == TIKU_KEY_RETURN) {
            p->clock_menu = 0;
            dp_apply(p);
        }
        else if (p->values[p->setting].writable &&
                 (p->setting == DP_NAME || p->setting == DP_RADIO)) {
            switch (ev->key) {
            case TIKU_KEY_BACKSPACE: key = TIKU_FIELD_BACKSPACE; break;
            case TIKU_KEY_DELETE: key = TIKU_FIELD_DELETE; break;
            case TIKU_KEY_LEFT: key = TIKU_FIELD_LEFT; break;
            case TIKU_KEY_RIGHT: key = TIKU_FIELD_RIGHT; break;
            case TIKU_KEY_HOME: key = TIKU_FIELD_HOME; break;
            case TIKU_KEY_END: key = TIKU_FIELD_END; break;
            default: break;
            }
            if ((ev->modifiers & TIKU_MOD_CMD) &&
                (ev->key == 'a' || ev->key == 'A')) key = TIKU_FIELD_SELECT_ALL;
            if (key != TIKU_FIELD_CHAR || ev->text[0])
                (void)tiku_field_key(&p->field, key,
            !!(ev->modifiers & TIKU_MOD_SHIFT), ev->text[0]);
        }
    }
    p->app->dirty = 1;
    return 1;
}

/** @brief Return the native window for desktop input routing. */
tiku_window_t *desktop_deviceprefs_handle(desktop_t *app)
{
    return app->deviceprefs != NULL ? app->deviceprefs->window : NULL;
}

/** @brief Close the window without abandoning pending device work. */
void desktop_deviceprefs_close(desktop_t *app)
{
    tiku_window_t *w = desktop_deviceprefs_handle(app);
    if (w == NULL) return;
    tiku_workspace_close(app->desk, w);
    app->deviceprefs->window = NULL;
    app->dirty = 1;
}

/** @brief Open Settings > Devices Preferences, or raise its existing window. */
void desktop_deviceprefs_open(desktop_t *app)
{
    struct desktop_deviceprefs *p = app->deviceprefs;
    tiku_rect_t r;
    if (p == NULL) {
        p = calloc(1, sizeof *p);
        if (p == NULL) return;
        p->app = app;
        p->refs = 1;
        p->setting = DP_CPU;
        app->deviceprefs = p;
    }
    if (p->window != NULL && p->window->open) {
        tiku_workspace_activate(app->desk, p->window);
        return;
    }
    r.w = 224 + tiku_text_width(tiku_font_plain(),
            "Enter dBm. The device validates supported levels. "
            "Resets at reboot.") + 24;
    if (r.w < 740) r.w = 740;
    r.h = 100 + (DP_COUNT + 6) * dp_row();
    r.x = (app->surf->w - r.w) / 2;
    r.y = (app->surf->h - r.h) / 3;
    p->window = tiku_workspace_open(app->desk, "Devices Preferences", r,
            dp_draw, p, NULL);
    if (p->window != NULL) {
        tiku_window_set_size_controls(p->window, 0, 0);
        tiku_window_set_handlers(p->window, dp_event, NULL, NULL);
        dp_refresh(p);
    }
    app->dirty = 1;
}

/** @brief Detach UI ownership; pending jobs keep their snapshots alive. */
void desktop_deviceprefs_destroy(desktop_t *app)
{
    struct desktop_deviceprefs *p = app->deviceprefs;
    if (p == NULL) return;
    if (p->window != NULL && p->window->open) tiku_workspace_close(app->desk,
            p->window);
    p->app = NULL;
    app->deviceprefs = NULL;
    if (--p->refs == 0) free(p);
}
