/* Bounded jobs: explicit VFS navigation and periodic read-only monitoring. */

/** Closing one window cancels only its work, not other clients of the mount. */
static int utility_cancelled(utility_job_t *j)
{ return atomic_load(&j->cancelled) || tiku_work_cancelled(j); }

/** Refuse truncated values, embedded NUL and non-text data. */
static int utility_read(utility_job_t *j, const char *path, char *out, size_t cap)
{
    out[0] = 0;
    if (utility_cancelled(j) || !j->backend->ops->read) return 0;
    int n = j->backend->ops->read(j->backend, path, out, cap - 1);
    if (n < 0 || (size_t)n >= cap - 1) { out[0] = 0; return 0; }
    for (int i = 0; i < n; i++)
        if ((!out[i] || (unsigned char)out[i] < 32) &&
            out[i] != '\n' && out[i] != '\r' && out[i] != '\t') {
            out[0] = 0; return 0;
        }
    out[n] = 0;
    while (n && (out[n-1] == '\r' || out[n-1] == '\n')) out[--n] = 0;
    return 1;
}

/** Copy directory metadata without invoking endpoint read handlers. */
static int utility_entry(const tiku_model_t *m, void *ctx)
{
    utility_job_t *j = ctx;
    if (utility_cancelled(j)) return 1;
    if (j->count == U_ROWS) { j->truncated = 1; return 1; }
    if (!m->name[0] || strchr(m->name, '/') ||
        strlen(m->name) >= sizeof j->entries[0].name ||
        !strcmp(m->name, ".") || !strcmp(m->name, "..")) return 0;
    utility_entry_t *e = &j->entries[j->count++];
    snprintf(e->name, sizeof e->name, "%s", m->name);
    e->directory = tiku_model_is_container(m);
    e->readable = (m->facts.perm & TIKU_P_READ) != 0;
    e->writable = tiku_model_is_writable(m);
    return 0;
}

/** Populate observational values; permissions alone do not imply safe reads. */
static void utility_directory_values(utility_job_t *j, int64_t deadline)
{
    for (int i = 0; i < j->count && !utility_cancelled(j); i++) {
        utility_entry_t *e = &j->entries[i]; char path[256], value[2048];
        int n = snprintf(path, sizeof path, "%s%s%s", j->path,
            strcmp(j->path, "/") ? "/" : "", e->name);
        if (e->directory || !e->readable || n >= (int)sizeof path ||
            !utility_autoread(path)) continue;
        if (j->reads >= 256 || tiku_trk_now() >= deadline) {
            j->limited = 1; break;
        }
        j->reads++;
        e->sampled = utility_read(j, path, value, sizeof value) ? 1 : -1;
        snprintf(e->data, sizeof e->data, "%.127s", value);
    }
}

/** Breadth-first discovery is bounded, cancellable, and entirely off the UI loop. */
static void utility_preload(utility_job_t *j)
{
    utility_snapshot_t *tail = calloc(1, sizeof *tail);
    if (!tail) { snprintf(j->status, sizeof j->status, "Unable to allocate device snapshot."); return; }
    j->cache = tail; strcpy(tail->path, "/");
    int directories = 1, failures = 0;
    int64_t deadline = tiku_trk_now() + 30000000;
    for (utility_snapshot_t *s = j->cache; s && !utility_cancelled(j); s = s->next) {
        if (tiku_trk_now() >= deadline) { j->limited = 1; break; }
        strcpy(j->path, s->path); j->count = j->truncated = 0;
        memset(j->entries, 0, sizeof j->entries);
        s->ok = j->backend->ops->list &&
            j->backend->ops->list(j->backend, s->path, utility_entry, j) >= 0;
        if (!s->ok) { failures++; continue; }
        utility_directory_values(j, deadline);
        s->count = j->count; s->truncated = j->truncated;
        memcpy(s->entries, j->entries, sizeof s->entries);
        if (s->truncated) j->limited = 1;
        for (int i = 0; i < s->count && !utility_cancelled(j); i++) {
            char path[256]; utility_entry_t *e = &s->entries[i];
            if (!e->directory) continue;
            int n = snprintf(path, sizeof path, "%s%s%s", s->path,
                strcmp(s->path, "/") ? "/" : "", e->name);
            if (n >= (int)sizeof path || !utility_path(path)) { j->limited = 1; continue; }
            if (utility_cache_find(j->cache, path)) continue;
            if (directories >= U_ROWS) { j->limited = 1; continue; }
            utility_snapshot_t *child = calloc(1, sizeof *child);
            if (!child) { j->limited = 1; continue; }
            strcpy(child->path, path); tail->next = child; tail = child; directories++;
        }
    }
    snprintf(j->status, sizeof j->status,
        "%s %d values queried. %s",
        j->limited || failures ? "Partial device snapshot." : "Device snapshot loaded.", j->reads,
        "Unqueried endpoints require an explicit read; Refresh reloads the snapshot.");
}

