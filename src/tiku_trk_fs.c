/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_fs.c - file operations, stepped rather than blocking.
 *
 * An operation is a queue plus a cursor: tiku_trk_fs_step() does a little and
 * returns, so the window keeps drawing and a collision can stop the queue and
 * wait for an answer instead of guessing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_fs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FS_QUEUE_MAX 512

/** @brief One reversal record: how to put a single item back. */
typedef struct {
    char from[TIKU_TRK_PATH_MAX];   /* where it ended up */
    char to[TIKU_TRK_PATH_MAX];     /* where it came from, "" if created */
    int  created;                   /* undo removes rather than moves    */
} undo_rec_t;

struct tiku_trk_fs {
    tiku_trk_backend_t *backend;
    char                queue[FS_QUEUE_MAX][TIKU_TRK_PATH_MAX];
    int                 qcount;
    int                 qcursor;
    char                dst[TIKU_TRK_PATH_MAX];
    tiku_trk_conflict_t policy;
    tiku_trk_progress_t prog;

    undo_rec_t         *undo;
    int                 undo_count;
    int                 undo_cap;
    tiku_trk_op_t       undo_op;
};

tiku_trk_fs_t *
tiku_trk_fs_new(tiku_trk_backend_t *backend)
{
    tiku_trk_fs_t *fs = calloc(1, sizeof *fs);

    if (fs != NULL) {
        fs->backend = backend;
    }
    return fs;
}

void
tiku_trk_fs_free(tiku_trk_fs_t *fs)
{
    if (fs != NULL) {
        free(fs->undo);
        free(fs);
    }
}

int
tiku_trk_fs_busy(const tiku_trk_fs_t *fs)
{
    return (fs != NULL && fs->prog.op != TIKU_TRK_OP_IDLE &&
            fs->qcursor < fs->qcount);
}

const tiku_trk_progress_t *
tiku_trk_fs_progress(const tiku_trk_fs_t *fs)
{
    return (fs != NULL) ? &fs->prog : NULL;
}

/*---------------------------------------------------------------------------*/
/* names                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief The one place the illegal set is written down. */
#define NAME_DENY "/"

const char *
tiku_trk_fs_name_deny(void)
{
    return NAME_DENY;
}

int
tiku_trk_fs_name_ok(const char *name, char *err, size_t errmax)
{
    if (name == NULL || name[0] == '\0') {
        if (err != NULL) { snprintf(err, errmax, "a name cannot be empty"); }
        return 0;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        if (err != NULL) { snprintf(err, errmax, "'%s' is not a usable name",
                                    name); }
        return 0;
    }
    if (strpbrk(name, NAME_DENY) != NULL) {
        if (err != NULL) {
            snprintf(err, errmax, "a name cannot contain '/'");
        }
        return 0;
    }
    return 1;
}

/** @brief Join a directory and a leaf name; -1 rather than truncate. */
static int
join(char *out, size_t max, const char *dir, const char *name)
{
    size_t dl = strlen(dir), nl = strlen(name);
    int root = (dir[0] == '/' && dir[1] == '\0');

    if (dl + nl + 2u > max) {
        return -1;
    }
    if (root) {
        out[0] = '/';
        memcpy(out + 1, name, nl + 1u);
    } else {
        memcpy(out, dir, dl);
        out[dl] = '/';
        memcpy(out + dl + 1u, name, nl + 1u);
    }
    return 0;
}

/** @brief Does an entry exist at dir/name? */
static int
exists(tiku_trk_fs_t *fs, const char *dir, const char *name)
{
    char path[TIKU_TRK_PATH_MAX];
    tiku_trk_model_t m;

    if (join(path, sizeof path, dir, name) != 0) {
        return 1;                    /* unusable: treat as taken */
    }
    return (fs->backend->ops->stat(fs->backend, path, &m) == 0);
}

/** @brief The directory part of a path, into @p out. */
static void
parent_of(const char *path, char *out, size_t max)
{
    const char *slash = strrchr(path, '/');
    size_t n;

    if (slash == NULL || slash == path) {
        snprintf(out, max, "/");
        return;
    }
    n = (size_t)(slash - path);
    if (n >= max) { n = max - 1u; }
    memcpy(out, path, n);
    out[n] = '\0';
}

