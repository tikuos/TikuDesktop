/* tiku_device_utilities_reclaim.inl -- part of tiku_device_utilities.c:
 * parsing the board's reports on its memory reconstruction job (the job,
 * the last job and the owners under /sys/mem/reclaim) into the titles and
 * sentences the monitor shows.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

/** @brief The value of the word "@p key=value" in @p text, into @p out.
 *  @return 1, or 0 when it is absent, empty or too long. */
static int utility_reclaim_field(const char *text, const char *key, char *out, size_t cap)
{
    size_t k = strlen(key);
    while (*text) {
        size_t n = strcspn(text, " \t\r\n");
        if (n > k && !strncmp(text, key, k) && text[k] == '=') {
            n -= k + 1;
            if (!n || n >= cap) return 0;
            memcpy(out, text + k + 1, n); out[n] = 0; return 1;
        }
        text += n; text += strspn(text, " \t\r\n");
    }
    return 0;
}
/** @brief The job's token at the start of @p text, "1:" and a nonzero
 *         32-bit number, into @p out.  @return 1, or 0. */
static int utility_reclaim_token(const char *text, char *out, size_t cap)
{
    size_t n = strcspn(text, " \t\r\n"); uint32_t v = 0;
    if (n < 3 || n >= cap || text[0] != '1' || text[1] != ':') return 0;
    for (size_t i = 2; i < n; i++) {
        unsigned d = (unsigned)(text[i] - '0');
        if (d > 9 || v > (UINT32_MAX - d) / 10u) return 0;
        v = v * 10u + d;
    }
    if (!v) return 0;
    memcpy(out, text, n); out[n] = 0; return 1;
}
/** @brief The sentence for a job's cause, or NULL for an unknown one. */
static const char *utility_reclaim_cause(const char *cause)
{
    static const char *pairs[][2] = {
        {"ok", "Request completed; owners restored."},
        {"capacity", "Not enough eligible memory."},
        {"records", "Allocation records are full."},
        {"no_layout", "No safe contiguous layout was found."},
        {"search_limit", "Layout search reached its configured limit."},
        {"refused", "An owner could not safely pause. Existing memory was retained."},
        {"timeout", "An owner did not finish within its deadline."},
        {"cancelled", "Request cancelled; any required recovery has finished."},
        {"expired", "Request expired before its memory was claimed."},
        {"owner_fault", "Recovery stopped. Memory remains protected; retry after resolving the cause."},
        {"detached", "Backing memory was detached. Retry is not supported."},
        {"disabled", "Reconstruction was disabled."},
        {"changed", "Allocations changed during planning. Request was declined."}
    };
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++)
        if (!strcmp(cause, pairs[i][0])) return pairs[i][1];
    return NULL;
}
/** @brief Parse a job report into @p r: "idle", or a token with a state
 *         and cause, and the phase of a running job; with @p last, the end
 *         of the last job.  @return 1, or 0 when it does not parse. */
static int utility_reclaim_parse(const char *text, utility_reclaim_t *r, int last)
{
    char state[24], phase[24], cause[24];
    memset(r, 0, sizeof *r);
    if (!strcmp(text, "idle")) {
        r->available = 1; strcpy(r->title, "Idle");
        strcpy(r->detail, "No reconstruction is running."); return 1;
    }
    if (!utility_reclaim_token(text, r->token, sizeof r->token) ||
        !utility_reclaim_field(text, "state", state, sizeof state) ||
        !utility_reclaim_field(text, "cause", cause, sizeof cause)) return 0;
    const char *explanation = utility_reclaim_cause(cause);
    if (!explanation) return 0;
    if (last) {
        const char *title = !strcmp(state, "ready") || !strcmp(state, "claimed") ? "Finished" :
            !strcmp(state, "cancelled") ? "Cancelled" : !strcmp(state, "failed") ? "Declined" : NULL;
        if (!title) return 0;
        snprintf(r->title, sizeof r->title, "Last request: %s", title);
        snprintf(r->detail, sizeof r->detail, "%s", explanation);
    } else {
        if (!utility_reclaim_field(text, "phase", phase, sizeof phase)) return 0;
        const char *title = !strcmp(phase, "plan") ? "Planning" :
            !strcmp(phase, "prepare") ? "Saving owner state" :
            !strcmp(phase, "commit") ? "Rebuilding allocations" :
            !strcmp(phase, "restore") ? "Restoring owner state" :
            !strcmp(phase, "abort") ? "Resuming original owners" : NULL;
        if (!title || (strcmp(state, "pending") && strcmp(state, "fault"))) return 0;
        r->active = 1; r->fault = !strcmp(state, "fault");
        r->can_cancel = !r->fault && strcmp(phase, "abort");
        r->can_retry = r->fault && strcmp(cause, "detached");
        snprintf(r->title, sizeof r->title, "%s  |  Job %s", r->fault ? "Recovery needs attention" : title, r->token);
        snprintf(r->detail, sizeof r->detail, "%s", r->fault ? explanation :
            "Cancel stops the request; recovery still finishes if rebuilding has begun.");
    }
    r->available = 1; return 1;
}
/** @brief The BASIC owner's line of the owners report, as a sentence in
 *         r->basic. */
static void utility_reclaim_basic(const char *text, utility_reclaim_t *r)
{
    static const char *pairs[][2] = {
        {"eligible", "ready"}, {"no_arena", "not using an arena"},
        {"executing_statement", "busy executing a statement"}, {"waiting", "waiting; cannot pause safely"},
        {"external_resource", "native or peripheral state cannot be restored"},
        {"debugger", "debugger active"}, {"active_timer", "active timer prevents reconstruction"},
        {"active_watch", "active watch prevents reconstruction"},
        {"large_buffer", "large buffer cannot be checkpointed"}, {"array_budget", "arrays exceed checkpoint capacity"},
        {"no_store", "last snapshot attempt found no storage"}, {"store_busy_or_full", "last snapshot attempt found storage busy or full"},
        {"save_failed", "last snapshot write failed"}, {"restore_failed", "snapshot recovery failed"},
        {"saving_source", "saving current source"}, {"saving_state", "saving runtime state"},
        {"snapshot_committed", "snapshot saved"}, {"verifying", "checking snapshot"},
        {"restoring_source", "restoring source"}, {"cleanup_failed", "resumed; temporary snapshot cleanup failed"}
    };
    while (*text) {
        size_t n = strcspn(text, "\n"); char row[256], detail[48];
        if (n < sizeof row) {
            memcpy(row, text, n); row[n] = 0;
            const char *name = strchr(row, ' ');
            if (name && !strncmp(name, " BASIC ", 7) &&
                utility_reclaim_field(row, "detail", detail, sizeof detail)) {
                for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++)
                    if (!strcmp(detail, pairs[i][0])) {
                        snprintf(r->basic, sizeof r->basic, "BASIC: %s", pairs[i][1]); return;
                    }
            }
        }
        text += n; if (*text) text++;
    }
}
