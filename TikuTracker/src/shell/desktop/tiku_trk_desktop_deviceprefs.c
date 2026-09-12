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
#include "tiku_field_event.h"
#include "tiku_dialog.h"
#include "tiku_choice_paint.h"
#include "tiku_properties_paint.h"
#include "tiku_preferences_layout.h"
#include "tiku_text_layout_paint.h"
#include "tiku_work.h"
#include <time.h>

enum { DP_NAME, DP_CPU, DP_TIME, DP_CRYPTO, DP_RADIO, DP_SLEEP, DP_COUNT };
enum { DP_DEVICE = 10, DP_SETTING = 30, DP_EDIT = 50,
       DP_APPLY, DP_REFRESH, DP_DONE };
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
    tiku_dialog_t dialog;
    tiku_choice_t choice;
    tiku_field_input_t input;
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
static int dp_row(void) { return tiku_font_plain()->height + 14; }

typedef struct {
    tiku_rect_t devices, table, group, edit, apply, refresh, done;
    int help_y, status_y;
} dp_layout_t;
static void dp_controls(struct desktop_deviceprefs *p, dp_layout_t l);

/** @brief One font-scaled layout for both drawing and pointer input. */
static dp_layout_t dp_layout(tiku_rect_t c, int setting)
{
    dp_layout_t l;
    int row = dp_row(), fh = tiku_font_plain()->height;
    int button = tiku_ui_button_h();
    int bw = tiku_ui_button_w(setting == DP_CPU ? "Save for restart" :
                             setting == DP_TIME ? "Sync now" : "Apply", 88);
    int left = 196 + (fh - 15) * 3;
    l.table = (tiku_rect_t){c.x + left + 18, c.y + 2 * fh + 34,
                          c.w - left - 36, DP_COUNT * row + 8};
    l.group = (tiku_rect_t){l.table.x, l.table.y + l.table.h + 18,
                          l.table.w, button + 3 * (fh + 2) + 60};
    tiku_rect_t inner = tiku_preferences_group_body(l.group);
    l.apply = (tiku_rect_t){inner.x + inner.w - bw, inner.y, bw, button};
    l.edit = (tiku_rect_t){inner.x, inner.y, inner.w - bw - 12, button};
    l.help_y = l.edit.y + button + 14;
    l.status_y = l.group.y + l.group.h + 14;
    tiku_preferences_action_t refresh = {"Refresh devices", left - 18};
    tiku_preferences_action_t done = {"Done", 88};
    (void)tiku_preferences_actions((tiku_rect_t){c.x + 18,
        l.status_y + 2 * (fh + 2) + 14, c.w - 36, button},
        &refresh, &done, 1, 12, &l.refresh, &l.done);
    l.devices = (tiku_rect_t){c.x + 18, c.y + fh + 28, left - 18,
                             l.group.y + l.group.h - c.y - fh - 28};
    return l;
}

/** @brief Device rows allow two short lines beside the board icon. */
static tiku_rect_t dp_device_row(dp_layout_t l, int i)
{
    int height = (l.devices.h - 8) / TIKU_MOUNT_MAX;
    return (tiku_rect_t){l.devices.x + 4, l.devices.y + 4 + i * height,
                         l.devices.w - 8, height};
}

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
    tiku_choice_init(&p->choice);
    tiku_field_input_init(&p->input);
    tiku_field_start(&p->field, p->setting == DP_CPU ? p->clock.target :
                     p->values[p->setting].value);
    tiku_field_deny(&p->field, "\r\n\t/");
    if (p->setting == DP_CPU) {
        for (int i = 0; i < p->clock.count; i++) {
            char label[32];
            snprintf(label, sizeof label, "%g MHz",
                     p->clock.rates[i] / 1000000.0);
            tiku_choice_add(&p->choice, label, 1);
        }
        for (int i = 0; i < p->clock.count; i++)
            if (p->clock.rates[i] == strtoul(p->field.text, NULL, 10))
                tiku_choice_select(&p->choice, i);
    } else if (p->setting == DP_CRYPTO) {
        tiku_choice_add(&p->choice, "Automatic", 1);
        tiku_choice_add(&p->choice, "Software only", 1);
        tiku_choice_select(&p->choice, strcmp(p->field.text, "auto") != 0);
    }
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
                 ? "Settings read from device."
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
        tiku_choice_close(&p->choice);
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
    tiku_choice_close(&p->choice);
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

