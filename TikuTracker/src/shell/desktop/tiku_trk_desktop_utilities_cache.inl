/* Device-local directory snapshots; never authorize writes from cached data. */

/** Release both completed and partially acquired directory snapshots. */
static void utility_cache_free(utility_snapshot_t *s)
{
    while (s) { utility_snapshot_t *next = s->next; free(s); s = next; }
}

/** Match an exact directory, never a prefix from another branch. */
static utility_snapshot_t *utility_cache_find(utility_snapshot_t *s, const char *path)
{
    for (; s; s = s->next) if (!strcmp(s->path, path)) return s;
    return NULL;
}

/** Cache a refreshed directory, bounded independently of the visible tree. */
static void utility_cache_store(struct desktop_utility *p, utility_job_t *j)
{
    utility_snapshot_t *s = utility_cache_find(p->cache, j->path);
    if (!s) {
        int count = 0;
        for (s = p->cache; s; s = s->next) count++;
        if (count >= U_ROWS) return;
        s = calloc(1, sizeof *s); if (!s) return;
        s->next = p->cache; p->cache = s;
    }
    snprintf(s->path, sizeof s->path, "%s", j->path);
    s->ok = j->ok; s->count = j->count; s->truncated = j->truncated;
    memcpy(s->entries, j->entries, sizeof s->entries);
}

/** Audited observational endpoints only: no devices, event drains or commands. */
static int utility_autoread(const char *path)
{
    static const char *const safe[] = {
        "/sys/version", "/sys/uptime", "/sys/time", "/sys/boot_count",
        "/sys/last_reset", "/sys/cold_boots", "/sys/device/name",
        "/sys/device/id", "/sys/device/uid", "/sys/device/mcu",
        "/sys/device/version", "/sys/cpu/freq", "/sys/cpu/freq_available",
        "/sys/cpu/freq_target", "/sys/cpu/freq_change_mode",
        "/sys/mem/sram", "/sys/mem/nvm", "/sys/mem/kind", "/sys/mem/free",
        "/sys/mem/used", "/sys/mem/nvmfree", "/sys/mem/tiers",
        "/sys/mem/failed", "/sys/mem/stack_free", "/sys/mem/sram_map",
        "/sys/mem/nvm_map", "/sys/sched/idle", "/sys/crypto/mode",
        "/sys/crypto/ops", "/sys/crypto/pk", "/proc/count"
    };
    for (size_t i = 0; i < sizeof safe / sizeof *safe; i++)
        if (!strcmp(path, safe[i])) return 1;
    if (strncmp(path, "/proc/", 6)) return 0;
    const char *s = path + 6;
    if (!isdigit((unsigned char)*s)) return 0;
    while (isdigit((unsigned char)*s)) s++;
    return !strcmp(s, "/name") || !strcmp(s, "/state") ||
           !strcmp(s, "/sram_used") || !strcmp(s, "/events");
}