/** Read only actual process directories; a missing field remains unavailable. */
static void utility_processes(utility_job_t *j)
{
    if (utility_cancelled(j) || !j->backend->ops->list ||
        j->backend->ops->list(j->backend, "/proc", utility_entry, j) < 0) return;
    for (int i = 0; i < j->count && j->metrics.nprocess < 8; i++) {
        utility_entry_t *e = &j->entries[i]; double pid;
        if (!e->directory || !utility_number(e->name, &pid)) continue;
        char path[256], values[4][96];
        const char *fields[] = {"name", "state", "sram_used", "events"};
        for (int k = 0; k < 4; k++) {
            snprintf(path, sizeof path, "/proc/%.127s/%s", e->name, fields[k]);
            if (!utility_read(j, path, values[k], sizeof values[k]))
                snprintf(values[k], sizeof values[k], "Unavailable");
        }
        char row[192];
        snprintf(row, sizeof row,
            "%.8s   %.32s   | %.20s | RAM %.20s B | events %.20s",
            e->name, values[0], values[1], values[2], values[3]);
        memcpy(j->metrics.process[j->metrics.nprocess++], row, sizeof row);
        if (utility_cancelled(j)) break;
    }
}

/** Do not infer CPU utilization or whole-bank RAM usage from unrelated counters. */
static void utility_monitor(utility_job_t *j)
{
    const char *paths[] = {"/sys/uptime", "/sys/cpu/freq", "/sys/mem/used",
                          "/sys/mem/stack_free", "/proc/count"};
    double *out[] = {&j->metrics.uptime, &j->metrics.hz, &j->metrics.ram,
                    &j->metrics.stack, &j->metrics.processes};
    char text[2048];
    for (int i = 0; i < 5; i++)
        if (utility_read(j, paths[i], text, sizeof text) &&
            utility_number(text, out[i])) j->metrics.valid |= 1u << i;
    if (utility_read(j, "/sys/mem/nvm_map", text, sizeof text))
        utility_store(text, &j->metrics);
    if (utility_read(j, "/sys/mem/sram", text, sizeof text) &&
        utility_number(text, &j->metrics.sram)) j->metrics.valid |= 64;
    utility_processes(j);
    j->ok = j->metrics.valid != 0;
    snprintf(j->status, sizeof j->status, "%s", j->ok ?
        "Sampled from device. Missing readings are unavailable, never zero-filled." :
        "No telemetry available: device may be disconnected, busy or unsupported.");
}

/** Validate syntax locally, then re-read clock capabilities immediately before writing. */
static int utility_validate(utility_job_t *j)
{
    double n;
    if (!utility_editable(j->path) || !j->input[0] ||
        strpbrk(j->input, "\r\n\t")) return 0;
    if (!strcmp(j->path, "/sys/device/name")) {
        if (strlen(j->input) > 31) return 0;
        for (const char *s = j->input; *s; s++)
            if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_') return 0;
        return 1;
    }
    if (!strcmp(j->path, "/sys/crypto/mode"))
        return !strcmp(j->input, "auto") || !strcmp(j->input, "sw");
    if (!strcmp(j->path, "/sys/radio/txpower")) {
        char *end; errno = 0; long dbm = strtol(j->input, &end, 10);
        return !errno && !*end && dbm >= -100 && dbm <= 30;
    }
    if (!utility_number(j->input, &n) || n > 4294967295.0) return 0;
    if (!strcmp(j->path, "/sys/time")) return 1;
    char mode[32], rates[256], *save;
    if (!utility_read(j, "/sys/cpu/freq_change_mode", mode, sizeof mode) ||
        (strcmp(mode, "live") && strcmp(mode, "reboot")) ||
        !utility_read(j, "/sys/cpu/freq_available", rates, sizeof rates)) return 0;
    for (char *s = strtok_r(rates, " \t", &save); s; s = strtok_r(NULL, " \t", &save)) {
        double rate;
        if (utility_number(s, &rate) && n == rate) return 1;
    }
    return 0;
}