static const char *
leaf_of(const char *path)
{
    const char *slash = strrchr(path, '/');

    return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

/**
 * @brief Split "<base> copy N" into its base and the number.
 *
 * This is what stops "x copy" duplicating into "x copy copy": the suffix is
 * recognised and the counter advanced instead (FS-006).
 */
static int
split_suffix(const char *name, const char *word, char *base, size_t max)
{
    size_t nl = strlen(name), wl = strlen(word);
    const char *p;
    int n = 0;

    snprintf(base, max, "%s", name);
    if (nl <= wl + 1u) {
        return 0;
    }
    /* "<base> <word>" or "<base> <word> <n>" */
    p = name + nl;
    while (p > name && p[-1] >= '0' && p[-1] <= '9') {
        p--;
    }
    if (p < name + nl) {
        n = atoi(p);
        if (p > name && p[-1] == ' ') {
            p--;
        }
    }
    if ((size_t)(p - name) > wl && strncmp(p - wl, word, wl) == 0 &&
        p - wl - 1 >= name && p[-wl - 1] == ' ') {
        size_t bl = (size_t)(p - wl - 1 - name);
        if (bl >= max) { bl = max - 1u; }
        memcpy(base, name, bl);
        base[bl] = '\0';
        return (n > 0) ? n : 1;
    }
    return 0;
}

/** @brief First free "<base> <word> [n]" in @p dir. */
static int
free_suffixed_name(tiku_trk_fs_t *fs, const char *dir, const char *name,
                   const char *word, int plain_first, char *out, size_t max)
{
    char base[TIKU_TRK_NAME_MAX];
    int n, i;

    n = split_suffix(name, word, base, sizeof base);
    if (plain_first && n == 0 && !exists(fs, dir, name)) {
        snprintf(out, max, "%s", name);   /* the plain name is free */
        return 0;
    }
    if (n == 0) {
        snprintf(out, max, "%s %s", base, word);
        if (!exists(fs, dir, out)) {
            return 0;
        }
        n = 1;
    }
    for (i = (n > 1) ? n : 2; i < 10000; i++) {
        snprintf(out, max, "%s %s %d", base, word, i);
        if (!exists(fs, dir, out)) {
            return 0;
        }
    }
    return -1;
}

int
tiku_trk_fs_duplicate_name(tiku_trk_fs_t *fs, const char *dir,
                           const char *name, char *out, size_t max)
{
    /* Duplicate never reuses the plain name: a duplicate is always "copy". */
    return free_suffixed_name(fs, dir, name, "copy", 0, out, max);
}

int
tiku_trk_fs_link_name(tiku_trk_fs_t *fs, const char *dir, const char *name,
                      char *out, size_t max)
{
    /* A link keeps the original name when the destination allows it. */
    return free_suffixed_name(fs, dir, name, "link", 1, out, max);
}

/*---------------------------------------------------------------------------*/
/* drop semantics                                                            */
/*---------------------------------------------------------------------------*/

/** @brief Volume id of a path, or 0 when it cannot be told. */
static unsigned long
volume_of(tiku_trk_fs_t *fs, const char *path)
{
    struct stat st;

    /* Device backends are one volume each; the backend identity IS the
     * volume, so a drop inside a device is always a move. */
    if (fs->backend->devid[0] != '\0') {
        return 1;
    }
    if (stat(path, &st) != 0) {
        char dir[TIKU_TRK_PATH_MAX];
        parent_of(path, dir, sizeof dir);
        if (stat(dir, &st) != 0) {
            return 0;
        }
    }
    return (unsigned long)st.st_dev;
}

tiku_trk_op_t
tiku_trk_fs_drop_action(tiku_trk_fs_t *fs, const char *src,
                        const char *dst_dir, int force_copy)
{
    unsigned long a, b;

    if (force_copy) {
        return TIKU_TRK_OP_COPY;
    }
    a = volume_of(fs, src);
    b = volume_of(fs, dst_dir);
    /* Same volume: a move is a rename and costs nothing.  Different volume:
     * the bytes have to travel, so it is a copy and the original stays. */
    return (a != 0 && a == b) ? TIKU_TRK_OP_MOVE : TIKU_TRK_OP_COPY;
}

/*---------------------------------------------------------------------------*/
/* the queue                                                                 */
/*---------------------------------------------------------------------------*/

static void
undo_reset(tiku_trk_fs_t *fs, tiku_trk_op_t op)
{
    fs->undo_count = 0;
    fs->undo_op = op;
    if (fs->undo == NULL) {
        fs->undo_cap = FS_QUEUE_MAX;
        fs->undo = calloc((size_t)fs->undo_cap, sizeof *fs->undo);
    }
}

static void
undo_push(tiku_trk_fs_t *fs, const char *from, const char *to, int created)
{
    undo_rec_t *r;

    if (fs->undo == NULL || fs->undo_count >= fs->undo_cap) {
        return;
    }
    r = &fs->undo[fs->undo_count++];
    snprintf(r->from, sizeof r->from, "%s", from);
    snprintf(r->to, sizeof r->to, "%s", to ? to : "");
    r->created = created;
}

/**
 * @brief Whether the Trash lives underneath @p path.
 *
 * A prefix test, not a substring one: "/home/u" contains "/home/u/.Trash",
 * and a plain strstr would also match a sibling called "/home/user-.Trash".
 */
static int
contains_trash(tiku_trk_fs_t *fs, const char *path)
{
    char probe[TIKU_TRK_PATH_MAX];
    size_t n;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    n = strlen(path);
    while (n > 1u && path[n - 1u] == '/') {
        n--;                            /* a trailing slash is not a level */
    }
    if (n + sizeof "/.Trash" > sizeof probe) {
        return 0;
    }
    memcpy(probe, path, n);
    probe[n] = '\0';
    return exists(fs, probe, ".Trash");
}

static int
start(tiku_trk_fs_t *fs, tiku_trk_op_t op, const char *const *paths, int n,
      const char *dst, tiku_trk_conflict_t policy)
{
    int i;

    if (fs == NULL || tiku_trk_fs_busy(fs)) {
        return -1;
    }
    if (n > FS_QUEUE_MAX) {
        n = FS_QUEUE_MAX;
    }
    fs->qcount = 0;
    for (i = 0; i < n; i++) {
        /* The Trash is dropped from any destructive selection, so it can
         * never be trashed or deleted -- and neither can anything that
         * CONTAINS it, since taking the parent takes the Trash with it
         * (FS-043).  Both tests are silent: the row says so. */
        if (op == TIKU_TRK_OP_TRASH &&
            (strstr(paths[i], "/.Trash") != NULL ||
             contains_trash(fs, paths[i]))) {
            /* Scoped to the destructive op.  Applied to every queue it also
             * dropped a plain COPY of any folder that happens to hold a
             * Trash -- copying a home directory would quietly do nothing. */
            continue;
        }
        snprintf(fs->queue[fs->qcount++], TIKU_TRK_PATH_MAX, "%s", paths[i]);
    }
    fs->qcursor = 0;
    fs->policy = policy;
    snprintf(fs->dst, sizeof fs->dst, "%s", dst ? dst : "");
    memset(&fs->prog, 0, sizeof fs->prog);
    fs->prog.op = op;
    fs->prog.total = fs->qcount;
    undo_reset(fs, op);
    return 0;
}

int
tiku_trk_fs_copy(tiku_trk_fs_t *fs, const char *const *paths, int n,
                 const char *dst_dir, tiku_trk_conflict_t policy)
{
    return start(fs, TIKU_TRK_OP_COPY, paths, n, dst_dir, policy);
}

int
tiku_trk_fs_move(tiku_trk_fs_t *fs, const char *const *paths, int n,
                 const char *dst_dir, tiku_trk_conflict_t policy)
{
    return start(fs, TIKU_TRK_OP_MOVE, paths, n, dst_dir, policy);
}

int
tiku_trk_fs_duplicate(tiku_trk_fs_t *fs, const char *const *paths, int n)
{
    return start(fs, TIKU_TRK_OP_DUPLICATE, paths, n, NULL,
                 TIKU_TRK_CONFLICT_RENAME);
}

int
tiku_trk_fs_trash(tiku_trk_fs_t *fs, const char *const *paths, int n)
{
    return start(fs, TIKU_TRK_OP_TRASH, paths, n, NULL,
                 TIKU_TRK_CONFLICT_SKIP);
}

/*---------------------------------------------------------------------------*/
/* the work                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Copy one file's bytes through the backend. */
static int
copy_bytes(tiku_trk_fs_t *fs, const char *src, const char *dst, char *err,
           size_t errmax)
{
    static unsigned char buf[64 * 1024];
    FILE *in, *out;
    size_t n;

    /* Local files copy directly; a device file goes through the backend so
     * its own transfer discipline applies. */
    if (fs->backend->devid[0] != '\0') {
        int len = fs->backend->ops->read(fs->backend, src, buf, sizeof buf);
        if (len < 0) {
            snprintf(err, errmax, "cannot read '%s'", src);
            return -1;
        }
        return fs->backend->ops->write(fs->backend, dst, buf, (size_t)len,
                                       err, errmax);
    }
    in = fopen(src, "rb");
    if (in == NULL) {
        snprintf(err, errmax, "cannot read '%s' (%s)", src, strerror(errno));
        return -1;
    }
    out = fopen(dst, "wb");
    if (out == NULL) {
        (void)fclose(in);
        snprintf(err, errmax, "cannot create '%s' (%s)", dst,
                 strerror(errno));
        return -1;
    }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            (void)fclose(in);
            (void)fclose(out);
            snprintf(err, errmax, "write failed on '%s'", dst);
            return -1;
        }
    }
    (void)fclose(in);
    return (fclose(out) == 0) ? 0 : -1;
}