/** @brief Wrapped explanatory text within a bounded details block. */
static void dp_explain(tiku_surface_t *s, int x, int y, int width,
                       const char *text, tiku_rgb_t ink, int lines)
{
    int fh = tiku_measure_height(TIKU_MEASURE_PLAIN);
    tiku_text_block_draw(s, (tiku_rect_t){x, y, width, lines * (fh + 2) - 2},
                         TIKU_MEASURE_PLAIN, text, ink, 2);
}

/** @brief A clipped single line cannot spill into a neighboring column. */
static void dp_line(tiku_surface_t *s, tiku_rect_t r, const char *text,
                    tiku_rgb_t ink)
{
    tiku_text_line_draw(s, r, TIKU_MEASURE_PLAIN, text, ink);
}

/** @brief The current value table is separate from the pending edit below. */
static void dp_table(struct desktop_deviceprefs *p, tiku_surface_t *s,
                      dp_layout_t l)
{
    tiku_properties_t table;
    char values[DP_COUNT][128];
    int focused = -1;
    tiku_properties_init(&table);
    table.row_h = dp_row();
    table.selected = p->setting;
    for (int i = 0; i < DP_COUNT; i++) {
        char *value = values[i];
        dp_value_t *v = &p->values[i];
        snprintf(value, sizeof values[i], "%s", v->present ? v->value :
                 (p->busy ? "Reading…" : "Unavailable"));
        if (i == DP_CPU && v->present) {
            double mhz = strtoul(v->value, NULL, 10) / 1000000.0;
            if (dp_clock_pending(p))
                snprintf(value, sizeof values[i], "%g MHz now / %g MHz saved",
                    mhz, strtoul(p->clock.target, NULL, 10) / 1000000.0);
            else snprintf(value, sizeof values[i], "%g MHz", mhz);
        }
        if (i == DP_RADIO && v->present) snprintf(value, sizeof values[i],
            "%s dBm", v->value);
        if (i == DP_TIME && v->present) {
            time_t seconds = (time_t)strtoull(v->value, NULL, 10);
            struct tm utc;
            if (gmtime_r(&seconds, &utc))
                strftime(value, sizeof values[i], "%Y-%m-%d %H:%M UTC", &utc);
        }
        if (i == DP_CRYPTO && v->present)
            snprintf(value, sizeof values[i], "%s", !strcmp(v->value,
            "auto") ? "Automatic" : "Software only");
        tiku_properties_add(&table, settings[i].label, value,
            !v->present && !p->busy ? TIKU_PROPERTY_UNAVAILABLE : 0);
        if (tiku_dialog_focused(&p->dialog, DP_SETTING + i)) focused = i;
    }
    tiku_properties_draw(&table, s, l.table, focused);
}

