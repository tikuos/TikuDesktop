/* Asynchronous runner, bounded output and explicit device-port ownership. */
typedef struct { char *argv[128]; char words[32][256]; int argc, nword; } command_t;

static int argument(command_t *c, const char *word)
{
    if (c->argc >= 127) return 0;
    c->argv[c->argc++] = (char *)word; c->argv[c->argc] = NULL; return 1;
}

/** argv only: neither checkout paths nor user fields are evaluated by a shell. */
static int command_build(bench_t *st, const job_t *job, command_t *c)
{
    memset(c, 0, sizeof *c);
    if (st->suites[job->suite].host_only) {
        const char *host[] = {"python3", "-u", "-m", "tikubench", "run",
                             st->suites[job->suite].key, "--skip-build"};
        for (size_t i = 0; i < sizeof host / sizeof host[0]; i++) argument(c, host[i]);
        if (*job->only) { argument(c, "--only"); argument(c, job->only); }
        return 1;
    }
    const char *base[] = {"python3", "-u", "-m", "tikubench", "run",
        st->suites[job->suite].key, "--board", st->mcu,
        st->flash ? "--flash" : "--skip-build"};
    for (size_t i = 0; i < sizeof base / sizeof base[0]; i++) argument(c, base[i]);
    if (st->usb) { argument(c, "--console"); argument(c, "usb"); }
    else {
        if (!*st->port || !*st->field[1].text) return 0;
        for (const char *p = st->field[1].text; *p; p++) if (!isdigit((unsigned char)*p)) return 0;
        long baud = strtol(st->field[1].text, NULL, 10);
        if (baud < 1200 || baud > 4000000) return 0;
        argument(c, "--port"); argument(c, st->port);
        argument(c, "--baud"); argument(c, st->field[1].text);
    }
    if (*job->only) { argument(c, "--only"); argument(c, job->only); }
    if (st->large) { argument(c, "--make-var"); argument(c, "MEMORY_MODEL=large"); }
    char vars[TIKU_FIELD_MAX]; snprintf(vars, sizeof vars, "%s", st->field[2].text);
    char *save = NULL;
    for (char *p = strtok_r(vars, " \t", &save); p; p = strtok_r(NULL, " \t", &save)) {
        char *eq = strchr(p, '=');
        if (!eq || eq == p || c->nword == 32 || !strncmp(p, "MEMORY_MODEL=", 13)) return 0;
        for (char *q = p; q < eq; q++) if (!isalnum((unsigned char)*q) && *q != '_') return 0;
        snprintf(c->words[c->nword], sizeof c->words[0], "%s", p);
        argument(c, "--make-var"); argument(c, c->words[c->nword++]);
    }
    if (st->pk) { argument(c, "--make-var"); argument(c, "TIKU_CRACEN_PK_ENABLE=1"); }
    if (st->epaper) {
        argument(c, "--make-var"); argument(c, "HAS_TIKUKITS=1");
        argument(c, "--make-var"); argument(c, "TIKU_KIT_EPAPER_ENABLE=1");
    }
    {
        /* The ePaper presets ride in the same extra-cflags argument as
         * the free-text flags, ahead of them, as the GTK frontend sends
         * them: a panel by its value, a pattern by its index. */
        static const int panel_value[] = {1, 2};
        char *cf = c->words[c->nword]; size_t cap = sizeof c->words[0];
        cf[0] = '\0';
        if (st->epaper) {
            if (c->nword == 32) return 0;
            int pi = st->epanel.selected >= 0 && st->epanel.selected < 2
                     ? st->epanel.selected : 0;
            snprintf(cf, cap,
                     "-DEPAPER_KIT_PANEL=%d -DEPAPER_KIT_PATTERN=%d%s%s",
                     panel_value[pi],
                     st->epattern.selected >= 0 ? st->epattern.selected : 0,
                     *st->field[3].text ? " " : "", st->field[3].text);
            argument(c, "--extra-cflags"); argument(c, c->words[c->nword++]);
        } else if (*st->field[3].text) {
            argument(c, "--extra-cflags"); argument(c, st->field[3].text);
        }
    }
    return 1;
}

/**
 * @brief The first queued job whose options do not build, or -1.
 *
 * A host-only job reads neither the port, the baud nor the make
 * variables, so checking only the head of a queue that leads with one
 * carries a bad option into the batch, where it aborts after the board
 * has already been released.
 */
