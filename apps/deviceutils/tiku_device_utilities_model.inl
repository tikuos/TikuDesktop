/* Parsing and edit policy are independent of windows and the transport. */

/** Accept a complete unsigned scalar, never partial, signed or overflowing input. */
static int utility_number(const char *text, double *value)
{
    char *end;
    if (!text || !isdigit((unsigned char)*text)) return 0;
    errno = 0;
    unsigned long long n = strtoull(text, &end, 10);
    while (*end == '\r' || *end == '\n') end++;
    if (errno || *end || n > 9007199254740991ULL) return 0;
    *value = (double)n; return 1;
}

/** Stay in an absolute device namespace; reject traversal and control bytes. */
static int utility_path(const char *path)
{
    if (!path || path[0] != '/' || strlen(path) >= 256) return 0;
    const char *start = path + 1;
    for (const char *s = start; ; s++) {
        if (*s && ((unsigned char)*s < 32 || (unsigned char)*s == 127)) return 0;
        if (!*s || *s == '/') {
            size_t n = (size_t)(s - start);
            if ((n == 1 && start[0] == '.') ||
                (n == 2 && !strncmp(start, "..", 2))) return 0;
            if (!*s) break;
            if (!n) return 0;
            start = s + 1;
        }
    }
    return 1;
}

/** Only audited configuration endpoints get an editor, not arbitrary commands. */
static int utility_editable(const char *path)
{
    return !strcmp(path, "/sys/device/name") ||
        !strcmp(path, "/sys/cpu/freq_target") ||
        !strcmp(path, "/sys/crypto/mode") ||
        !strcmp(path, "/sys/radio/txpower") || !strcmp(path, "/sys/time");
}

/** Explain semantics without guessing at unknown endpoint units or side effects. */
static const char *utility_help(const char *path)
{
    if (!strcmp(path, "/sys/device/name"))
        return "Persistent device name. Reconnect to update desktop naming.";
    if (!strcmp(path, "/sys/cpu/freq_target"))
        return "Clock target in Hz. Checked against freq_available. May require a restart; no restart is automatic.";
    if (!strcmp(path, "/sys/crypto/mode"))
        return "auto uses available hardware; sw forces software. Resets at reboot.";
    if (!strcmp(path, "/sys/radio/txpower"))
        return "Transmit power in dBm. Firmware validates supported levels; resets at reboot.";
    if (!strcmp(path, "/sys/time")) return "Wall clock: unsigned Unix epoch seconds.";
    if (!strcmp(path, "/sys/uptime")) return "Seconds since the device booted.";
    if (!strcmp(path, "/sys/cpu/freq")) return "Current CPU frequency in Hz; not necessarily the saved target.";
    if (!strcmp(path, "/sys/mem/used"))
        return "Process SRAM allocations in bytes; excludes kernel statics and stack.";
    if (!strcmp(path, "/sys/mem/stack_free")) return "Measured stack headroom in bytes.";
    if (!strcmp(path, "/sys/mem/nvm_map"))
        return "NVM regions. Store used includes metadata and allocated slot slack, not just file payload.";
    if (!strcmp(path, "/sys/mem/sram_map")) return "SRAM banks, static data, stack pool and tier arena.";
    if (!strncmp(path, "/proc", 5)) return "Process statistics published by this firmware. Missing entries may have exited.";
    return "Device-defined endpoint. Reads can access hardware. Only audited settings can be edited here.";
}

/** Extract Store occupancy only when the firmware supplies both size and used. */
static void utility_store(const char *text, utility_metrics_t *m)
{
    const char *line = text;
    while (*line) {
        if (!strncmp(line, "store\t", 6)) {
            unsigned long long size, used; char extra;
            char row[128]; size_t n = strcspn(line, "\r\n");
            if (n >= sizeof row) return;
            memcpy(row, line, n); row[n] = 0;
            if (sscanf(row, "store\t%llu\t%llu %c", &size, &used, &extra) == 2 &&
                size && used <= size && size <= 9007199254740991ULL) {
                m->store_size = (double)size; m->store_used = (double)used;
                m->valid |= 32;
            }
            return;
        }
        line += strcspn(line, "\n"); if (*line) line++;
    }
}