/** @brief Etched setting group, with explicit writable/read-only state. */
static void dp_editor(struct desktop_deviceprefs *p, tiku_surface_t *s,
                       dp_layout_t l)
{
    char title[96];
    unsigned flags = (p->busy ? TIKU_S_DISABLED : 0) |
        (tiku_dialog_focused(&p->dialog, DP_EDIT) ? TIKU_S_FOCUS : 0);
    tiku_rgb_t quiet = tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL);
    snprintf(title, sizeof title, "%s%s", settings[p->setting].label,
        p->values[p->setting].present && !p->values[p->setting].writable
        ? " (read-only)" : "");
    (void)tiku_ui_group(s, l.group, title);
    if (p->values[p->setting].writable) {
        if (p->setting == DP_CPU) {
            char label[80];
            snprintf(label, sizeof label, "%g MHz%s",
                strtoul(p->field.text, NULL, 10) / 1000000.0,
                dp_clock_dirty(p) ? " (not saved)" : " (saved)");
            tiku_choice_draw(&p->choice, s, l.edit, label, flags);
        } else if (p->setting == DP_TIME) {
            dp_line(s, l.edit, "Use this computer's time", TIKU_C_TEXT);
        } else if (p->setting == DP_CRYPTO) {
            tiku_choice_draw(&p->choice, s, l.edit, NULL, flags);
        } else {
            tiku_ui_textfield_scroll(s, l.edit, p->field.text, p->field.caret,
                p->field.sel_a, p->field.sel_b, flags, p->input.scroll);
        }
        if (p->setting == DP_CPU && !dp_clock_dirty(p))
            flags |= TIKU_S_DISABLED;
        flags &= ~TIKU_S_FOCUS;
        if (tiku_dialog_focused(&p->dialog, DP_APPLY)) flags |= TIKU_S_FOCUS;
        if (tiku_dialog_pressed(&p->dialog, DP_APPLY)) flags |= TIKU_S_PRESSED;
        tiku_ui_button(s, l.apply,
                       p->setting == DP_TIME ? "Sync now" :
                       p->setting == DP_CPU &&
                       !strcmp(p->clock.mode, "reboot")
                           ? "Save for restart" : "Apply",
                       flags);
    } else {
        dp_line(s, (tiku_rect_t){l.edit.x, l.edit.y,
                               l.group.w - 28, l.edit.h},
            p->busy ? "Reading device settings…" :
            p->values[p->setting].present ? "Reported by the device" :
            "Not available on this device", quiet);
    }
    dp_explain(s, l.edit.x, l.help_y, l.group.w - 28,
        p->setting == DP_CPU ? dp_clock_help(p) : settings[p->setting].help,
        quiet, 3);
}

/** @brief Classic device sidebar, recessed value list and grouped editor. */
static void dp_draw(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t c,
                    void *ctx)
{
    struct desktop_deviceprefs *p = ctx;
    const tiku_font_t *f = tiku_font_plain();
    dp_layout_t l = dp_layout(c, p->setting);
    tiku_rgb_t quiet = tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL);
    int i;
    (void)w;
    dp_controls(p, l);
    tiku_fill(s, c, TIKU_C_PANEL);
    tiku_text(s, tiku_font_bold(), l.devices.x, c.y + 18 + f->ascent,
              "Devices", TIKU_C_TEXT);
    tiku_ui_sunken(s, l.devices, TIKU_C_DOC);
    for (i = 0; i < p->count; i++) {
        tiku_rect_t r = dp_device_row(l, i);
        if (i == p->selected) tiku_fill(s, r, TIKU_C_SELECT);
        tiku_icon_paint(s, "microcontroller", r.x + 6,
                        r.y + (r.h - 24) / 2, 24);
        dp_explain(s, r.x + 38, r.y + 5, r.w - 44, p->names[i],
                   i == p->selected ? TIKU_C_SELTEXT : TIKU_C_TEXT, 2);
        if (tiku_dialog_focused(&p->dialog, DP_DEVICE + i))
            tiku_frame(s, tiku_inset(r, 1), TIKU_C_FOCUS);
    }
    if (!p->count) dp_explain(s, l.devices.x + 12, l.devices.y + 14,
        l.devices.w - 24, "No devices connected", quiet, 2);
    tiku_icon_paint(s, "microcontroller", l.table.x, c.y + 16, 32);
    dp_line(s, (tiku_rect_t){l.table.x + 44, c.y + 14, l.table.w - 44,
                            f->height + 4},
            p->count ? p->names[p->selected] : "Device settings", TIKU_C_TEXT);
    dp_line(s, (tiku_rect_t){l.table.x + 44, c.y + f->height + 20,
                            l.table.w - 44, f->height},
            "Select a setting to view or change.", quiet);
    if (p->count) {
        dp_table(p, s, l);
        dp_editor(p, s, l);
        dp_explain(s, l.table.x, l.status_y, l.table.w, p->status, quiet, 2);
    } else {
        tiku_rect_t empty = {l.table.x, l.table.y, l.table.w,
                            l.group.y + l.group.h - l.table.y};
        (void)tiku_ui_group(s, empty, "Device settings");
        tiku_icon_paint(s, "microcontroller", empty.x + 24,
                        empty.y + 34, 48);
        dp_line(s, (tiku_rect_t){empty.x + 88, empty.y + 34,
                                empty.w - 104, 28},
                "No connected devices", TIKU_C_TEXT);
        dp_explain(s, empty.x + 24, empty.y + 102, empty.w - 48,
            "Connect a board using the desktop Devices menu, "
            "then choose Refresh devices.", quiet, 3);
    }
    tiku_ui_button(s, l.refresh, "Refresh devices",
        (p->busy ? TIKU_S_DISABLED : 0) |
        (tiku_dialog_focused(&p->dialog, DP_REFRESH) ? TIKU_S_FOCUS : 0) |
        (tiku_dialog_pressed(&p->dialog, DP_REFRESH) ? TIKU_S_PRESSED : 0));
    tiku_ui_button(s, l.done, "Done",
        (tiku_dialog_focused(&p->dialog, DP_DONE) ? TIKU_S_FOCUS : 0) |
        (tiku_dialog_pressed(&p->dialog, DP_DONE) ? TIKU_S_PRESSED : 0));
    tiku_choice_draw_popup(&p->choice, s, l.edit, c);
}