static int queue_first_invalid(bench_t *st, command_t *c)
{
    for (int i = 0; i < st->njob; i++)
        if (!command_build(st, &st->jobs[i], c)) return i;
    return -1;
}

static void log_line(bench_t *st, const char *text, int output)
{
    logline_t *l = &st->logs[st->log_head];
    snprintf(l->text, sizeof l->text, "%s", text); l->output = output;
    st->log_head = (st->log_head + 1) % 256;
    if (st->nlog < 256) st->nlog++;
    if (st->log) { fprintf(st->log, "%s\n", text); fflush(st->log); }
    st->dirty = 1;
}

static result_t *result_add(bench_t *st, int job, const char *id)
{
    if (st->nresult == NR) { st->batch_failed = 1; return NULL; }
    result_t *r = &st->results[st->nresult++]; memset(r, 0, sizeof *r);
    r->job = job; snprintf(r->id, sizeof r->id, "%s", id);
    snprintf(r->text, sizeof r->text, "Pending: %.150s", id);
    return r;
}

static void plan_build(bench_t *st)
{
    st->nresult = 0;
    for (int k = 0; k < st->njob; k++) {
        const job_t *job = &st->jobs[k]; const suite_t *s = &st->suites[job->suite];
        int before = st->nresult;
        for (int i = s->first; i < s->first + s->count; i++) {
            const leaf_t *l = &st->leaves[i];
            if (*job->only ? csv_has(job->only, l->id) : leaf_supported(st, l, 1))
                result_add(st, k, l->id);
        }
        if (st->nresult == before) result_add(st, k, s->key);
    }
}

/** Host validation may revise a firmware-green row, rather than add a second. */
static void result_end(bench_t *st, const char *id, int pass, int fail, int harness)
{
    result_t *r = NULL;
    for (int i = 0; i < st->nresult; i++)
        if (st->results[i].job == st->job && !strcmp(st->results[i].id, id)) r = &st->results[i];
    if (!r) r = result_add(st, st->job, id);
    if (!r) return;
    r->done = 1; r->pass = pass; r->fail = fail; r->harness = harness;
    r->skip = pass == 0 && fail == 0 && !harness;
    snprintf(r->text, sizeof r->text, "%s %.150s | pass %d, fail %d%s",
        fail || harness ? "FAIL" : r->skip ? "SKIP" : "PASS", id, pass, fail,
        harness ? " | harness validation failed" : "");
    st->dirty = 1;
}