/** Compare the reviewed value, check permissions again, write once, then verify. */
static int utility_write(utility_job_t *j, const tiku_model_t *m)
{
    char current[2048], error[128] = "";
    if (!tiku_model_is_writable(m) || !j->backend->ops->write ||
        !utility_validate(j)) {
        snprintf(j->status, sizeof j->status, "Not saved: unsupported value or setting is no longer writable.");
        return 0;
    }
    int same = utility_read(j, j->path, current, sizeof current);
    if (same && !strcmp(j->path, "/sys/time")) {
        double before, now;
        same = utility_number(j->before, &before) &&
            utility_number(current, &now) && now >= before && now - before <= 300;
    } else same = same && !strcmp(current, j->before);
    if (!same) {
        snprintf(j->status, sizeof j->status, "Not saved: value changed since review. Refresh and review again.");
        return 0;
    }
    if (utility_cancelled(j)) return 0;
    if (j->backend->ops->write(j->backend, j->path, j->input,
        strlen(j->input), error, sizeof error) != 0) {
        snprintf(j->status, sizeof j->status, "Write failed: %.120s. Refresh before retrying.", error);
        return 0;
    }
    if (!utility_read(j, j->path, j->value, sizeof j->value)) {
        snprintf(j->status, sizeof j->status, "Write sent; readback unavailable. Do not retry blindly. Refresh to check.");
        return 0;
    }
    j->ok = 1;
    same = !strcmp(j->input, j->value);
    if (!same && !strcmp(j->path, "/sys/time")) {
        double sent, now;
        same = utility_number(j->input, &sent) && utility_number(j->value, &now) &&
            now >= sent && now - sent <= 5;
    }
    snprintf(j->status, sizeof j->status, "%s", !same ?
        "Write sent; device returned a different value. Showing its response." :
        !strcmp(j->path, "/sys/cpu/freq_target") ?
        "Clock target confirmed. See freq_change_mode; a restart may be needed." :
        "Saved and confirmed by the device.");
    return 1;
}

/** Inspect one path; a writable flag is not the panel's edit policy. */
static void utility_run(void *ctx)
{
    utility_job_t *j = ctx; tiku_model_t m;
    if (utility_cancelled(j)) return;
    if (!j->mode) { utility_monitor(j); return; }
    if (j->preload) { utility_preload(j); return; }
    snprintf(j->status, sizeof j->status, "Path unavailable or device disconnected.");
    if (!utility_path(j->path) || !j->backend->ops->stat ||
        j->backend->ops->stat(j->backend, j->path, &m) != 0) return;
    j->found = 1;
    j->directory = tiku_model_is_container(&m);
    j->readable = (m.facts.perm & TIKU_P_READ) != 0;
    j->writable = tiku_model_is_writable(&m);
    if (j->write) { (void)utility_write(j, &m); return; }
    if (j->directory && j->backend->ops->list) {
        j->ok = j->backend->ops->list(j->backend, j->path, utility_entry, j) >= 0;
        if (j->ok) utility_directory_values(j, tiku_trk_now() + 30000000);
        snprintf(j->status, sizeof j->status, "%s", !j->ok ? "Directory read failed." :
            j->truncated ? "First 128 entries shown. Navigate by exact path for additional entries." :
            "Safe values populated. Select an endpoint to read its complete current value.");
    } else if (j->readable) {
        j->ok = utility_read(j, j->path, j->value, sizeof j->value);
        snprintf(j->status, sizeof j->status, "%s", j->ok ?
            "Value read from device. Writes require review and explicit confirmation." :
            "Read failed, value exceeds 2046 bytes, or endpoint returned non-text data.");
    } else snprintf(j->status, sizeof j->status, "Write-only endpoint. Command endpoints cannot be edited here.");
}