/** @brief Process one queued item.  @return 1 done, 0 stopped on a conflict. */
static int
step_one(tiku_trk_fs_t *fs)
{
    const char *src = fs->queue[fs->qcursor];
    char dstdir[TIKU_TRK_PATH_MAX], dstpath[TIKU_TRK_PATH_MAX];
    char name[TIKU_TRK_NAME_MAX];
    char err[256];

    snprintf(fs->prog.current, sizeof fs->prog.current, "%s", src);
    err[0] = '\0';

    if (fs->prog.op == TIKU_TRK_OP_TRASH ||
        fs->prog.op == TIKU_TRK_OP_DELETE) {
        if (fs->backend->devid[0] != '\0') {
            /* A device store has rm and no trash; deletion is final and the
             * UI must have said so before we got here. */
            char cmd[TIKU_TRK_PATH_MAX];
            snprintf(cmd, sizeof cmd, "%s", src);
            if (unlink(cmd) != 0) {
                fs->prog.failed++;
            } else {
                fs->prog.done++;
            }
        } else if (remove(src) != 0) {
            /* The message is for a human in a dialog: show the leaf name,
             * which is what they selected, not the whole path. */
            snprintf(fs->prog.error, sizeof fs->prog.error,
                     "cannot remove '%s' (%s)", leaf_of(src),
                     strerror(errno));
            fs->prog.failed++;
        } else {
            fs->prog.done++;
        }
        fs->qcursor++;
        return 1;
    }

    if (fs->prog.op == TIKU_TRK_OP_DUPLICATE) {
        /* Each item duplicates into its OWN parent, even when the selection
         * spans folders (FS-008). */
        parent_of(src, dstdir, sizeof dstdir);
        if (tiku_trk_fs_duplicate_name(fs, dstdir, leaf_of(src), name,
                                       sizeof name) != 0) {
            fs->prog.failed++;
            fs->qcursor++;
            return 1;
        }
    } else {
        snprintf(dstdir, sizeof dstdir, "%s", fs->dst);
        snprintf(name, sizeof name, "%s", leaf_of(src));
        {   /* An item already in the destination is silently skipped, not
             * copied onto itself (FS-005). */
            char srcdir[TIKU_TRK_PATH_MAX];
            parent_of(src, srcdir, sizeof srcdir);
            if (strcmp(srcdir, dstdir) == 0) {
                fs->prog.skipped++;
                fs->qcursor++;
                return 1;
            }
        }
    }
    if (join(dstpath, sizeof dstpath, dstdir, name) != 0) {
        fs->prog.failed++;
        fs->qcursor++;
        return 1;
    }

    /* Collisions: ask, unless the caller already chose a policy. */
    if (fs->prog.op != TIKU_TRK_OP_DUPLICATE && exists(fs, dstdir, name)) {
        switch (fs->policy) {
        case TIKU_TRK_CONFLICT_SKIP:
            fs->prog.skipped++;
            fs->qcursor++;
            return 1;
        case TIKU_TRK_CONFLICT_RENAME:
            if (tiku_trk_fs_duplicate_name(fs, dstdir, name, name,
                                           sizeof name) != 0 ||
                join(dstpath, sizeof dstpath, dstdir, name) != 0) {
                fs->prog.failed++;
                fs->qcursor++;
                return 1;
            }
            break;
        case TIKU_TRK_CONFLICT_REPLACE:
            break;                  /* fall through and overwrite */
        default:
            fs->prog.conflict = 1;
            snprintf(fs->prog.conflict_path, sizeof fs->prog.conflict_path,
                     "%s", dstpath);
            return 0;               /* stop and wait for an answer */
        }
    }

    if (fs->prog.op == TIKU_TRK_OP_MOVE) {
        if (rename(src, dstpath) != 0) {
            /* Across volumes rename cannot work; copy then remove. */
            if (copy_bytes(fs, src, dstpath, err, sizeof err) != 0 ||
                remove(src) != 0) {
                snprintf(fs->prog.error, sizeof fs->prog.error, "%s",
                         err[0] ? err : "move failed");
                fs->prog.failed++;
                fs->qcursor++;
                return 1;
            }
        }
        undo_push(fs, dstpath, src, 0);
        fs->prog.done++;
    } else {
        if (copy_bytes(fs, src, dstpath, err, sizeof err) != 0) {
            snprintf(fs->prog.error, sizeof fs->prog.error, "%s", err);
            fs->prog.failed++;
        } else {
            undo_push(fs, dstpath, NULL, 1);
            fs->prog.done++;
        }
    }
    fs->qcursor++;
    return 1;
}