static void consume_line(bench_t *st, char *line)
{
    if (st->loading) { metadata_line(st, line); return; }
    /* Strip ANSI escape sequences; keep readable text in logs and UI. */
    char clean[LINE_MAXIMUM]; size_t at = 0;
    for (size_t i = 0; line[i] && at + 1 < sizeof clean; i++) {
        if ((unsigned char)line[i] == 27 && line[i + 1] == '[') {
            i += 2; while (line[i] && !(line[i] >= '@' && line[i] <= '~')) i++;
            if (!line[i]) break;
        } else if ((unsigned char)line[i] >= 32 || line[i] == '\t') clean[at++] = line[i];
    }
    clean[at] = '\0';
    if (strstr(clean, "[[HITL:UNPLUG]]")) snprintf(st->prompt, sizeof st->prompt, "Action required: unplug the test board");
    else if (strstr(clean, "[[HITL:REPLUG]]")) snprintf(st->prompt, sizeof st->prompt, "Action required: reconnect the test board");
    else if (strstr(clean, "[[HITL:DONE]]")) st->prompt[0] = '\0';
    if (strstr(clean, "[BUILD]") || strstr(clean, "[FLASH]")) st->in_build = 1;
    if (strstr(clean, "[MONITOR]") || strstr(clean, "[TS:BEGIN]")) st->in_build = 0;
    log_line(st, clean, !st->in_build && !strstr(clean, "[TC:"));
    if (strstr(clean, "Build + flash succeeded") || strstr(clean, "Flash OK") ||
        strstr(clean, "Build failed") || strstr(clean, "Flash failed")) st->in_build = 0;
    const char *begin = strstr(clean, "[TC:RUN] ");
    char suite[96], category[160];
    if (begin) snprintf(st->active, sizeof st->active, "%s", begin + 9);
    else if (sscanf(clean, "== %95[^:]: %159s --", suite, category) == 2)
        snprintf(st->active, sizeof st->active, "%s", category);
    if (begin || strstr(clean, "[BUILD]") || strstr(clean, "[FLASH]") || strstr(clean, "[MONITOR]"))
        snprintf(st->note, sizeof st->note, "%.500s", clean);
    char *mark = strstr(clean, "[TC:END] ");
    if (mark) {
        char *counts = strstr(mark + 9, " pass=");
        int pass = 0, fail = 0, harness = 0;
        if (counts && sscanf(counts, " pass=%d fail=%d", &pass, &fail) == 2 &&
            pass >= 0 && fail >= 0 && pass <= 100000000 && fail <= 100000000) {
            char *hf = strstr(counts, " harness_fail=");
            if (hf) harness = atoi(hf + 14) != 0;
            *counts = '\0'; result_end(st, mark + 9, pass, fail, harness);
        } else { st->batch_failed = 1; log_line(st, "Invalid completion marker", 1); }
    } else if (strstr(clean, "[TS:END]")) {
        char *counts = strstr(clean, "total="); int total, pass, fail;
        if (counts && sscanf(counts, "total=%d pass=%d fail=%d", &total, &pass, &fail) == 3 &&
            total >= 0 && pass >= 0 && fail >= 0 && pass <= 100000000 && fail <= 100000000) {
            const char *id = *st->active ? st->active : st->jobs[st->job].key;
            result_end(st, id, pass, fail, 0);
            if (!strcmp(st->jobs[st->job].key, "kernel")) {
                for (int i = 0; i < st->nresult; i++) if (st->results[i].job == st->job &&
                    !strcmp(st->results[i].id, id)) {
                    st->results[i].done = 0;
                    snprintf(st->results[i].text, sizeof st->results[i].text,
                        "Firmware: %.150s | %d/%d | awaiting host validation", id, pass, total);
                }
            }
        }
    } else if (strstr(clean, "[FAIL]") || strstr(clean, "[T:F:") ||
               strstr(clean, "Build failed") || strstr(clean, "Flash failed")) {
        result_t *r = result_add(st, st->job, clean);
        if (r) { r->done = r->fail = 1; snprintf(r->text, sizeof r->text, "FAIL %.240s", clean); }
    } else if (strstr(clean, "[WARN]")) {
        result_t *r = result_add(st, st->job, clean);
        if (r) { r->done = r->skip = 1; snprintf(r->text, sizeof r->text, "WARN %.240s", clean); }
    }
    /* Suites without enumerated leaves still need evidence of actual tests. */
    const suite_t *s = &st->suites[st->jobs[st->job].suite];
    if (!s->count && (strstr(clean, "[PASS]") || strstr(clean, "[T:P:"))) {
        for (int i = 0; i < st->nresult; i++) if (st->results[i].job == st->job &&
            !strcmp(st->results[i].id, s->key)) {
            int pass = st->results[i].pass + 1;
            result_end(st, s->key, pass, st->results[i].fail, st->results[i].harness); break;
        }
    }
}

static int process_spawn(bench_t *st, char *const argv[])
{
    int out[2], input[2];
    if (pipe(out) < 0) return 0;
    fcntl(out[0], F_SETFD, FD_CLOEXEC); fcntl(out[1], F_SETFD, FD_CLOEXEC);
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, input) < 0) {
        close(out[0]); close(out[1]); return 0;
    }
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        dup2(input[1], STDIN_FILENO); dup2(out[1], STDOUT_FILENO); dup2(out[1], STDERR_FILENO);
        close(out[0]); close(out[1]); close(input[0]); close(input[1]);
        if (chdir(st->directory) != 0) { perror("TikuBench checkout"); _exit(127); }
        execvp(argv[0], argv); perror("TikuBench runner"); _exit(127);
    }
    close(out[1]); close(input[1]);
    if (pid < 0) { close(out[0]); close(input[0]); return 0; }
    setpgid(pid, pid);
    st->child = pid; st->child_fd = out[0]; st->input_fd = input[0];
    fcntl(st->child_fd, F_SETFL, O_NONBLOCK); fcntl(st->input_fd, F_SETFL, O_NONBLOCK);
    st->eof = st->reaped = st->overflow = 0; st->partial_n = 0;
    return 1;
}