/** Publish only this controller's completed result, never another target's stale data. */
static void utility_done(void *ctx)
{
    utility_job_t *j = ctx; struct desktop_utility *p = j->owner;
    if (utility_cancelled(j)) return;
    j->completed = 1;
    if (!p->app) return;
    if (!p->mode && p->paused) {
        snprintf(p->status, sizeof p->status,
            "Paused snapshot. Resume to acquire a new sample.");
        p->app->dirty = 1; return;
    }
    p->sampled = tiku_trk_now(); p->next = p->sampled + 5000000;
    snprintf(p->status, sizeof p->status, "%s", j->status);
    if (!p->mode) {
        p->metrics = j->metrics;
        tiku_history_push(&p->history[0], p->sampled, p->metrics.ram / 1024,
                          (p->metrics.valid & 4) != 0);
        tiku_history_push(&p->history[1], p->sampled, p->metrics.store_used / 1024,
                          (p->metrics.valid & 32) != 0);
    } else {
        if (j->preload) { utility_preload_result(p, j); p->app->dirty = 1; return; }
        /* A write that never reached the device read nothing back, and
         * its empty result would mark a live endpoint unavailable. */
        if (!j->write || j->ok) {
            utility_registry_result(p, j);
            if (j->directory) utility_cache_store(p, j);
            p->directory = j->directory;
            p->readable = j->readable;
            p->truncated = j->truncated;
            snprintf(p->value, sizeof p->value, "%s", j->value);
            tiku_field_start(&p->edit,
                             strlen(p->value) < 256 ? p->value : "");
        }
        p->writable = j->ok && j->writable; p->armed = 0;
    }
    p->app->dirty = 1;
}

/** Cancelled jobs cannot leave old values looking live or writable. */
static void utility_free(void *ctx)
{
    utility_job_t *j = ctx; struct desktop_utility *p = j->owner;
    p->busy = 0;
    p->job = NULL;
    if (j->posted) utility_jobs--;
    if (p->app && !j->completed) {
        memset(&p->metrics, 0, sizeof p->metrics);
        p->writable = 0; p->armed = 0;
        p->count = 0; p->value[0] = 0;
        utility_tree_reset(p);
        snprintf(p->status, sizeof p->status, "Device disconnected. Reconnect the same device, then Refresh.");
        p->app->dirty = 1;
    }
    if (--p->refs == 0) free(p);
    utility_cache_free(j->cache);
    free(j);
}

/** Resolve through the desktop and serialize with all other work on this mount. */
static void utility_request(struct desktop_utility *p, int write)
{
    if (p->busy) return;
    char full[TIKU_PATH_MAX], local[TIKU_PATH_MAX];
    snprintf(full, sizeof full, "/devices/%s/sys", p->target);
    tiku_backend_t *b = p->target[0] ?
        tiku_backend_serving(p->app->backend, full, local, sizeof local) : NULL;
    if (!b || !tiku_backend_tiku_identity(b)) {
        p->writable = 0; p->armed = 0;
        memset(&p->metrics, 0, sizeof p->metrics);
        p->count = 0; p->value[0] = 0;
        utility_tree_reset(p);
        snprintf(p->status, sizeof p->status, "No connected target. Connect in Devices, then choose Use default or Next device.");
        p->next = tiku_trk_now() + 5000000; return;
    }
    if (!write && p->mode && !p->preload && utility_cache_show(p, p->path)) return;
    utility_job_t *j = calloc(1, sizeof *j);
    if (!j) return;
    atomic_init(&j->cancelled, 0);
    j->owner = p; j->backend = b; j->mode = p->mode; j->write = write;
    j->preload = !write && p->mode && p->preload;
    j->collapse = p->collapse; p->collapse = 0;
    snprintf(j->path, sizeof j->path, "%s", p->path);
    snprintf(j->input, sizeof j->input, "%.255s", p->edit.text);
    snprintf(j->before, sizeof j->before, "%s", p->value);
    tiku_work_job_t work = {.run = utility_run, .done = utility_done,
                           .free = utility_free, .ctx = j};
    snprintf(work.key, sizeof work.key, "board:/devices/%.30s", p->target);
    if (tiku_work_post(&work) != 0) {
        free(j); snprintf(p->status, sizeof p->status, "Desktop busy. Try again."); return;
    }
    p->busy = 1; p->refs++; p->job = j;
    p->preload = 0;
    j->posted = 1; utility_jobs++;
    snprintf(p->status, sizeof p->status, "%s", write ? "Saving and verifying…" :
        j->preload ? "Loading device tree and safe values…" : "Reading device…");
}