/** @brief Keyboard focus order, recomputed from current capabilities. */
static void dp_controls(struct desktop_deviceprefs *p, dp_layout_t l)
{
    int writable = !p->busy && p->values[p->setting].writable;
    tiku_dialog_begin(&p->dialog, DP_APPLY, DP_DONE);
    for (int i = 0; i < p->count; i++)
        tiku_dialog_add(&p->dialog, DP_DEVICE + i, dp_device_row(l, i),
                        TIKU_DIALOG_BUTTON, !p->busy);
    tiku_properties_t table;
    tiku_properties_init(&table);
    table.row_h = dp_row();
    for (int i = 0; i < DP_COUNT; i++)
        tiku_properties_add(&table, settings[i].label, NULL, 0);
    (void)tiku_properties_controls(&table, l.table, &p->dialog, DP_SETTING,
                                    !p->busy && p->count);
    tiku_dialog_add(&p->dialog, DP_EDIT, l.edit, TIKU_DIALOG_INPUT,
                    writable && p->setting != DP_TIME);
    tiku_dialog_add(&p->dialog, DP_APPLY, l.apply, TIKU_DIALOG_BUTTON,
                    writable && (p->setting != DP_CPU || dp_clock_dirty(p)));
    tiku_dialog_add(&p->dialog, DP_REFRESH, l.refresh,
                    TIKU_DIALOG_BUTTON, !p->busy);
    tiku_dialog_add(&p->dialog, DP_DONE, l.done, TIKU_DIALOG_BUTTON, 1);
    tiku_dialog_end(&p->dialog);
    p->choice.enabled = writable;
}

/** @brief Dropdown selection updates the draft, never the device. */
static int dp_choice_event(struct desktop_deviceprefs *p,
                           const tiku_event_t *ev, dp_layout_t l, tiku_rect_t c)
{
    tiku_choice_result_t result;
    if (p->setting != DP_CPU && p->setting != DP_CRYPTO) return 0;
    result = tiku_choice_event(&p->choice, ev, l.edit, c,
                               p->dialog.focus == DP_EDIT);
    if (result == TIKU_CHOICE_CHANGED) {
        char value[32];
        if (p->setting == DP_CPU)
            snprintf(value, sizeof value, "%lu",
                     p->clock.rates[p->choice.selected]);
        else snprintf(value, sizeof value, "%s",
                      p->choice.selected ? "sw" : "auto");
        tiku_field_start(&p->field, value);
    }
    return result != TIKU_CHOICE_IGNORED;
}