static void metadata_start(bench_t *st)
{
    if (st->busy || st->child || st->closing) return;
    st->nsuite = st->nleaf = st->nboard = st->nhistory = st->nmetric = 0;
    st->metadata_bad = st->metadata_ok = 0; st->loading = 1;
    char *argv[] = {"python3", "gui/tb_meta.py", NULL};
    if (!process_spawn(st, argv)) {
        st->loading = 0; snprintf(st->note, sizeof st->note, "Could not start metadata reader");
    } else snprintf(st->note, sizeof st->note, "Reading TikuBench registry and history...");
    st->dirty = 1;
}

static void release_finish(bench_t *st)
{
    for (int i = 0; i < st->nresult; i++) if (!st->results[i].done) {
        result_t *r = &st->results[i]; r->done = 1;
        r->skip = st->cancelled; r->harness = !st->cancelled;
        snprintf(r->text, sizeof r->text, "%s: %.180s",
            st->cancelled ? "Not run (cancelled)" : "FAIL incomplete run", r->id);
    }
    st->no_tests = 1;
    for (int i = 0; i < st->nresult; i++)
        if (st->results[i].pass || st->results[i].fail) st->no_tests = 0;
    if (st->log) { fclose(st->log); st->log = NULL; }
    snprintf(st->note, sizeof st->note, "%s. Log: %.400s",
        st->cancelled ? "Stopped" : st->batch_failed ? "Finished with failures" :
        st->no_tests ? "No tests ran (skipped)" : "Finished",
        st->log_path);
    if (st->released && st->services->board_reclaim) {
        st->reclaiming = 1; st->deadline = st->now + 15000000;
        if (!st->services->board_reclaim(st->services->ctx, st->target_name)) {
            st->reclaiming = st->released = st->busy = 0;
            snprintf(st->note, sizeof st->note, "Device could not be reclaimed. Reconnect through Devices.");
        }
    } else st->busy = 0;
    st->dirty = 1;
}

static void run_next(bench_t *st)
{
    if (st->job >= st->njob || st->cancelled) { release_finish(st); return; }
    command_t command;
    if (!command_build(st, &st->jobs[st->job], &command) || !process_spawn(st, command.argv)) {
        st->batch_failed = 1; log_line(st, "Runner could not start", 1);
        release_finish(st); return;
    }
    st->in_build = 0;
    st->active[0] = st->prompt[0] = '\0'; st->send_n = st->send_at = 0;
    char text[512];
    const suite_t *s = &st->suites[st->jobs[st->job].suite];
    snprintf(text, sizeof text, "Suite %d/%d: %s on %s (%s)", st->job + 1, st->njob,
             s->key, s->host_only ? "this computer" : st->target_name,
             s->host_only ? "host only" : st->profile);
    log_line(st, text, 1); snprintf(st->note, sizeof st->note, "%s", text);
    for (int i = 0; i < command.argc; i++) log_line(st, command.argv[i], 0);
}

static void job_finish(bench_t *st)
{
    int bad = !WIFEXITED(st->exit_status) || WEXITSTATUS(st->exit_status) != 0;
    for (int i = 0; i < st->nresult; i++) {
        result_t *r = &st->results[i]; if (r->job != st->job) continue;
        if (!r->done) {
            r->done = r->harness = 1;
            snprintf(r->text, sizeof r->text, "%s: %.180s",
                     st->cancelled ? "Cancelled" : "FAIL missing completion", r->id);
        }
        bad |= r->fail || r->harness;
    }
    if (bad) {
        st->batch_failed = 1;
        if (!st->cancelled) {
            result_t *r = result_add(st, st->job, "Runner status");
            if (r) { r->done = r->harness = 1;
                snprintf(r->text, sizeof r->text, "FAIL: runner exit / incomplete test evidence"); }
        }
    }
    st->job++;
    run_next(st);
}