int
tiku_trk_fs_step(tiku_trk_fs_t *fs, int budget)
{
    int n = 0;

    if (fs == NULL || fs->prog.conflict) {
        return 0;
    }
    while (n < budget && fs->qcursor < fs->qcount) {
        if (!step_one(fs)) {
            break;                  /* stopped on a conflict */
        }
        n++;
    }
    if (fs->qcursor >= fs->qcount && !fs->prog.conflict) {
        fs->prog.op = (fs->prog.total > 0) ? fs->prog.op : TIKU_TRK_OP_IDLE;
    }
    return n;
}

void
tiku_trk_fs_resolve(tiku_trk_fs_t *fs, tiku_trk_conflict_t how)
{
    if (fs == NULL || !fs->prog.conflict) {
        return;
    }
    fs->prog.conflict = 0;
    fs->policy = how;
    if (how == TIKU_TRK_CONFLICT_SKIP) {
        fs->prog.skipped++;
        fs->qcursor++;
        fs->policy = TIKU_TRK_CONFLICT_ASK;   /* ask again next time */
    }
}

void
tiku_trk_fs_cancel(tiku_trk_fs_t *fs)
{
    if (fs != NULL) {
        /* What is already done stays done: a half-finished copy is real. */
        fs->qcursor = fs->qcount;
        fs->prog.conflict = 0;
    }
}

