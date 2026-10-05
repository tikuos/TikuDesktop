/* tiku_device_utilities_io.inl -- part of tiku_device_utilities.c: the
 * worker jobs (the monitor's samples and reclaim commands, the inspector's
 * reads, preload and checked writes) and their results on the loop.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

/** @brief Whether the job was cancelled: its window closed, or the work
 *         queue cancelled it. */
static int utility_cancelled(utility_job_t *j)
{ return atomic_load(&j->cancelled) || tiku_work_cancelled(j); }

/** @brief Read @p path's value as text into @p out, line ends trimmed.
 *  @return 1, or 0 for a failed or cancelled read, a value that fills
 *          @p out, or bytes that are not text. */
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

/** @brief A listing callback: add the node @p m describes to the job's
 *         entries from its metadata, reading no value.  A name that is
 *         empty, "." or "..", too long or holds '/' is skipped; past U_ROWS
 *         the listing is marked truncated.  @return 1 to stop. */
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
    /* The node's read policy, -1 when its metadata states none;
     * utility_autoread() follows a policy before its own list. */
    e->autoread = tiku_attr_read_policy(m->facts.meta);
    return 0;
}

/** @brief Read the values of the listing that may be read without asking
 *         (utility_autoread()), at most 256 reads a job and until
 *         @p deadline; past either the job is marked limited. */
static void utility_directory_values(utility_job_t *j, int64_t deadline)
{
    for (int i = 0; i < j->count && !utility_cancelled(j); i++) {
        utility_entry_t *e = &j->entries[i]; char path[256], value[2048];
        int n = snprintf(path, sizeof path, "%s%s%s", j->path,
            strcmp(j->path, "/") ? "/" : "", e->name);
        if (e->directory || !e->readable || n >= (int)sizeof path ||
            !utility_autoread(e->autoread, path)) continue;
        if (j->reads >= 256 || utility_now() >= deadline) {
            j->limited = 1; break;
        }
        j->reads++;
        e->sampled = utility_read(j, path, value, sizeof value) ? 1 : -1;
        snprintf(e->data, sizeof e->data, "%.127s", value);
    }
}

/** @brief On a worker: list the board breadth first from the root, every
 *         folder but /data, reading the values that may be read without
 *         asking; at most U_ROWS folders and 30 seconds. */
