/* Device preferences: capabilities, explicit writes and confirmed readback. */
#include <assert.h>
#include <unistd.h>
#include "tiku_icons_tiku.h"
#include "tiku_measure.h"
#include "../../system/desktop/tiku_trk_desktop_deviceprefs.c"
/** @brief The painter is not linked: a full frame is the flag, as before. */
void desktop_damage_all(desktop_t *app) { app->dirty = 1; }

/** @brief Where setting @p i sits in the outline, scrolled into view. */
static tiku_rect_t dp_setting_row(struct desktop_deviceprefs *p,
                                  dp_layout_t l, int i)
{
    int row;
    dp_outline_build(p);
    row = dp_row_of_setting(p, i);
    assert(row >= 0);
    (void)tiku_list_scroll_to(&p->list, row, l.table);
    return tiku_list_row_rect(&p->list, l.table, row);
}

static char vals[DP_COUNT][96] = {"Desk sensor", "128000000",
                                  "1789041600", "auto", "0", "WFI", "up", "",
                                  ""};
static int writes, readonly, missing, reject, mismatch;
static int clock_supported;
static int config_supported, config_fault;
static int config_unenrolled;
static char config_epoch[33] = "0102030405060708090a0b0c0d0e0f10";
static char config_history[768];
static unsigned config_revision = 7;
static char clock_target[96] = "128000000";
static char clock_mode[16] = "reboot";
static const char *clock_rates = "64000000 128000000";

int desktop_board_names(desktop_t *app, char *out, size_t max)
{
    (void)app;
    return snprintf(out, max, "nrf54l15\nnrf54lm20b\n");
}

/* The facts a board reports beside its settings: a few, so the outline
 * has sections to show; the rest are "not on this firmware". */