/** @brief Shared controls own gestures; this panel owns only device actions. */
static int dp_event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    struct desktop_deviceprefs *p = ctx;
    tiku_rect_t c = tiku_window_content(w);
    dp_layout_t l = dp_layout(c, p->setting);
    tiku_dialog_result_t result;
    dp_controls(p, l);
    if (ev->type == TIKU_EVENT_DEACTIVATED) {
        (void)tiku_dialog_event(&p->dialog, ev);
        tiku_choice_close(&p->choice);
        p->input.dragging = 0;
        p->app->dirty = 1;
        return 1;
    }
    if ((p->choice.open || (p->dialog.focus == DP_EDIT &&
         ev->type == TIKU_EVENT_KEY_DOWN)) && dp_choice_event(p, ev, l, c)) {
        p->app->dirty = 1;
        return 1;
    }
    result = tiku_dialog_event(&p->dialog, ev);
    /* Cmd-W closes the panel.  Cmd-Q is the desk's, answered before a
     * key reaches here, and is not claimed twice. */
    if (ev->type == TIKU_EVENT_KEY_DOWN && (ev->modifiers & TIKU_MOD_CMD) &&
        ev->key == 'w') result.activated = DP_DONE;
    if (result.activated == DP_DONE) {
        desktop_deviceprefs_close(p->app);
        return 1;
    }
    if (result.activated >= DP_DEVICE &&
        result.activated < DP_DEVICE + p->count) {
        p->selected = result.activated - DP_DEVICE;
        memset(p->values, 0, sizeof p->values);
        dp_request(p, -1);
    } else if (result.activated >= DP_SETTING &&
               result.activated < DP_SETTING + DP_COUNT) {
        p->setting = result.activated - DP_SETTING;
        dp_edit(p);
        dp_controls(p, dp_layout(c, p->setting));
        tiku_dialog_focus(&p->dialog, DP_EDIT);
    } else if (result.activated == DP_REFRESH) dp_refresh(p);
    else if (result.activated == DP_APPLY) dp_apply(p);
    else if (!result.taken && !p->busy) {
        if (!dp_choice_event(p, ev, l, c) &&
            p->values[p->setting].writable &&
            (p->setting == DP_NAME || p->setting == DP_RADIO)) {
            if (tiku_field_event(&p->field, &p->input, ev, l.edit,
                p->dialog.focus == DP_EDIT) == TIKU_FIELD_REJECTED)
                snprintf(p->status, sizeof p->status,
                    "Text is too long or contains unsupported characters.");
        }
    }
    p->app->dirty = 1;
    return 1;
}

/** @brief An open dropdown owns outside clicks until dismissed. */
int desktop_deviceprefs_popup(desktop_t *app)
{
    return app->deviceprefs && app->deviceprefs->window &&
           app->deviceprefs->window->open && app->deviceprefs->choice.open;
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
    if (app->prefs_track == w) app->prefs_track = NULL;
    tiku_choice_close(&app->deviceprefs->choice);
    tiku_workspace_close(app->desk, w);
    app->deviceprefs->window = NULL;
    app->dirty = 1;
}

/** @brief Open Settings > Devices Preferences, or raise its existing window. */
void desktop_deviceprefs_open(desktop_t *app)
{
    struct desktop_deviceprefs *p = app->deviceprefs;
    tiku_rect_t r;
    dp_layout_t l;
    if (p == NULL) {
        p = calloc(1, sizeof *p);
        if (p == NULL) return;
        p->app = app;
        p->refs = 1;
        p->setting = DP_CPU;
        tiku_dialog_init(&p->dialog);
        tiku_choice_init(&p->choice);
        app->deviceprefs = p;
    }
    if (p->window != NULL && p->window->open) {
        tiku_workspace_activate(app->desk, p->window);
        return;
    }
    r.w = 270 + tiku_text_width(tiku_font_plain(),
            "Radio transmit power  128 MHz now / 64 MHz saved");
    if (r.w < 760) r.w = 760;
    l = dp_layout((tiku_rect_t){0, 0, r.w, 0}, p->setting);
    r.h = l.done.y + l.done.h + 44;
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