int
tiku_trk_fs_rename(tiku_trk_fs_t *fs, const char *path, const char *new_name,
                   char *err, size_t errmax)
{
    char dir[TIKU_TRK_PATH_MAX], dst[TIKU_TRK_PATH_MAX];

    if (fs == NULL || path == NULL || new_name == NULL) {
        return -1;
    }
    if (!tiku_trk_fs_name_ok(new_name, err, errmax)) {
        return -1;
    }
    parent_of(path, dir, sizeof dir);
    if (exists(fs, dir, new_name)) {
        if (err != NULL) {
            snprintf(err, errmax, "'%s' already exists here", new_name);
        }
        return -1;
    }
    if (join(dst, sizeof dst, dir, new_name) != 0) {
        return -1;
    }
    if (rename(path, dst) != 0) {
        if (err != NULL) {
            snprintf(err, errmax, "cannot rename (%s)", strerror(errno));
        }
        return -1;
    }
    undo_reset(fs, TIKU_TRK_OP_RENAME);
    undo_push(fs, dst, path, 0);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* undo                                                                      */
/*---------------------------------------------------------------------------*/

int
tiku_trk_fs_can_undo(const tiku_trk_fs_t *fs, char *what, size_t max)
{
    static const char *names[] = {
        "", "Copy", "Move", "Duplicate", "Move to Trash", "Delete",
        "Rename", "Create Link"
    };

    if (fs == NULL || fs->undo_count == 0) {
        return 0;
    }
    /* A delete is gone; saying "Undo Delete" and failing would be worse
     * than not offering it. */
    if (fs->undo_op == TIKU_TRK_OP_DELETE) {
        return 0;
    }
    if (what != NULL && (size_t)fs->undo_op < sizeof names / sizeof names[0]) {
        snprintf(what, max, "%s", names[fs->undo_op]);
    }
    return 1;
}

int
tiku_trk_fs_undo(tiku_trk_fs_t *fs, char *err, size_t errmax)
{
    int i, n = 0;

    if (!tiku_trk_fs_can_undo(fs, NULL, 0)) {
        return -1;
    }
    /* Reverse order, so a sequence that renamed around a collision unwinds
     * without colliding with itself. */
    for (i = fs->undo_count - 1; i >= 0; i--) {
        undo_rec_t *r = &fs->undo[i];

        if (r->created) {
            if (remove(r->from) == 0) { n++; }
        } else if (rename(r->from, r->to) == 0) {
            n++;
        } else if (err != NULL) {
            snprintf(err, errmax, "cannot undo '%s' (%s)", r->from,
                     strerror(errno));
        }
    }
    fs->undo_count = 0;
    return n;
}
