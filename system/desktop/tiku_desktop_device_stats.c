/* tiku_desktop_device_stats.c -- device stats.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_desktop_jobs_private.h"

/* How long an answer stands before the board is asked again.  The About
 * box reads it while it is open, and a board's own numbers move slowly. */
#define STATS_HOLD_US 4000000

typedef struct {
    desktop_t      *app;
    char            name[64];
    tiku_backend_t *dev;
    char            text[1024];
} sread_t;

/** @brief One node of the board, trimmed, or "" when it has none. */
static void
stat_read(tiku_backend_t *dev, const char *path, char *out, size_t max)
{
    int n = dev->ops->read(dev, path, out, max - 1u);
    char *nl;

    if (n < 0) {
        out[0] = '\0';
        return;
    }
    out[n] = '\0';
    nl = strchr(out, '\n');
    if (nl != NULL) {
        *nl = '\0';
    }
}

/* What the board is asked for, in the order the facts are shown. */
enum { SR_MCU, SR_FREQ, SR_SRAM, SR_USED, SR_NVM, SR_NVMFREE, SR_KIND,
       SR_UP, SR_BOOTS, SR_N };

/* The corners of a board's namespace that are there only when the part
 * has the thing: what it HAS, asked by whether it can be seen. */
static const struct { const char *path; const char *say; } sr_has[] = {
    { "/sys/coproc", "coprocessor" },
    { "/sys/npu",    "NPU"         },
    { "/sys/gpu",    "GPU"         },
    { "/sys/flpr",   "FLPR"        },
    { "/sys/crypto", "crypto"      },
    { "/sys/flash",  "NOR flash"   },
    { "/sys/emmc",   "eMMC"        },
};

/** @brief Whether the board has this corner of its namespace. */
static int
have_node(tiku_backend_t *dev, const char *path)
{
    tiku_model_t m;

    return dev->ops->stat(dev, path, &m) == 0;
}

/** @brief A whole node, newlines and all, or "" when it has none. */
static void
node_read(tiku_backend_t *dev, const char *path, char *out, size_t max)
{
    int n = dev->ops->read(dev, path, out, max - 1u);

    out[(n > 0) ? n : 0] = '\0';
}

/**
 * @brief One tier's capacity and use out of /sys/mem/tiers.
 *
 * Matched at the START of a line: "psram" ends in the same four letters
 * as "sram", and a loose search would read one tier's numbers for the
 * other's.
 */
static int
tier_room(const char *tiers, const char *name, unsigned long *total,
          unsigned long *used)
{
    size_t len = strlen(name);
    const char *p = tiers;

    while (p != NULL && *p != '\0') {
        if (strncmp(p, name, len) == 0 && p[len] == ' ') {
            const char *t = strstr(p, "total=");
            const char *u = strstr(p, "used=");

            if (t == NULL || u == NULL) {
                return 0;
            }
            *total = strtoul(t + 6, NULL, 10);
            *used = strtoul(u + 5, NULL, 10);
            return 1;
        }
        p = strchr(p, '\n');
        p = (p != NULL) ? p + 1 : NULL;
    }
    return 0;
}

/**
 * @brief Forward one memory's map, a line at a time, under @p tag.
 *
 * The board says its estates (code, module, tier, store, persist;
 * static, stack, tier, banks) and this end passes them on untouched:
 * how they are drawn is the reader's business.  @return 1 when the
 * board had a map to give.
 */
static int
say_map(sread_t *s, size_t *at, const char *tag, const char *path)
{
    char map[512];
    const char *p;

    node_read(s->dev, path, map, sizeof map);
    if (map[0] == '\0') {
        return 0;
    }
    for (p = map; *p != '\0' && *at < sizeof s->text; ) {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        if (len > 0u) {
            *at += (size_t)snprintf(s->text + *at, sizeof s->text - *at,
                                    "%s\t%.*s\n", tag, (int)len, p);
        }
        p = (nl != NULL) ? nl + 1 : p + strlen(p);
    }
    return 1;
}

/**
 * @brief Say what the board is and what it holds, as raw facts.
 *
 * The memories come as MAPS -- each estate of each memory with its size
 * and use -- from /sys/mem/sram_map and /sys/mem/nvm_map.  A board too
 * old to carry those answers with its tiers instead, which is less but
 * is at least each tier's own free against its own total.
 */