static const char *fact_value(const char *path)
{
    static const struct { const char *path, *value; } table[] = {
        { "/sys/device/mcu", "nrf54l15" }, { "/sys/version", "0.06" },
        { "/sys/uptime", "93784" }, { "/sys/mem/sram", "262144" },
        { "/sys/mem/used", "81920" }, { "/sys/mem/free", "172032" },
        { "/sys/mem/stack_free", "6144" }, { "/sys/mem/nvm", "1572864" },
        { "/sys/last_reset", "power" }, { "/sys/boot_count", "41" },
        { "/sys/mem/sram_map", "bank\t262144\nstatic\t40960\nstack\t16384\t6144\n"
                               "tier\t32768\t1200\t1" },
        { "/sys/mem/nvm_map", "kind\tflash\ntotal\t1572864\ncode\t262144\t131072\n"
                              "module\t32768\ntier\t32768\t4096\nstore\t1114112\n"
                              "persist\t8192" },
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (!strcmp(path, table[i].path)) return table[i].value;
    return NULL;
}

/* The layout service: one knob, a staged request, and a store that may
 * hold files, which refuses a move without the erase. */
static unsigned long layout_current = 32768, layout_pending_value;
static unsigned long layout_pending_op, layout_gen = 3, layout_rev = 5;
static const char layout_identity[] = "00112233445566778899aabbccddeeff";
static int layout_pending, layout_pending_erase, layout_files = 1;
static char layout_last_request[128];

static int layout_read(const char *path, char *out, size_t max)
{
    if (!strcmp(path, "/sys/mem/layout/caps"))
        return snprintf(out, max, "nvm.tier\tstore\t4096\t352256\t4096\t32768\n"
                        "methods\terase\n");
    if (!strcmp(path, "/sys/mem/layout/current"))
        return snprintf(out, max, "generation\t%lu\nrevision\t%lu\n"
                        "identity\t%s\nnvm.tier\t%lu\n",
                        layout_gen, layout_rev, layout_identity, layout_current);
    if (!strcmp(path, "/sys/mem/layout/pending"))
        return layout_pending
            ? snprintf(out, max, "op\t%lu\nmethod\t%s\nnvm.tier\t%lu\n",
                       layout_pending_op, layout_pending_erase ? "erase" : "none",
                       layout_pending_value)
            : snprintf(out, max, "none\n");
    if (!strcmp(path, "/sys/mem/layout/status"))
        return snprintf(out, max, "store\tready\nheld\tnone\nrecord\tvalid\n"
                        "tier\t%lu\ncorrected\t0\nphase\tnone\n", layout_current);
    return -1;
}

static int layout_write(const char *path, const char *in, size_t n,
                        char *err, size_t max)
{
    char text[128];
    snprintf(text, sizeof text, "%.*s", (int)n, in);
    if (!strcmp(path, "/sys/mem/layout/stage")) {
        unsigned long bytes = 0, op = 0, g = 0, r = 0;
        char id[64] = "";
        snprintf(layout_last_request, sizeof layout_last_request, "%s", text);
        if (sscanf(text, "nvm.tier=%lu op=%lu identity=%63s expect=%lu:%lu",
                   &bytes, &op, id, &g, &r) != 5)
            { snprintf(err, max, "cannot write (EINVAL)"); return -1; }
        if (strcmp(id, layout_identity))
            { snprintf(err, max, "cannot write (ECONFLICT)"); return -1; }
        if (g != layout_gen || r != layout_rev)
            { snprintf(err, max, "cannot write (ESTALE)"); return -1; }
        if (layout_pending) { snprintf(err, max, "cannot write (EBUSY)"); return -1; }
        if (bytes < 4096 || bytes > 352256 || bytes % 4096)
            { snprintf(err, max, "cannot write (ERANGE)"); return -1; }
        if (layout_files && !strstr(text, "method=erase"))
            { snprintf(err, max, "cannot write (ECONFLICT)"); return -1; }
        layout_pending = 1; layout_pending_op = op; layout_pending_value = bytes;
        layout_pending_erase = strstr(text, "method=erase") != NULL;
        layout_rev++;
        return 0;
    }
    if (!strcmp(path, "/sys/mem/layout/cancel")) {
        if (!layout_pending || strtoul(text, NULL, 10) != layout_pending_op)
            { snprintf(err, max, "cannot write (ENOENT)"); return -1; }
        layout_pending = 0; layout_rev++;
        return 0;
    }
    return 1;   /* not the service's */
}

static int which(const char *path)
{
    if (clock_supported && !strcmp(path, "/sys/cpu/freq_target"))
        return DP_COUNT;
    for (int i = 0; i < DP_COUNT; i++)
        if (!strcmp(path, settings[i].path)) return i;
    return -1;
}

static int mock_stat(tiku_backend_t *b, const char *path, tiku_model_t *m)
{
    int i = which(path);
    (void)b;
    if (config_supported && !strncmp(path, "/sys/config/", 12)) {
        memset(m, 0, sizeof *m); m->facts.perm = TIKU_P_READ | TIKU_P_WRITE;
        return 0;
    }
    if (i < 0 && fact_value(path) != NULL) {
        memset(m, 0, sizeof *m); m->facts.perm = TIKU_P_READ;
        return 0;
    }
    if (!strncmp(path, "/sys/mem/layout/", 16)) {
        memset(m, 0, sizeof *m);
        m->facts.perm = TIKU_P_READ | (readonly ? 0 : TIKU_P_WRITE);
        return 0;
    }
    if (i < 0 || (missing && i == DP_RADIO)) return -1;
    memset(m, 0, sizeof *m);
    m->facts.perm = TIKU_P_READ | (readonly ? 0 : TIKU_P_WRITE);
    return 0;
}

static int mock_read(tiku_backend_t *b, const char *path, void *out, size_t max)
{
    int i = which(path);
    (void)b;
    if (!strcmp(path, "/sys/device/uid")) return snprintf(out, max, "aabbccddeeff0011\n");
    if (config_supported && !strncmp(path, "/sys/config/", 12)) {
        char hex[65];
        const char *value = strstr(path, "frequency") ? clock_target : vals[DP_NAME];
        size_t len = strlen(value);
        if (config_fault) return -1;
        if (!strcmp(path, "/sys/config/status") || !strcmp(path, "/sys/config/provision"))
            return config_unenrolled ? snprintf(out, max, "v1:unenrolled:-\n") :
                snprintf(out, max, "v1:ready:%s\n", config_epoch);
        if (!strcmp(path, "/sys/config/history")) return snprintf(out, max, "%s", config_history);
        assert(len < 32);
        for (size_t k = 0; k < len; k++) snprintf(hex + 2*k, 3, "%02x", (unsigned char)value[k]);
        return snprintf(out, max, "v1:%s:%08x:%s:%s\n", config_epoch,
            config_revision, config_revision ? "applied" : "unset", config_revision ? hex : "");
    }
    if (clock_supported) {
        if (!strcmp(path, "/sys/cpu/freq_available"))
            return snprintf(out, max, "%s\n", clock_rates);
        if (!strcmp(path, "/sys/cpu/freq_change_mode"))
            return snprintf(out, max, "%s\n", clock_mode);
        if (i == DP_COUNT) return snprintf(out, max, "%s\n", clock_target);
    }
    if (i < 0 && fact_value(path) != NULL)
        return snprintf(out, max, "%s\n", fact_value(path));
    if (!strncmp(path, "/sys/mem/layout/", 16)) return layout_read(path, out, max);
    if (i < 0) return -1;
    return snprintf(out, max, "%s\n", vals[i]);
}

static int mock_write(tiku_backend_t *b, const char *path, const void *in,
                      size_t n, char *err, size_t max)
{
    (void)b;
    writes++;
    if (!strncmp(path, "/sys/mem/layout/", 16)) {
        int rc = layout_write(path, in, n, err, max);
        if (rc <= 0) return rc;
    }
    if (config_supported && !strcmp(path, "/sys/config/provision")) {
        assert(config_unenrolled && n == 32);
        snprintf(config_epoch, sizeof config_epoch, "%.*s", (int)n, (const char *)in);
        config_unenrolled = 0; config_revision = 0; config_history[0] = 0;
        return 0;
    }
    if (config_supported && !strcmp(path, "/sys/config/name")) {
        const char *wire = in;
        unsigned rev;
        assert(n >= 77 && sscanf(wire + 33, "%8x", &rev) == 1);
        if (rev != config_revision) {
            snprintf(config_history, sizeof config_history,
                "1:%08x:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:applied:41\n", config_revision);
            snprintf(err, max, "cannot write (ECONFLICT)"); return -1;
        }
        size_t len = (n - 75 - 9)/2;
        for (size_t k = 0; k < len; k++) {
            unsigned c;
            assert(sscanf(wire + 75 + 2*k, "%2x", &c) == 1);
            vals[DP_NAME][k] = (char)c;
        }
        vals[DP_NAME][len] = 0; config_revision++;
        snprintf(config_history, sizeof config_history, "1:%08x:%.32s:applied:%.*s\n",
            config_revision, wire+42, (int)(2*len), wire+75);
        return 0;
    }
    if (reject) {
        snprintf(err, max, "unsupported value");
        return -1;
    }
    if (!mismatch) snprintf(which(path) == DP_COUNT ? clock_target :
        vals[which(path)], sizeof vals[0], "%.*s", (int)n, (const char *)in);
    return 0;
}

/* Exercise complete pointer gestures, not press-only pseudo-clicks. */
static int send_input(tiku_window_t *w, const tiku_event_t *ev)
{
    int result = tiku_window_send(w, ev);
    if (ev->type == TIKU_EVENT_POINTER_DOWN && w->open) {
        tiku_event_t up = *ev;
        up.type = TIKU_EVENT_POINTER_UP;
        result |= tiku_window_send(w, &up);
    }
    return result;
}

int main(void)
{
    char outbox[] = "/tmp/tiku-deviceprefs-outbox-XXXXXX";
    assert(mkdtemp(outbox) && !setenv("TIKU_CONFIG_OUTBOX", outbox, 1));
    assert(tiku_icons_tiku_register() > 0);
    const tiku_backend_ops_t ops = {.stat=mock_stat, .read=mock_read,
                                   .write=mock_write};
    tiku_backend_t backend = {.ops=&ops};
    dp_job_t j = {.backend=&backend, .write_at=-1};
    dp_run(&j);
    assert(writes == 0 && j.values[DP_NAME].writable);
    assert(!j.values[DP_CPU].writable && !j.values[DP_SLEEP].writable);
    j.write_at = DP_NAME;
    snprintf(j.input, sizeof j.input, "Lab sensor");
    dp_run(&j);
    assert(writes == 1 && !strcmp(j.values[DP_NAME].value, "Lab sensor"));
    assert(!strcmp(j.status, "Applied and confirmed by device."));
    readonly = 1;
    dp_run(&j);
    assert(writes == 1 && strstr(j.status, "no longer writable"));
    readonly = 0;
    reject = 1;
    dp_run(&j);
    assert(writes == 2 && strstr(j.status, "unsupported value"));
    reject = 0;
    mismatch = 1;
    snprintf(j.input, sizeof j.input, "Other sensor");
    dp_run(&j);
    assert(strstr(j.status, "different value"));
    memset(j.values, 0, sizeof j.values);
    missing = 1;
    j.write_at = -1;
    dp_run(&j);
    assert(!j.values[DP_RADIO].present);
    clock_supported = 1;
    mismatch = 0;
    j.write_at = DP_CPU;
    snprintf(j.input, sizeof j.input, "64000000");
    dp_run(&j);
    assert(j.values[DP_CPU].writable && j.clock.count == 2);
    assert(!strcmp(j.clock.target, "64000000"));
    assert(!strcmp(j.values[DP_CPU].value, "128000000"));
    assert(strstr(j.status, "Saved for next restart"));
    readonly = 1;
    int before = writes;
    dp_run(&j);
    assert(writes == before && !j.values[DP_CPU].writable);
    readonly = 0;
    snprintf(clock_mode, sizeof clock_mode, "fixed");
    before = writes;
    dp_run(&j);
    assert(writes == before && !j.values[DP_CPU].writable);
    j.write_at = -1;
    dp_run(&j);
    assert(!j.values[DP_CPU].writable);
    snprintf(clock_mode, sizeof clock_mode, "reboot");
    dp_run(&j);
    const char *platform_rates[] = {
        "1000000 2000000 4000000 8000000",
        "12000000 48000000 100000000 125000000 133000000 150000000",
        "96000000 192000000", "96000000 250000000",
        "240000000 480000000 1000000000",
        "100000000 150000000 200000000 300000000 400000000 600000000"
    };
    for (int i = 0; i < 6; i++) {
        clock_rates = platform_rates[i];
        dp_run(&j);
        assert(j.clock.writable && j.clock.count >= 2);
        snprintf(j.input, sizeof j.input, "%lu",
                 j.clock.rates[j.clock.count - 1]);
        j.write_at = DP_CPU;
        dp_run(&j);
        assert(!strcmp(j.clock.target, j.input));
        assert(strstr(j.status, "Saved for next restart"));
        assert(!strcmp(j.values[DP_CPU].value, "128000000"));
        j.write_at = -1;
    }
    clock_rates = "64000000 128000000";
    snprintf(clock_target, sizeof clock_target, "64000000");
    dp_run(&j);
    /* The layout service through the job: the tier read with its bounds,
     * a stage refused while the store holds files, confirmed with the
     * erase, then cancelled; a stale expectation refused. */
    {
        char request[128];
        dp_tier_t seen;
        assert(j.layout.have && j.layout.current == 32768 &&
               j.layout.floor == 4096 && j.layout.ceiling == 352256 &&
               j.layout.step == 4096 && j.layout.erase_ok && !j.layout.pending);
        assert(j.values[DP_TIER].present && j.values[DP_TIER].writable);
        seen = j.layout;
        dp_layout_request(&seen, 16384, 77, 0, request, sizeof request);
        assert(!strcmp(request, "nvm.tier=16384 op=77 "
                       "identity=00112233445566778899aabbccddeeff expect=3:5"));
        j.write_at = DP_TIER;
        snprintf(j.input, sizeof j.input, "%s", request);
        before = writes;
        dp_run(&j);
        assert(writes == before + 1 && j.need_erase && !j.layout.pending);
        assert(strstr(j.status, "Confirm the erase"));
        j.need_erase = 0;
        dp_layout_request(&seen, 16384, 77, 1, request, sizeof request);
        assert(!strcmp(request, "nvm.tier=16384 op=77 "
                       "identity=00112233445566778899aabbccddeeff expect=3:5 "
                       "method=erase"));
        j.write_at = DP_TIER;
        snprintf(j.input, sizeof j.input, "%s", request);
        dp_run(&j);
        assert(!j.need_erase && j.layout.pending && j.layout.pending_op == 77 &&
               j.layout.pending_value == 16384 && j.layout.pending_erase);
        assert(!strcmp(layout_last_request, request));
        assert(strstr(j.status, "Staged for the next restart"));
        assert(strstr(j.values[DP_TIER].value, "32768"));
        j.write_at = DP_TIER;                       /* a second stage: busy */
        dp_layout_request(&j.layout, 8192, 78, 0, request, sizeof request);
        snprintf(j.input, sizeof j.input, "%s", request);
        dp_run(&j);
        assert(strstr(j.status, "already staged"));
        j.write_at = DP_CANCEL_TIER;
        snprintf(j.input, sizeof j.input, "%lu", j.layout.pending_op);
        dp_run(&j);
        assert(!j.layout.pending && strstr(j.status, "cancelled"));
        j.write_at = DP_TIER;                       /* a stale expectation */
        snprintf(j.input, sizeof j.input, "nvm.tier=16384 op=79 identity=%s "
                 "expect=3:5", layout_identity);
        dp_run(&j);
        assert(strstr(j.status, "changed under you"));
        j.write_at = DP_READ; dp_run(&j);
    }
    desktop_t *app = calloc(1, sizeof *app);
    /* Keep the displayed revision across the asynchronous job. A concurrent
     * writer must conflict even when the host refreshes availability first. */
    config_supported = 1;
    j.write_at = -1; dp_run(&j);
    assert(j.config[0].mode == 1 && j.config[0].revision == 7);
    config_revision++;
    j.write_at = DP_NAME; snprintf(j.input, sizeof j.input, "Managed sensor");
    before = writes;
    dp_run(&j);
    assert(writes == before + 1 && j.pending.state == TIKU_CONFIG_PENDING_CONFLICT);
    assert(strcmp(vals[DP_NAME], "Managed sensor"));
    before = writes;
    dp_run(&j); /* Unresolved local record prevents another save. */
    assert(writes == before && strstr(j.status, "Resolve Configuration recovery"));
    j.write_at = DP_DISCARD; dp_run(&j);
    assert(j.pending.state == TIKU_CONFIG_PENDING_NONE && writes == before);
    j.write_at = DP_NAME;
    dp_run(&j); /* new explicit save after resolution: revision 8 */
    assert(!strcmp(vals[DP_NAME], "Managed sensor") && config_revision == 9);
    assert(strstr(j.status, "Applied and confirmed"));
    config_fault = 1; before = writes;
    dp_run(&j);
    assert(writes == before && strstr(j.status, "Refresh before saving"));
    config_fault = 0;
    config_unenrolled = 1;
    j.write_at = DP_READ; dp_run(&j);
    assert(j.config[0].enrollable && j.can_enroll && writes == before); /* discovery never enrolls */
    char previous[96]; snprintf(previous, sizeof previous, "%s", vals[DP_NAME]);
    j.write_at = DP_ENROLL; dp_run(&j);
    assert(writes == before+1 && !config_unenrolled && j.config[0].revision == 0);
    assert(!strcmp(previous, vals[DP_NAME]) && strstr(j.status, "Recovery enabled"));
    assert(j.pending.state == TIKU_CONFIG_PENDING_NONE);
    config_supported = 0;
    j.write_at = -1; dp_run(&j);

    /* A board with no recovery keeps the plain workflow even when the
     * local directory is gone: the fault names no device, so it cannot
     * stand between this board and its own setting. */
    {
        char away[128];
        int made = writes;
        snprintf(away, sizeof away, "%s", outbox);
        assert(!setenv("TIKU_CONFIG_OUTBOX", "/proc/nonexistent-outbox", 1));
        memset(&j.config, 0, sizeof j.config);
        j.write_at = DP_NAME;
        snprintf(j.input, sizeof j.input, "plainboard");
        dp_run(&j);
        assert(writes == made + 1 && !strcmp(vals[DP_NAME], "plainboard"));
        struct desktop_deviceprefs edit = {0};
        edit.setting = DP_NAME;
        edit.values[DP_NAME].writable = 1;
        edit.pending.state = TIKU_CONFIG_PENDING_ERROR;
        assert(dp_can_edit(&edit));          /* no record names this board */
        snprintf(edit.pending.uid, sizeof edit.pending.uid, "aabbccddeeff0011");
        assert(!dp_can_edit(&edit));         /* a record does */
        assert(!setenv("TIKU_CONFIG_OUTBOX", away, 1));
        j.write_at = -1; dp_run(&j);
    }
    struct desktop_deviceprefs *pending = calloc(1, sizeof *pending);
    dp_job_t *cancelled = calloc(1, sizeof *cancelled);
    assert(app && pending && cancelled);
    pending->app = app;
    pending->refs = 2;
    pending->busy = 1;
    app->deviceprefs = pending;
    cancelled->owner = pending;
    dp_free(cancelled);
    assert(!pending->busy && pending->refs == 1);
    assert(strstr(pending->status, "disconnected"));
    dp_job_t *late = calloc(1, sizeof *late);
    assert(late);
    late->owner = pending;
    pending->refs++;
    desktop_deviceprefs_destroy(app);
    assert(pending->refs == 1 && pending->app == NULL);
    dp_done(late);
    dp_free(late);
    free(app);
    struct desktop_deviceprefs p = {.count=2, .selected=0, .setting=DP_NAME,
                                    .fact=-1};
    tiku_list_init(&p.list, 1);
    memcpy(p.values, j.values, sizeof p.values);
    memcpy(p.fact_values, j.fact_values, sizeof p.fact_values);
    memcpy(p.sram_map, j.sram_map, sizeof p.sram_map);
    memcpy(p.nvm_map, j.nvm_map, sizeof p.nvm_map);
    p.layout = j.layout;
    p.clock = j.clock;
    /* The section on show: its heading first, then its settings and the
     * facts the device answered, in bytes and spans a person reads; the
     * source list holds every board and the chosen one's sections. */
    {
        char text[128];
        int memory = 0, board[DP_TREE], section[DP_TREE];
        int estates = 0;
        p.fact = -1;
        p.estate = -1;
        dp_show_section(&p, SEC_MEMORY);
        assert(p.rows[0].heading == 1 && p.rows[0].section == SEC_MEMORY);
        /* The PSRAM switch is the section's setting, so it is shown. */
        assert(p.fact < 0 && p.setting == DP_PSRAM);
        for (int i = 0; i < p.nrow; i++) {
            memory += p.rows[i].section == SEC_MEMORY && p.rows[i].fact >= 0;
            estates += p.rows[i].estate >= 0;
        }
        assert(memory == 4 && estates == 3);   /* static, stack, tier */
        assert(p.estate_total[0] == 262144UL);
        for (int i = 0; i < p.nestate; i++) {
            if (!strcmp(p.estates[i].key, "stack"))
                assert(!strcmp(p.estates[i].value, "16.0 KB, 6.0 KB headroom"));
            if (!strcmp(p.estates[i].key, "code"))
                assert(!strcmp(p.estates[i].value, "256.0 KB, 128.0 KB used"));
        }
        dp_show_section(&p, SEC_POWER);
        assert(p.fact < 0 && p.setting == DP_SLEEP);
        dp_show_section(&p, SEC_STORAGE);
        estates = 0;
        for (int i = 0; i < p.nrow; i++) estates += p.rows[i].estate >= 0;
        assert(estates == 5 && p.estate_total[1] == 1572864UL);
        /* No eMMC here, so its switch is not listed; the tier is, and it
         * is the section's setting.  Its editor takes KB and refuses a
         * size off the step, out of bounds, or unchanged. */
        assert(p.setting == DP_TIER && p.fact < 0);
        for (int i = 0; i < p.nrow; i++) assert(p.rows[i].setting != DP_EMMC);
        dp_setting_text(&p, DP_TIER, text, sizeof text);
        assert(!strcmp(text, "32.0 KB"));
        dp_edit(&p);
        assert(!strcmp(p.field.text, "32"));
        tiku_field_start(&p.field, "33"); dp_apply(&p);
        assert(strstr(p.status, "steps of 4 KB") && !p.busy);
        tiku_field_start(&p.field, "400"); dp_apply(&p);
        assert(strstr(p.status, "Use 4 to 344 KB") && !p.busy);
        tiku_field_start(&p.field, "32"); dp_apply(&p);
        assert(strstr(p.status, "already") && !p.busy);
        p.layout.pending = 1; p.layout.pending_value = 16384;
        dp_setting_text(&p, DP_TIER, text, sizeof text);
        assert(!strcmp(text, "32.0 KB now / 16.0 KB at restart"));
        p.layout.pending = 0;
        dp_fact_text(FMT_BYTES, "262144", text, sizeof text);
        assert(!strcmp(text, "256.0 KB"));
        dp_fact_text(FMT_SECONDS, "93784", text, sizeof text);
        assert(!strcmp(text, "1d 2h 3m"));
        assert(dp_layout((tiku_rect_t){0, 0, 820, 760}, DP_CPU).table.h >
               dp_layout((tiku_rect_t){0, 0, 820, 0}, DP_CPU).table.h);
        assert(dp_tree(&p, board, section) == 2 + SEC_COUNT);
        assert(board[0] == 0 && section[1] == SEC_IDENTITY &&
               board[1 + SEC_COUNT] == 1);
        dp_show_section(&p, SEC_PROCESSOR);
        /* A section the board has nothing for is greyed out and cannot
         * be chosen; before any read, every section is offered. */
        {
            struct desktop_deviceprefs bare = p;
            dp_value_t sleep = p.values[DP_SLEEP];
            /* The radio went missing earlier in this run: greyed. */
            assert(!dp_section_available(&p, SEC_RADIO));
            assert(dp_section_available(&p, SEC_POWER));
            p.values[DP_SLEEP].present = 0;
            assert(!dp_section_available(&p, SEC_POWER));
            assert(dp_section_available(&p, SEC_MEMORY));
            p.values[DP_SLEEP] = sleep;
            memset(bare.values, 0, sizeof bare.values);
            memset(bare.fact_values, 0, sizeof bare.fact_values);
            assert(dp_section_available(&bare, SEC_RADIO));
        }
    }
    snprintf(p.names[0], sizeof p.names[0], "nrf54l15");
    snprintf(p.names[1], sizeof p.names[1], "nrf54lm20b");
    snprintf(p.status, sizeof p.status,
             "Settings read from device.");
    p.setting = DP_CPU;
    dp_edit(&p);
    assert(dp_clock_pending(&p) && !dp_clock_dirty(&p));
    assert(strstr(dp_clock_help(&p), "Restart the device"));
    snprintf(p.values[DP_CPU].value, sizeof p.values[DP_CPU].value,
             "63999000");
    /* measurement noise is not a pending change */
    assert(!dp_clock_pending(&p));
    snprintf(p.values[DP_CPU].value, sizeof p.values[DP_CPU].value,
             "128000000");
    dp_clock_t supported_clock = p.clock;
    memset(&p.clock, 0, sizeof p.clock);
    assert(strstr(dp_clock_help(&p), "newer device firmware"));
    snprintf(p.clock.mode, sizeof p.clock.mode, "fixed");
    assert(strstr(dp_clock_help(&p), "fixed clock"));
    p.clock = supported_clock;
    p.clock.writable = 0;
    assert(strstr(dp_clock_help(&p), "read-only"));
    p.clock = supported_clock;
    for (int dark = 0; dark < 2; dark++) {
        char path[80];
        tiku_theme_set(dark ? tiku_theme_dark() : NULL);
        dp_layout_t dl = dp_layout((tiku_rect_t){0,0,820,0}, p.setting);
        int height = dl.done.y + dl.done.h + 18;
        tiku_surface_t *s = tiku_surface_new(820, height, TIKU_C_PANEL);
        p.choice.open = dark;
        dp_draw(NULL, s, (tiku_rect_t){0,0,820,height}, &p);
        snprintf(path, sizeof path, "/tmp/tiku-deviceprefs-%d.png", dark);
        assert(tiku_surface_png(s, path) == 0);
        tiku_surface_free(s);
    }
    tiku_theme_set(NULL);
    {
        /* Storage with the tier chosen: the KB field and Save for restart;
         * then the same with a change staged, the cancel and the pill. */
        struct desktop_deviceprefs preview = p;
        dp_layout_t dl = dp_layout((tiku_rect_t){0,0,820,0}, DP_TIER);
        int height = dl.done.y + dl.done.h + 18;
        tiku_surface_t *s = tiku_surface_new(820, height, TIKU_C_PANEL);
        preview.choice.open = 0;
        dp_show_section(&preview, SEC_STORAGE);
        dp_edit(&preview);
        dp_draw(NULL, s, (tiku_rect_t){0,0,820,height}, &preview);
        assert(tiku_surface_png(s, "/tmp/tiku-deviceprefs-tier.png") == 0);
        preview.layout.pending = 1;
        preview.layout.pending_value = 16384;
        preview.layout.pending_op = 77;
        preview.layout.pending_erase = 1;
        dp_edit(&preview);
        dp_draw(NULL, s, (tiku_rect_t){0,0,820,height}, &preview);
        assert(tiku_surface_png(s, "/tmp/tiku-deviceprefs-tier-staged.png") == 0);
        tiku_surface_free(s);
    }
    {
        /* Memory, with the stack pool chosen: the bar under its figures. */
        struct desktop_deviceprefs preview = p;
        dp_layout_t dl = dp_layout((tiku_rect_t){0,0,820,0}, p.setting);
        int height = dl.done.y + dl.done.h + 18;
        tiku_surface_t *s = tiku_surface_new(820, height, TIKU_C_PANEL);
        preview.choice.open = 0;
        dp_show_section(&preview, SEC_MEMORY);
        for (int i = 0; i < preview.nestate; i++)
            if (!strcmp(preview.estates[i].key, "stack")) preview.estate = i;
        dp_draw(NULL, s, (tiku_rect_t){0,0,820,height}, &preview);
        assert(tiku_surface_png(s, "/tmp/tiku-deviceprefs-memory.png") == 0);
        tiku_surface_free(s);
    }
    {
        struct desktop_deviceprefs preview = p;
        dp_layout_t dl = dp_layout((tiku_rect_t){0,0,760,0}, p.setting);
        int height = dl.done.y + dl.done.h + 18;
        tiku_surface_t *s = tiku_surface_new(760, height, TIKU_C_PANEL);
        preview.choice.open = 0;
        preview.setting = DP_SLEEP;
        dp_draw(NULL, s, (tiku_rect_t){0,0,760,height}, &preview);
        assert(tiku_surface_png(s, "/tmp/tiku-deviceprefs-readonly.png") == 0);
        preview.can_enroll = preview.config[0].enrollable = 1;
        preview.confirm = 1;
        dp_draw(NULL, s, (tiku_rect_t){0,0,760,height}, &preview);
        assert(tiku_surface_png(s, "/tmp/tiku-deviceprefs-enroll.png") == 0);
        preview.confirm = 0;
        preview.pending.state = TIKU_CONFIG_PENDING_RETRY;
        snprintf(preview.pending.path, sizeof preview.pending.path, "/sys/device/name");
        snprintf(preview.pending.uid, sizeof preview.pending.uid, "aabbccddeeff0011");
        snprintf(preview.pending.summary, sizeof preview.pending.summary,
                 "A saved request is unconfirmed. Retry sends the exact original request.");
        tiku_config_snapshot_t saved = {.mode = 1, .revision = 9};
        uint8_t token[16] = {1};
        snprintf(saved.incarnation, sizeof saved.incarnation, "%s", config_epoch);
        assert(tiku_config_request(&saved, token, "Lab sensor", 10,
               preview.pending.request, sizeof preview.pending.request) > 0);
        dp_draw(NULL, s, (tiku_rect_t){0,0,760,height}, &preview);
        assert(tiku_surface_png(s, "/tmp/tiku-deviceprefs-recovery.png") == 0);
        preview.count = 0;
        preview.setting = DP_CPU;
        memset(preview.values, 0, sizeof preview.values);
        memset(&preview.clock, 0, sizeof preview.clock);
        snprintf(preview.status, sizeof preview.status,
                 "Connect a device from the desktop Devices menu.");
        dp_draw(NULL, s, (tiku_rect_t){0,0,760,height}, &preview);
        assert(tiku_surface_png(s, "/tmp/tiku-deviceprefs-empty.png") == 0);
        tiku_surface_free(s);
    }
    p.choice.open = 0;
    app = calloc(1, sizeof *app);
    app->surf = tiku_surface_new(1100, 800, TIKU_C_PANEL);
    app->desk = tiku_workspace_new(app->surf);
    p.app = app;
    app->deviceprefs = &p;
    p.window = tiku_workspace_open(app->desk, "Devices Preferences",
        (tiku_rect_t){30, 60, 760, 720}, dp_draw, &p, NULL);
    tiku_window_set_handlers(p.window, dp_event, NULL, NULL);
    assert(desktop_deviceprefs_handle(app) == p.window);
    tiku_rect_t c = tiku_window_content(p.window);
    dp_layout_t layout = dp_layout(c, p.setting);
    tiku_event_t ev = {.type=TIKU_EVENT_POINTER_DOWN, .button=1,
        .x=dp_setting_row(&p, layout, DP_CRYPTO).x+4,
        .y=dp_setting_row(&p, layout, DP_CRYPTO).y+4};
    assert(send_input(p.window, &ev));
    assert(p.setting == DP_CRYPTO);
    layout = dp_layout(c, p.setting);
    ev.x = layout.edit.x+4; ev.y = layout.edit.y+4;
    assert(send_input(p.window, &ev));
    assert(p.choice.open);
    ev = (tiku_event_t){.type=TIKU_EVENT_KEY_DOWN, .key=TIKU_KEY_DOWN};
    assert(send_input(p.window, &ev));
    ev.key = TIKU_KEY_RETURN;
    assert(send_input(p.window, &ev));
    assert(!strcmp(p.field.text, "sw"));
    ev = (tiku_event_t){.type=TIKU_EVENT_POINTER_DOWN, .button=1};
    ev.x = dp_setting_row(&p, layout, DP_NAME).x+4;
    ev.y = dp_setting_row(&p, layout, DP_NAME).y+4;
    assert(send_input(p.window, &ev));
    assert(p.setting == DP_NAME);
    ev = (tiku_event_t){.type=TIKU_EVENT_KEY_DOWN,
        .key='a', .modifiers=TIKU_MOD_CMD};
    assert(send_input(p.window, &ev));
    ev = (tiku_event_t){.type=TIKU_EVENT_KEY_DOWN,
        .key='X', .text="X"};
    assert(send_input(p.window, &ev));
    assert(!strcmp(p.field.text, "X"));
    ev = (tiku_event_t){.type=TIKU_EVENT_POINTER_DOWN, .button=1,
        .x=dp_setting_row(&p, layout, DP_CPU).x+4,
        .y=dp_setting_row(&p, layout, DP_CPU).y+4};
    assert(send_input(p.window, &ev));
    assert(p.setting == DP_CPU);
    layout = dp_layout(c, p.setting);
    assert(layout.done.y + layout.done.h <= c.y + c.h);
    assert(dp_device_row(layout, TIKU_MOUNT_MAX - 1).y +
           dp_device_row(layout, TIKU_MOUNT_MAX - 1).h <=
           layout.devices.y + layout.devices.h);
    before = writes;
    ev = (tiku_event_t){.type=TIKU_EVENT_POINTER_DOWN, .button=1,
        .x=layout.table.x+1, .y=layout.table.y+1};
    assert(send_input(p.window, &ev));
    assert(p.setting == DP_CPU && writes == before);
    ev = (tiku_event_t){.type=TIKU_EVENT_POINTER_DOWN, .button=1,
        .x=layout.edit.x+4, .y=layout.edit.y+4};
    assert(send_input(p.window, &ev));
    assert(p.choice.open);
    tiku_rect_t popup = tiku_choice_popup(&p.choice, layout.edit, c);
    ev.x = popup.x+4;
    ev.y = popup.y+2+tiku_measure_row_h()+4;
    assert(send_input(p.window, &ev));
    assert(!p.choice.open && !strcmp(p.field.text, "128000000"));
    assert(!strcmp(clock_target, "64000000")); /* selection is not a write */
    assert(dp_clock_dirty(&p));
    before = writes;
    ev = (tiku_event_t){.type=TIKU_EVENT_KEY_DOWN, .key=' '};
    assert(send_input(p.window, &ev));
    assert(p.choice.open);
    ev.key = TIKU_KEY_RETURN;
    assert(send_input(p.window, &ev));
    assert(!p.choice.open && writes == before && !p.busy);
    tiku_field_start(&p.field, p.clock.target);
    assert(!dp_clock_dirty(&p));
    dp_apply(&p); /* unchanged Save must not queue a write */
    assert(writes == before && !p.busy);
    p.config[0].enrollable = 1;
    p.can_enroll = 1;
    ev = (tiku_event_t){.type=TIKU_EVENT_POINTER_DOWN, .button=1,
        .x=layout.recover.x+4, .y=layout.recover.y+4};
    assert(send_input(p.window, &ev) && p.confirm == 1 && writes == before);
    ev.x = layout.forget.x+4;
    assert(send_input(p.window, &ev) && !p.confirm && writes == before);
    p.config[0].enrollable = 0;
    p.can_enroll = 0;
    p.pending.state = TIKU_CONFIG_PENDING_UNKNOWN;
    snprintf(p.pending.uid, sizeof p.pending.uid, "aabbccddeeff0011");
    snprintf(p.pending.request, sizeof p.pending.request, "test UI record");
    snprintf(p.pending.summary, sizeof p.pending.summary, "The setting advanced, but its receipt is unavailable. Acceptance cannot be determined.");
    ev.x = layout.recover.x+4;
    assert(send_input(p.window, &ev) && p.confirm == 2 && writes == before);
    assert(!dp_can_edit(&p));
    dp_apply(&p);
    assert(writes == before && !p.busy);
    ev.x = layout.forget.x+4;
    assert(send_input(p.window, &ev) && !p.confirm);
    memset(&p.pending, 0, sizeof p.pending);
    p.busy = 1;
    ev = (tiku_event_t){.type=TIKU_EVENT_POINTER_DOWN, .button=1,
        .x=layout.done.x+4, .y=layout.done.y+4};
    assert(send_input(p.window, &ev));
    assert(desktop_deviceprefs_handle(app) == NULL);
    assert(writes == before); /* Done closes even while busy, without a write. */
    tiku_workspace_free(app->desk);
    tiku_surface_free(app->surf);
    free(app);
    assert(!rmdir(outbox));
    puts("PASS: capabilities, writes, readback, input and teardown");
    return 0;
}