/** Bound each tick's I/O, preserve final unterminated lines, and reap exit status. */
static void process_poll(bench_t *st)
{
    if (st->child <= 0) return;
    if (st->send_at < st->send_n && st->input_fd >= 0) {
        ssize_t sent = send(st->input_fd, st->sendbuf + st->send_at,
                           st->send_n - st->send_at, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent > 0) st->send_at += (size_t)sent;
        else if (sent < 0 && errno != EAGAIN && errno != EINTR) {
            st->send_n = st->send_at = 0;
            snprintf(st->note, sizeof st->note, "Runner input closed: %s", strerror(errno)); st->dirty = 1;
        }
    }
    char buf[4096];
    for (int budget = 0; budget < 65536 && !st->eof; budget += sizeof buf) {
        ssize_t n = read(st->child_fd, buf, sizeof buf);
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) break;
        if (n <= 0) { st->eof = 1; break; }
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                st->partial[st->partial_n] = '\0';
                if (!st->overflow) consume_line(st, st->partial);
                else { st->metadata_bad = st->batch_failed = 1;
                    log_line(st, "Line exceeded 8191 bytes; run evidence incomplete", 1); }
                st->partial_n = 0; st->overflow = 0;
            } else if (st->partial_n + 1 < sizeof st->partial)
                st->partial[st->partial_n++] = buf[i];
            else st->overflow = 1;
        }
    }
    if (!st->reaped) {
        pid_t got = waitpid(st->child, &st->exit_status, WNOHANG);
        if (got == st->child) {
            st->reaped = 1;
            /* Owned helpers must not outlive their suite and keep the port. */
            (void)kill(-st->child, SIGKILL);
        }
    }
    if (!st->reaped || !st->eof) return;
    if (st->partial_n && !st->overflow) {
        st->partial[st->partial_n] = '\0'; consume_line(st, st->partial);
    }
    if (st->overflow) st->metadata_bad = st->batch_failed = 1;
    close(st->child_fd); close(st->input_fd);
    st->child_fd = st->input_fd = -1; st->child = 0; st->stop_at = 0;
    if (st->loading) {
        st->loading = 0;
        st->metadata_ok = WIFEXITED(st->exit_status) && !WEXITSTATUS(st->exit_status) &&
                          st->nsuite && st->nsuite + st->nleaf <= TIKU_LIST_MAX && !st->metadata_bad;
        snprintf(st->note, sizeof st->note, st->metadata_ok ?
                 "Registry loaded: %d suites, %d sub-tests. Select tests, then Run selected." :
                 "Metadata failed/incomplete. Check TIKU_BENCH and Python dependencies. (%d suites, %d tests)",
                 st->nsuite, st->nleaf);
        profile_update(st); entries_build(st); st->dirty = 1;
        if (*st->log_path) snprintf(st->note, sizeof st->note,
            "%s | history refreshed | Log: %.400s", st->cancelled ? "Stopped" :
            st->batch_failed ? "Finished with failures" : st->no_tests ? "No tests ran (skipped)" : "Finished", st->log_path);
    } else job_finish(st);
}

static void stop_run(bench_t *st)
{
    st->cancelled = 1;
    if (st->child > 0) { kill(-st->child, SIGINT); st->stop_at = st->now + 2000000; }
    else if (st->released && !st->reclaiming) release_finish(st);
    st->dirty = 1;
}

/** The user has confirmed the captured target, mode and selected queue. */
static void run_confirmed(bench_t *st)
{
    char port[256] = "", mcu[64] = "";
    int needs_device = queue_needs_device(st);
    if (needs_device && st->services->board_port)
        st->services->board_port(st->services->ctx, st->target_name, port, sizeof port);
    if (needs_device && st->services->board_mcu)
        st->services->board_mcu(st->services->ctx, st->target_name, mcu, sizeof mcu);
    for (char *p = mcu; *p; p++) *p = (char)tolower((unsigned char)*p);
    if (needs_device && (strcmp(port, st->port) || strcmp(mcu, st->mcu) || !*mcu)) {
        snprintf(st->note, sizeof st->note, "Device changed during confirmation; select and review it again.");
        st->dirty = 1; return;
    }
    st->busy = 1; st->cancelled = st->batch_failed = 0;
    st->waiting = st->released = st->reclaiming = 0;
    st->nlog = st->log_head = 0; plan_build(st);
    char pattern[] = "/tmp/tikubench-desktop-XXXXXX";
    int fd = mkstemp(pattern);
    if (fd >= 0) { st->log = fdopen(fd, "w"); if (!st->log) close(fd); }
    snprintf(st->log_path, sizeof st->log_path, "%s", fd >= 0 ? pattern : "unavailable");
    if (!st->log) { st->busy = 0; snprintf(st->note, sizeof st->note, "Cannot create a private run log"); return; }
    tiku_tabs_select(&st->tabs, TAB_RESULTS);
    if (!needs_device) { run_next(st); st->dirty = 1; return; }
    st->waiting = 1; st->deadline = st->now + 10000000;
    if (!st->services->board_release || !st->services->board_reclaim ||
        !st->services->board_release(st->services->ctx, st->target_name)) {
        st->waiting = 0; st->batch_failed = 1; release_finish(st);
    }
    st->dirty = 1;
}