static void
sread_say(sread_t *s, char raw[][32], const char *tiers)
{
    static const struct { const char *tier; const char *say; } kinds[] = {
        { "sram",   "SRAM"   },
        { "nvm",    NULL     },      /* the device names this one */
        { "hifram", "HIFRAM" },
        { "psram",  "PSRAM"  },
    };
    unsigned long total, used;
    size_t at;
    int i, mapped;

    at = (size_t)snprintf(s->text, sizeof s->text,
             "mcu\t%s\nfreq\t%s\nup\t%s\nboots\t%s\npart\t%s\t%s\t%s\n",
             raw[SR_MCU][0] ? raw[SR_MCU] : "?", raw[SR_FREQ],
             raw[SR_UP], raw[SR_BOOTS], raw[SR_SRAM], raw[SR_NVM],
             raw[SR_KIND][0] ? raw[SR_KIND] : "NVM");
    mapped  = say_map(s, &at, "sram", "/sys/mem/sram_map");
    mapped |= say_map(s, &at, "nvm", "/sys/mem/nvm_map");
    for (i = 0; !mapped && i < (int)(sizeof kinds / sizeof kinds[0]); i++) {
        if (at >= sizeof s->text ||
            !tier_room(tiers, kinds[i].tier, &total, &used)) {
            continue;
        }
        at += (size_t)snprintf(s->text + at, sizeof s->text - at,
                               "mem\t%s\t%lu\t%lu\n",
                               (kinds[i].say != NULL) ? kinds[i].say
                                   : (raw[SR_KIND][0] ? raw[SR_KIND]
                                                      : "NVM"),
                               (total > used) ? total - used : 0UL,
                               total);
    }
    for (i = 0; i < (int)(sizeof sr_has / sizeof sr_has[0]); i++) {
        if (at < sizeof s->text && have_node(s->dev, sr_has[i].path)) {
            at += (size_t)snprintf(s->text + at, sizeof s->text - at,
                                   "has\t%s\n", sr_has[i].say);
        }
    }
}

/** @brief On a worker: ask the board what it is and what it has left. */
static void
sread_run(void *ctx)
{
    static const char *const node[SR_N] = {
        "/sys/device/mcu", "/sys/cpu/freq", "/sys/mem/sram",
        "/sys/mem/used",   "/sys/mem/nvm",  "/sys/mem/nvmfree",
        "/sys/mem/kind",   "/sys/uptime",   "/sys/boot_count"
    };
    sread_t *s = ctx;
    char raw[SR_N][32];
    char tiers[256];
    int i;

    for (i = 0; i < SR_N; i++) {
        stat_read(s->dev, node[i], raw[i], sizeof raw[i]);
    }
    node_read(s->dev, "/sys/mem/tiers", tiers, sizeof tiers);
    sread_say(s, raw, tiers);
}

/** @brief On the loop: keep what it said. */
static void
sread_done(void *ctx)
{
    sread_t *s = ctx;
    int i;

    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (strcmp(s->app->dev_stats[i].name, s->name) == 0) {
            snprintf(s->app->dev_stats[i].text,
                     sizeof s->app->dev_stats[i].text, "%s", s->text);
            s->app->dev_stats[i].at = tiku_trk_now();
            desktop_damage_all(s->app);
            return;
        }
    }
}

/** @brief The asking is over. */
static void
sread_free(void *ctx)
{
    sread_t *s = ctx;
    int i;

    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (strcmp(s->app->dev_stats[i].name, s->name) == 0) {
            s->app->dev_stats[i].asking = 0;
        }
    }
    free(s);
}

/**
 * @brief What @p board last said about itself, into @p out.
 *
 * Answered from what is KEPT, never from the wire: a stale answer sets a
 * worker going and the caller draws what it has until that lands.  So a
 * window showing this is never the reason a frame is late.
 *
 * @return the bytes written.
 */
int
desktop_device_stats(desktop_t *app, const char *board, char *out,
                     size_t max)
{
    int i, at = -1;

    if (app == NULL || board == NULL || board[0] == '\0' || out == NULL ||
        max == 0u) {
        return 0;
    }
    out[0] = '\0';
    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (strcmp(app->dev_stats[i].name, board) == 0) {
            at = i;
            break;
        }
        if (at < 0 && app->dev_stats[i].name[0] == '\0') {
            at = i;             /* a free slot, if none names it */
        }
    }
    if (at < 0) {
        return 0;
    }
    if (app->dev_stats[at].name[0] == '\0') {
        snprintf(app->dev_stats[at].name, sizeof app->dev_stats[at].name,
                 "%s", board);
        app->dev_stats[at].text[0] = '\0';
        app->dev_stats[at].at = 0;
    }
    snprintf(out, max, "%s", app->dev_stats[at].text);
    if (!app->dev_stats[at].asking &&
        tiku_trk_now() - app->dev_stats[at].at > STATS_HOLD_US) {
        char prefix[TIKU_PATH_MAX], local[TIKU_PATH_MAX];
        tiku_backend_t *dev;

        snprintf(prefix, sizeof prefix, "/devices/%s/sys", board);
        dev = tiku_backend_serving(app->backend, prefix, local,
                                   sizeof local);
        if (dev != NULL) {
            tiku_work_job_t job;
            sread_t *s = calloc(1, sizeof *s);

            if (s != NULL) {
                s->app = app;
                s->dev = dev;
                snprintf(s->name, sizeof s->name, "%s", board);
                memset(&job, 0, sizeof job);
                job.run = sread_run;
                job.done = sread_done;
                job.free = sread_free;
                job.ctx = s;
                snprintf(job.key, sizeof job.key, "stats:%.40s", board);
                if (tiku_work_post(&job) == 0) {
                    app->dev_stats[at].asking = 1;
                } else {
                    free(s);
                }
            }
        }
    }
    return (int)strlen(out);
}