static void utility_preload(utility_job_t *j)
{
    utility_snapshot_t *tail = calloc(1, sizeof *tail);
    if (!tail) { snprintf(j->status, sizeof j->status, "Unable to allocate device snapshot."); return; }
    j->cache = tail; strcpy(tail->path, "/");
    int directories = 1, failures = 0;
    int64_t deadline = utility_now() + 30000000;
    for (utility_snapshot_t *s = j->cache; s && !utility_cancelled(j); s = s->next) {
        if (utility_now() >= deadline) { j->limited = 1; break; }
        /* /data holds the person's files, and listing it can provision
         * the store on first use: its folder shows in the tree, and is
         * listed only when it is opened. */
        if (!strcmp(s->path, "/data")) continue;
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

/** @brief Read up to eight processes from /proc: each numbered folder's
 *         name, state, RAM and events, a field that cannot be read shown
 *         as Unavailable. */
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

/** @brief Read the board's reconstruction job into the metrics: the
 *         running job, or when none runs the last one and whether the mode
 *         is off; the BASIC owner's line; Cancel and Retry stay offered only
 *         when the board lets that control be written.  A job report that
 *         cannot be read or parsed shows Unavailable. */
static void utility_reclaim_sample(utility_job_t *j)
{
    utility_reclaim_t *r = &j->metrics.reclaim;
    char text[2048], mode[24];
    if (!utility_read(j, "/sys/mem/reclaim/job", text, sizeof text) ||
        !utility_reclaim_parse(text, r, 0)) {
        memset(r, 0, sizeof *r);
        strcpy(r->title, "Unavailable");
        strcpy(r->detail, "This firmware does not report reconstruction, or the device is unreachable.");
        return;
    }
    if (!r->active) {
        utility_reclaim_t previous;
        if (utility_read(j, "/sys/mem/reclaim/last", text, sizeof text) &&
            utility_reclaim_parse(text, &previous, 1)) *r = previous;
        if (utility_read(j, "/sys/mem/reclaim/mode", mode, sizeof mode) && !strcmp(mode, "off")) {
            strcpy(r->title, "Disabled");
            strcpy(r->detail, "Automatic reconstruction is disabled on this device.");
        }
    }
    if (utility_read(j, "/sys/mem/reclaim/owners", text, sizeof text))
        utility_reclaim_basic(text, r);
    if (r->can_cancel || r->can_retry) {
        tiku_model_t m;
        const char *path = r->can_retry ? "/sys/mem/reclaim/retry" : "/sys/mem/reclaim/cancel";
        if (!j->backend->ops->stat || j->backend->ops->stat(j->backend, path, &m) != 0 ||
            !tiku_model_is_writable(&m)) r->can_cancel = r->can_retry = 0;
    }
}

/** @brief Send the confirmed Cancel (j->write 2) or Retry (3): only when
 *         the job read again is the one confirmed and the control can be
 *         written; it is sent once and not retried. */
static void utility_reclaim_command(utility_job_t *j)
{
    utility_reclaim_t current; tiku_model_t m;
    char text[512], error[128] = "";
    const char *path = j->write == 3 ? "/sys/mem/reclaim/retry" : "/sys/mem/reclaim/cancel";
    if (!utility_read(j, "/sys/mem/reclaim/job", text, sizeof text) ||
        !utility_reclaim_parse(text, &current, 0) || !current.active ||
        strcmp(current.token, j->input) ||
        !(j->write == 3 ? current.can_retry : current.can_cancel)) {
        snprintf(j->status, sizeof j->status, "Not sent: the reconstruction job changed. Refresh and review again."); return;
    }
    if (!j->backend->ops->stat || !j->backend->ops->write ||
        j->backend->ops->stat(j->backend, path, &m) != 0 || !tiku_model_is_writable(&m)) {
        snprintf(j->status, sizeof j->status, "Not sent: this device does not allow the selected control."); return;
    }
    if (utility_cancelled(j)) return;
    int rc = j->backend->ops->write(j->backend, path, current.token, strlen(current.token), error, sizeof error);
    snprintf(j->status, sizeof j->status, "%s", rc ?
        "Command not confirmed. Refresh before trying again; it may have reached the device." :
        j->write == 3 ? "Recovery retry accepted. Watching device status." :
        "Cancellation accepted. Any required owner recovery will finish first.");
}

/** @brief Sample the board: uptime, clock, RAM used, stack headroom,
 *         process count, the store's use and SRAM, each set in valid only
 *         when the board reported it; then the processes and the reclaim
 *         job.  ok is set when any reading was read. */
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
    utility_reclaim_sample(j);
    j->ok = j->metrics.valid != 0;
    snprintf(j->status, sizeof j->status, "%s", j->ok ?
        "Sampled from device. Missing readings are unavailable, never zero-filled." :
        "No telemetry available: device may be disconnected, busy or unsupported.");
}

/** @brief Whether the job's value suits its setting: a device name of at
 *         most 31 letters, digits, - and _; crypto mode auto or sw; radio
 *         power -100 to 30 dBm; a time up to 2^32 - 1; or a CPU target that
 *         freq_available lists, read now, when freq_change_mode is live or
 *         reboot.  A value holding a tab or line end is refused. */
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

/** @brief Write the reviewed value, when the setting can still be written,
 *         the value is valid and the current value is still the one
 *         reviewed (the clock within five minutes); then read it back.
 *  @return 1 when the write was sent and its value read back. */
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

/** @brief The worker: for the monitor a sample, or only the reclaim panel
 *         after a reclaim command; for the inspector a preload, or one path
 *         read or written. */
static void utility_run(void *ctx)
{
    utility_job_t *j = ctx; tiku_model_t m;
    if (utility_cancelled(j)) return;
    if (!j->mode) {
        if (j->reclaim_only) {
            snprintf(j->status, sizeof j->status, "Reconstruction status updated; memory readings keep their original sample time.");
            if (j->write == 2 || j->write == 3) utility_reclaim_command(j);
            utility_reclaim_sample(j);
        } else utility_monitor(j);
        return;
    }
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
        if (j->ok) utility_directory_values(j, utility_now() + 30000000);
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

/** @brief On the loop: apply a finished job's result to its window,
 *         unless it was cancelled or the window has gone; a paused monitor
 *         keeps its sample. */
static void utility_done(void *ctx)
{
    utility_job_t *j = ctx; struct device_utility *p = j->owner;
    if (utility_cancelled(j)) return;
    j->completed = 1;
    if (!p->app) return;
    if (!p->mode && p->paused && j->write < 2) {
        snprintf(p->status, sizeof p->status,
            "Paused snapshot. Resume to acquire a new sample.");
        p->app->dirty = 1; return;
    }
    int64_t now = utility_now();
    if (!j->reclaim_only) p->sampled = now;
    p->next = now + (!p->mode && j->metrics.reclaim.active ? 1000000 : 5000000);
    snprintf(p->status, sizeof p->status, "%s", j->status);
    if (!p->mode) {
        if (j->reclaim_only) p->metrics.reclaim = j->metrics.reclaim;
        else p->metrics = j->metrics;
        if (strcmp(p->reclaim_confirm_token, p->metrics.reclaim.token) ||
            !(p->reclaim_confirm == 3 ? p->metrics.reclaim.can_retry : p->metrics.reclaim.can_cancel))
            p->reclaim_confirm = 0;
        if (!j->reclaim_only) {
        tiku_history_push(&p->history[0], p->sampled, p->metrics.ram / 1024,
                          (p->metrics.valid & 4) != 0);
        tiku_history_push(&p->history[1], p->sampled, p->metrics.store_used / 1024,
                          (p->metrics.valid & 32) != 0);
        }
    } else {
        if (j->preload) { utility_preload_result(p, j); p->app->dirty = 1; return; }
        /* A failed write read nothing back: the value and the listing
         * shown stay as they were. */
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

/** @brief On the loop, after done or in its place: free the job and
 *         release the utility.  When the job did not complete, its window's
 *         readings, value and tree are cleared and its status says the
 *         device disconnected. */
static void utility_free(void *ctx)
{
    utility_job_t *j = ctx; struct device_utility *p = j->owner;
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
    if (--p->refs == 0) { tiku_backend_close(p->backend); free(p); }
    utility_cache_free(j->cache);
    free(j);
}

/** @brief Post the window's next job: a read, or with @p write 1 a write
 *         and with 2 or 3 a reclaim Cancel or Retry, through a VFS proxy for
 *         its board and keyed to that board.  Nothing while a job runs; a
 *         folder in the cache is shown with no job; with no board the
 *         window is emptied. */
static void utility_request(struct device_utility *p, int write)
{
    if (p->busy) return;
    if (p->backend && strcmp(p->proxy_target, p->target)) {
        tiku_backend_close(p->backend); p->backend = NULL;
    }
    if (!p->backend && p->target[0] && p->app->services.send_message) {
        p->backend = tiku_vfs_proxy_new(p->target);
        snprintf(p->proxy_target, sizeof p->proxy_target, "%s", p->target);
    }
    tiku_backend_t *b = p->backend;
    if (!b) {
        p->writable = 0; p->armed = 0;
        memset(&p->metrics, 0, sizeof p->metrics);
        p->count = 0; p->value[0] = 0;
        utility_tree_reset(p);
        snprintf(p->status, sizeof p->status, "No connected target. Connect in Devices, then choose Use default or Next device.");
        p->next = utility_now() + 5000000; return;
    }
    if (!write && p->mode && !p->preload && utility_cache_show(p, p->path)) return;
    utility_job_t *j = calloc(1, sizeof *j);
    if (!j) return;
    atomic_init(&j->cancelled, 0);
    j->owner = p; j->backend = b; j->mode = p->mode; j->write = write;
    j->reclaim_only = !p->mode && (write >= 2 ||
        (p->metrics.reclaim.active && utility_now() - p->sampled < 5000000));
    j->preload = !write && p->mode && p->preload;
    j->collapse = p->collapse; p->collapse = 0;
    snprintf(j->path, sizeof j->path, "%s", p->path);
    snprintf(j->input, sizeof j->input, "%.255s", !p->mode && write >= 2 ?
             p->reclaim_confirm_token : p->edit.text);
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
