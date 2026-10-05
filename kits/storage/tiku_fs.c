/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs.c - file operations, run in steps.
 *
 * An operation is a queue plus a cursor: tiku_fs_step() does a little and
 * returns, and a collision or an error stops the queue until it is answered.
 * Every public entry point takes the engine's lock, the hand.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fs.h"
#include "tiku_mount.h"
#include "tiku_state.h"
#include "tiku_volume.h"

#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#define FS_QUEUE_MAX 512

/** @brief One reversal record: how to put a single item back. */
typedef struct {
    char from[TIKU_PATH_MAX];   /* where it ended up */
    char to[TIKU_PATH_MAX];     /* where it came from, "" if created */
    int  created;                   /* undo removes rather than moves    */
} undo_rec_t;

/**
 * @brief One reversible operation: its kind and the records it produced.
 *
 * Undo and redo take a whole operation, every record of it.
 */
typedef struct {
    tiku_op_t op;
    int           n;
    undo_rec_t   *rec;              /* n entries, grown as they arrive   */
} undo_op_t;

/**
 * @brief How deep the undo and redo stacks go; when one is full, its oldest
 *        operation is dropped.
 */
#define FS_UNDO_MAX 20


static void undo_stack_clear(undo_op_t *stack, int *count);

struct tiku_fs {
    tiku_backend_t *backend;
    char                queue[FS_QUEUE_MAX][TIKU_PATH_MAX];
    /* Set once a folder's children have been queued ahead of its second
     * visit, so a folder whose children will not go is not expanded again
     * (FS-049). */
    unsigned char       qexpanded[FS_QUEUE_MAX];
    int                 qcount;
    int                 qcursor;
    char                dst[TIKU_PATH_MAX];
    tiku_conflict_t policy;
    tiku_progress_t prog;

    int                 pending_ask;  /* a one-shot answer is in force  */
    undo_op_t           ustack[FS_UNDO_MAX];
    int                 ucount;
    undo_op_t           rstack[FS_UNDO_MAX];
    int                 rcount;

    tiku_cancel_fn  cancel_fn;
    void               *cancel_ctx;
    tiku_fs_moved_fn moved_fn;
    void               *moved_ctx;
        int                 cancelling;   /* the flag cancel() itself sets  */
    int                 link_relative;
    char                link_as[TIKU_NAME_MAX]; /* the one link's name, or "" */
    /* One hand on the engine: a worker steps it while the loop reads
     * its progress, answers its questions and cancels it.  The hand is
     * put down around each read and write of bytes, so the loop waits
     * for a chunk at most. */
    pthread_mutex_t     hand;
    pthread_t           owner;
    int                 depth;
    tiku_progress_t     prog_seen;    /* the loop's copy of prog        */
    /* What moved, told later on the loop: the hook that follows a move
     * touches windows, and a step may run on a worker. */
    struct { char from[TIKU_PATH_MAX]; char to[TIKU_PATH_MAX]; } moved[16];
    int                 moved_n;
};

/*---------------------------------------------------------------------------*/
/* THE HAND                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Take the hand, or deepen a hold this thread already has. */
static void
fs_take(tiku_fs_t *fs)
{
    if (fs->depth > 0 && pthread_equal(fs->owner, pthread_self())) {
        fs->depth++;
        return;
    }
    pthread_mutex_lock(&fs->hand);
    fs->owner = pthread_self();
    fs->depth = 1;
}

/** @brief Give the hand back, or shallow the hold. */
static void
fs_give(tiku_fs_t *fs)
{
    if (--fs->depth == 0) {
        pthread_mutex_unlock(&fs->hand);
    }
}

/** @brief Put the hand down entirely, for a wait.
 *  @return The hold to restore with fs_back(). */
static int
fs_rest(tiku_fs_t *fs)
{
    int held = fs->depth;

    fs->depth = 0;
    pthread_mutex_unlock(&fs->hand);
    return held;
}

/** @brief Pick the hand back up after fs_rest(). */
static void
fs_back(tiku_fs_t *fs, int held)
{
    pthread_mutex_lock(&fs->hand);
    fs->owner = pthread_self();
    fs->depth = held;
}

/** @brief Note that an item moved from @p from to @p to, for
 *         tiku_fs_moved_pump() to report; at most 16 wait. */
static void
landed(tiku_fs_t *fs, const char *from, const char *to)
{
    if (fs != NULL && fs->moved_fn != NULL && from != NULL && to != NULL &&
        from[0] != '\0' && to[0] != '\0' &&
        fs->moved_n < (int)(sizeof fs->moved / sizeof fs->moved[0])) {
        snprintf(fs->moved[fs->moved_n].from, TIKU_PATH_MAX, "%s", from);
        snprintf(fs->moved[fs->moved_n].to, TIKU_PATH_MAX, "%s", to);
        fs->moved_n++;
    }
}

void
tiku_fs_moved_pump(tiku_fs_t *fs)
{
    if (fs == NULL) {
        return;
    }
    for (;;) {
        char from[TIKU_PATH_MAX], to[TIKU_PATH_MAX];
        tiku_fs_moved_fn fn;
        void *ctx;

        fs_take(fs);
        if (fs->moved_n == 0) {
            fs_give(fs);
            return;
        }
        snprintf(from, sizeof from, "%s", fs->moved[0].from);
        snprintf(to, sizeof to, "%s", fs->moved[0].to);
        fs->moved_n--;
        memmove(&fs->moved[0], &fs->moved[1],
                (size_t)fs->moved_n * sizeof fs->moved[0]);
        fn = fs->moved_fn;
        ctx = fs->moved_ctx;
        fs_give(fs);
        /* Outside the hand: the hook opens windows. */
        if (fn != NULL) {
            fn(ctx, from, to);
        }
    }
}

/** @brief Install the moved hook. */
static void
fs_set_moved(tiku_fs_t *fs, tiku_fs_moved_fn fn, void *ctx)
{
    if (fs != NULL) {
        fs->moved_fn = fn;
        fs->moved_ctx = ctx;
    }
}

/** @brief Whether the job should stop where it stands. */
static int
cancelled(tiku_fs_t *fs)
{
    if (fs->cancelling) {
        return 1;
    }
    if (fs->cancel_fn != NULL && fs->cancel_fn(fs->cancel_ctx)) {
        fs->cancelling = 1;
    }
    return fs->cancelling;
}

/** @brief A zeroed engine over @p backend, or NULL. */
static tiku_fs_t *
fs_new(tiku_backend_t *backend)
{
    tiku_fs_t *fs = calloc(1, sizeof *fs);

    if (fs != NULL) {
        fs->backend = backend;
    }
    return fs;
}

/** @brief Free the engine and both undo stacks. */
static void
fs_free(tiku_fs_t *fs)
{
    if (fs != NULL) {
        undo_stack_clear(fs->ustack, &fs->ucount);
        undo_stack_clear(fs->rstack, &fs->rcount);
    }
    if (fs != NULL) {
        free(fs);
    }
}

/** @brief Whether an operation has items still to process. */
static int
fs_busy(const tiku_fs_t *fs)
{
    return (fs != NULL && fs->prog.op != TIKU_OP_IDLE &&
            fs->qcursor < fs->qcount);
}

/*---------------------------------------------------------------------------*/
/* names                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief The characters a name may not contain. */
#define NAME_DENY "/"

const char *
tiku_fs_name_deny(void)
{
    return NAME_DENY;
}

int
tiku_fs_name_ok(const char *name, char *err, size_t errmax)
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

/** @brief Whether an entry exists at @p dir / @p name; a name too long to
 *         join counts as taken. */
static int
exists(tiku_fs_t *fs, const char *dir, const char *name)
{
    char path[TIKU_PATH_MAX];
    tiku_model_t m;

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

/** @brief The last component of @p path, or @p path when it has none. */
static const char *
leaf_of(const char *path)
{
    const char *slash = strrchr(path, '/');

    return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

/**
 * @brief Split "<base> <word> N" into its base and the number.
 *
 * A name that already ends in the suffix is recognised, so "x copy"
 * duplicates to "x copy 2", not "x copy copy" (FS-006).
 *
 * @return N, 1 for a bare suffix, or 0 when @p name has no suffix.
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
free_suffixed_name(tiku_fs_t *fs, const char *dir, const char *name,
                   const char *word, int plain_first, char *out, size_t max)
{
    char base[TIKU_NAME_MAX];
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

/** @brief The first free "<name> copy [n]" in @p dir. */
static int
fs_duplicate_name(tiku_fs_t *fs, const char *dir,
                           const char *name, char *out, size_t max)
{
    /* Duplicate never reuses the plain name: a duplicate is always "copy". */
    return free_suffixed_name(fs, dir, name, "copy", 0, out, max);
}

/** @brief @p name when it is free in @p dir, else the first free
 *         "<name> link [n]". */
static int
fs_link_name(tiku_fs_t *fs, const char *dir, const char *name,
                      char *out, size_t max)
{
    /* A link keeps the original name when the destination allows it. */
    return free_suffixed_name(fs, dir, name, "link", 1, out, max);
}

/*---------------------------------------------------------------------------*/
/* drop semantics                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Which backend serves @p path, and the path it knows it by.
 *
 * The local filesystem calls in this file are made only for a path a local
 * store serves.
 */
static tiku_backend_t *
store_of(const tiku_fs_t *fs, const char *path, char *local, size_t max)
{
    return tiku_backend_serving(fs->backend, path, local, max);
}

/**
 * @brief Whether a device serves @p path.
 *
 * Asked per path: a namespace carries its root's identity, which with local
 * files at the root says nothing about a path under /devices/<board>.
 */
static int
on_device(const tiku_fs_t *fs, const char *path)
{
    tiku_backend_t *on = store_of(fs, path, NULL, 0);

    return on != NULL && on->devid[0] != '\0';
}

/**
 * @brief The volume @p path is on, as st_dev (its parent's when @p path
 *        does not exist); 0 when neither can be stat'ed.
 *
 * Every device path answers 1, so a drop inside a device is a move.
 */
static unsigned long
volume_of(tiku_fs_t *fs, const char *path)
{
    struct stat st;

    if (on_device(fs, path)) {
        return 1;
    }
    if (stat(path, &st) != 0) {
        char dir[TIKU_PATH_MAX];
        parent_of(path, dir, sizeof dir);
        if (stat(dir, &st) != 0) {
            return 0;
        }
    }
    return (unsigned long)st.st_dev;
}

/** @brief TIKU_OP_MOVE when @p src and @p dst_dir are on one volume and no
 *         copy was asked for, else TIKU_OP_COPY. */
static tiku_op_t
fs_drop_action(tiku_fs_t *fs, const char *src,
                        const char *dst_dir, int force_copy)
{
    unsigned long a, b;

    if (force_copy) {
        return TIKU_OP_COPY;
    }
    a = volume_of(fs, src);
    b = volume_of(fs, dst_dir);
    /* Same volume: a move is a rename and costs nothing.  Different volume:
     * the bytes have to travel, so it is a copy and the original stays. */
    return (a != 0 && a == b) ? TIKU_OP_MOVE : TIKU_OP_COPY;
}

/*---------------------------------------------------------------------------*/
/* the queue                                                                 */
/*---------------------------------------------------------------------------*/

/** @brief Release one operation's records. */
static void
undo_op_free(undo_op_t *o)
{
    free(o->rec);
    o->rec = NULL;
    o->n = 0;
    o->op = TIKU_OP_IDLE;
}

/** @brief Empty a stack. */
static void
undo_stack_clear(undo_op_t *stack, int *count)
{
    while (*count > 0) {
        undo_op_free(&stack[--(*count)]);
    }
}

/**
 * @brief Push @p o; when the stack is full, the oldest operation is
 *        dropped.
 */
static undo_op_t *
undo_stack_push(undo_op_t *stack, int *count, const undo_op_t *o)
{
    if (*count >= FS_UNDO_MAX) {
        int i;

        undo_op_free(&stack[0]);
        for (i = 1; i < FS_UNDO_MAX; i++) {
            stack[i - 1] = stack[i];
        }
        stack[FS_UNDO_MAX - 1].rec = NULL;
        (*count)--;
    }
    stack[*count] = *o;
    return &stack[(*count)++];
}

/**
 * @brief Begin recording a new operation of kind @p op, and empty the redo
 *        stack.
 */
static void
undo_reset(tiku_fs_t *fs, tiku_op_t op)
{
    undo_op_t fresh;

    memset(&fresh, 0, sizeof fresh);
    fresh.op = op;
    undo_stack_clear(fs->rstack, &fs->rcount);
    (void)undo_stack_push(fs->ustack, &fs->ucount, &fresh);
}

/** @brief Add one record to the operation being recorded: the item is now
 *         at @p from, it came from @p to, and @p created marks a thing the
 *         operation made. */
static void
undo_push(tiku_fs_t *fs, const char *from, const char *to, int created)
{
    undo_op_t  *o;
    undo_rec_t *grown, *r;

    if (fs->ucount == 0) {
        return;
    }
    o = &fs->ustack[fs->ucount - 1];
    grown = realloc(o->rec, (size_t)(o->n + 1) * sizeof *o->rec);
    if (grown == NULL) {
        return;                     /* the operation stands; only its
                                     * reversal is lost                  */
    }
    o->rec = grown;
    r = &o->rec[o->n++];
    memset(r, 0, sizeof *r);
    snprintf(r->from, sizeof r->from, "%s", from);
    snprintf(r->to, sizeof r->to, "%s", to ? to : "");
    r->created = created;
}

/**
 * @brief Whether a Trash sits directly inside @p path: <path>/.Trash
 *        exists.
 */
static int
contains_trash(tiku_fs_t *fs, const char *path)
{
    char probe[TIKU_PATH_MAX];
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

/**
 * @brief Queue @p paths for @p op into @p dst, leaving out what the rules
 *        refuse, and check what the volumes allow.
 *
 * At most FS_QUEUE_MAX paths are taken.
 *
 * @return 0 when the job starts, or -1 when the engine is busy or a volume
 *         refuses the whole job.
 */
static int
start(tiku_fs_t *fs, tiku_op_t op, const char *const *paths, int n,
      const char *dst, tiku_conflict_t policy)
{
    int i;

    if (fs == NULL || tiku_fs_busy(fs)) {
        return -1;
    }
    if (n > FS_QUEUE_MAX) {
        n = FS_QUEUE_MAX;
    }
    fs->prog.error[0] = '\0';
    fs->prog.skipped = 0;
    fs->qcount = 0;
    for (i = 0; i < n; i++) {
        if ((op == TIKU_OP_COPY || op == TIKU_OP_MOVE) &&
            dst != NULL) {
            /* Every copy and move passes this refusal check (FS-023,
             * FS-024, FS-025). */
            char why[200];

            if (tiku_fs_check_move(fs, paths[i], dst, why,
                                       sizeof why) != TIKU_REFUSE_NONE) {
                snprintf(fs->prog.error, sizeof fs->prog.error, "%s", why);
                fs->prog.skipped++;
                continue;
            }
        }
        /* A trashing drops the Trash, anything in it and anything that
         * holds it, without a message (FS-043).  Other operations keep
         * them, so a folder that holds a Trash can still be copied. */
        if (op == TIKU_OP_TRASH &&
            (strstr(paths[i], "/.Trash") != NULL ||
             contains_trash(fs, paths[i]))) {
            continue;
        }
        if ((op == TIKU_OP_TRASH || op == TIKU_OP_DELETE) &&
            !on_device(fs, paths[i])) {
            /* A whole volume is refused, with a message: trashed as a
             * directory, it would be moved into its own Trash. */
            tiku_volumes_t vs;

            if (tiku_volumes_scan(&vs) > 0 &&
                tiku_volume_is_root(&vs, paths[i])) {
                snprintf(fs->prog.error, sizeof fs->prog.error,
                         "\"%.40s\" is a disk, not an item: it can be "
                         "unmounted but not moved, copied or deleted.",
                         leaf_of(paths[i]));
                fs->prog.skipped++;
                continue;
            }
        }
        fs->qexpanded[fs->qcount] = 0;
        snprintf(fs->queue[fs->qcount++], TIKU_PATH_MAX, "%s", paths[i]);
    }
    fs->qcursor = 0;
    fs->policy = policy;
    snprintf(fs->dst, sizeof fs->dst, "%s", dst ? dst : "");
    {   /* Progress is cleared except for the refusal recorded while the
         * queue was built, and the skip count: they say why items are
         * missing. */
        char kept[sizeof fs->prog.error];
        int skipped = fs->prog.skipped;

        snprintf(kept, sizeof kept, "%s", fs->prog.error);
        memset(&fs->prog, 0, sizeof fs->prog);
        snprintf(fs->prog.error, sizeof fs->prog.error, "%s", kept);
        fs->prog.skipped = skipped;
    }
    {
        /* Counted before the job starts: the collision question's form
         * depends on how many there are (FS-017). */
        int k, collisions = 0;

        for (k = 0; dst != NULL && k < fs->qcount; k++) {
            if (exists(fs, dst, leaf_of(fs->queue[k]))) {
                collisions++;
            }
        }
        fs->prog.collisions = collisions;
    }
    {   /* What the volumes allow, before a byte moves: a destination
         * that takes no write refuses the job (FS-012); a move off a
         * read-only volume becomes a copy, which is how a file comes off
         * a disc (FS-013); and trashing or deleting an item on a
         * read-only volume is refused with the volume named (FS-045). */
        tiku_volumes_t vs;
        char why[200];
        int k;

        /* The volume rules apply to local filesystems, decided by the
         * destination, or by the first item when there is none. */
        if (!on_device(fs, (dst != NULL) ? dst : paths[0]) &&
            tiku_volumes_scan(&vs) > 0) {
            if (dst != NULL &&
                !tiku_volume_may_write(&vs, dst, 1, why, sizeof why)) {
                snprintf(fs->prog.error, sizeof fs->prog.error,
                         "You can't move or copy items to read-only "
                         "volumes.");
                fs->prog.error_ask = 1;
                fs->prog.error_can_continue = 0;
                fs->qcursor = fs->qcount;
                return -1;
            }
            if (op == TIKU_OP_MOVE) {
                for (k = 0; k < fs->qcount; k++) {
                    if (!tiku_volume_may_write(&vs, fs->queue[k], 1,
                                                   why, sizeof why)) {
                        op = TIKU_OP_COPY;
                        break;
                    }
                }
            } else if (op == TIKU_OP_TRASH || op == TIKU_OP_DELETE) {
                for (k = 0; k < fs->qcount; k++) {
                    if (!tiku_volume_may_write(&vs, fs->queue[k], 1,
                                                   why, sizeof why)) {
                        snprintf(fs->prog.error, sizeof fs->prog.error,
                                 "%s", why);
                        fs->prog.error_ask = 1;
                        fs->prog.error_can_continue = 0;
                        fs->qcursor = fs->qcount;
                        return -1;
                    }
                }
            }
        }
    }
    if (op == TIKU_OP_COPY && dst != NULL &&
        !tiku_fs_in_trash(fs, dst) && !on_device(fs, dst)) {
        /* The space needed is summed before a byte moves, and a copy that
         * would not fit does not start.  Skipped for the Trash, where the
         * copy becomes a move, and for a device, whose free space is not
         * known here (FS-015). */
        const char *q[FS_QUEUE_MAX];
        int k;
        uint64_t total;

        for (k = 0; k < fs->qcount; k++) {
            q[k] = fs->queue[k];
        }
        total = tiku_fs_items_size(q, fs->qcount);
        if (!tiku_fs_fits(total, tiku_fs_free_bytes(dst))) {
            snprintf(fs->prog.error, sizeof fs->prog.error,
                     "There is not enough free space on the destination "
                     "volume.");
            fs->prog.error_ask = 1;
            fs->prog.error_can_continue = 0;
            fs->qcursor = fs->qcount;
            return -1;
        }
    }
    if (op == TIKU_OP_COPY && dst != NULL &&
        tiku_fs_in_trash(fs, dst)) {
        /* A copy into the Trash is a move (FS-014). */
        op = TIKU_OP_MOVE;
    }
    fs->prog.op = op;
    fs->prog.total = fs->qcount;
    snprintf(fs->prog.to, sizeof fs->prog.to, "%s", dst ? dst : "");
    fs->cancelling = 0;             /* a new job is not the cancelled one */
    undo_reset(fs, op);
    return 0;
}

/** @brief Start a copy job: start() for TIKU_OP_COPY. */
static int
fs_copy(tiku_fs_t *fs, const char *const *paths, int n,
                 const char *dst_dir, tiku_conflict_t policy)
{
    return start(fs, TIKU_OP_COPY, paths, n, dst_dir, policy);
}

/** @brief Start a move job: start() for TIKU_OP_MOVE. */
static int
fs_move(tiku_fs_t *fs, const char *const *paths, int n,
                 const char *dst_dir, tiku_conflict_t policy)
{
    return start(fs, TIKU_OP_MOVE, paths, n, dst_dir, policy);
}

/** @brief Start a duplicate job, every collision renamed. */
static int
fs_duplicate(tiku_fs_t *fs, const char *const *paths, int n)
{
    return start(fs, TIKU_OP_DUPLICATE, paths, n, NULL,
                 TIKU_CONFLICT_RENAME);
}

/** @brief Start a trashing job. */
static int
fs_trash(tiku_fs_t *fs, const char *const *paths, int n)
{
    return start(fs, TIKU_OP_TRASH, paths, n, NULL,
                 TIKU_CONFLICT_SKIP);
}


uint64_t
tiku_fs_free_bytes(const char *path)
{
    struct statvfs v;

    if (path == NULL || statvfs(path, &v) != 0) {
        return 0u;
    }
    return (uint64_t)v.f_bavail * (uint64_t)v.f_frsize;
}

uint64_t
tiku_fs_items_size(const char *const *paths, int n)
{
    uint64_t total = 0u;
    int i;

    for (i = 0; i < n; i++) {
        struct stat st;
        DIR *d;

        if (paths[i] == NULL || lstat(paths[i], &st) != 0) {
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            /* A file is charged its size and one block: its st_blksize
             * clamped to [1024, 8192], or 2048 when the filesystem gives
             * none. */
            unsigned long blk = (st.st_blksize > 0)
                                    ? (unsigned long)st.st_blksize : 2048ul;

            if (blk < 1024ul) { blk = 1024ul; }
            if (blk > 8192ul) { blk = 8192ul; }
            total += (uint64_t)st.st_size + (uint64_t)blk;
            continue;
        }
        total += 1024u;             /* one block for the directory itself */
        d = opendir(paths[i]);
        if (d != NULL) {
            struct dirent *e;

            while ((e = readdir(d)) != NULL) {
                char child[TIKU_PATH_MAX];
                const char *one[1];

                if (strcmp(e->d_name, ".") == 0 ||
                    strcmp(e->d_name, "..") == 0) {
                    continue;
                }
                if (join(child, sizeof child, paths[i], e->d_name) != 0) {
                    continue;
                }
                one[0] = child;
                total += tiku_fs_items_size(one, 1);
            }
            (void)closedir(d);
        }
    }
    return total;
}

int
tiku_fs_fits(uint64_t total, uint64_t free_bytes)
{
    /* Four kilobytes of headroom; landing exactly on the free space
     * counts as not fitting. */
    if (free_bytes == 0u) {
        return 1;                   /* unknown: not a reason to refuse    */
    }
    return !((total + 4u * 1024u) >= free_bytes);
}

/** @brief Whether @p path is @p dir or something under it. */
static int
under(const char *path, const char *dir)
{
    size_t n;

    if (path == NULL || dir == NULL || dir[0] == '\0') {
        return 0;
    }
    n = strlen(dir);
    while (n > 1u && dir[n - 1u] == '/') {
        n--;
    }
    return (strncmp(path, dir, n) == 0 &&
            (path[n] == '\0' || path[n] == '/'));
}

tiku_confirm_t
tiku_fs_confirm_move(const char *path)
{
    /* The system directories: a move out of one needs Shift held to
     * confirm. */
    static const char *system_dirs[] = {
        "/bin", "/boot", "/etc", "/lib", "/lib64", "/sbin", "/usr", "/var"
    };
    const char *home = getenv("HOME");
    size_t i;

    if (path == NULL || path[0] == '\0') {
        return TIKU_CONFIRM_NONE;
    }
    for (i = 0; i < sizeof system_dirs / sizeof system_dirs[0]; i++) {
        if (under(path, system_dirs[i])) {
            return TIKU_CONFIRM_OVERRIDE;
        }
    }
    if (home != NULL && home[0] != '\0') {
        char cfg[TIKU_PATH_MAX];
        const char *xdg = getenv("XDG_CONFIG_HOME");

        /* Home itself only, by exact match: a file inside home moves
         * without a question. */
        if (strcmp(path, home) == 0) {
            return TIKU_CONFIRM_OVERRIDE;
        }
        if (xdg != NULL && xdg[0] != '\0' && under(path, xdg)) {
            return TIKU_CONFIRM_ASK;
        }
        if (snprintf(cfg, sizeof cfg, "%s/.config", home) > 0 &&
            under(path, cfg)) {
            return TIKU_CONFIRM_ASK;
        }
    }
    return TIKU_CONFIRM_NONE;
}

/** @brief Whether @p path is its filesystem's Trash or inside it. */
static int
fs_in_trash(tiku_fs_t *fs, const char *path)
{
    char trash[TIKU_PATH_MAX];
    size_t n;

    if (fs == NULL || path == NULL ||
        tiku_fs_trash_dir(fs, path, trash, sizeof trash) != 0) {
        return 0;
    }
    n = strlen(trash);
    /* A prefix test on a whole component, so a sibling named ".Trashcan"
     * is not mistaken for something inside the Trash. */
    return (strncmp(path, trash, n) == 0 &&
            (path[n] == '\0' || path[n] == '/'));
}

/** @brief Start a job linking @p paths into @p dst. */
static int
fs_link(tiku_fs_t *fs, const char *const *paths, int n,
                 const char *dst, int relative)
{
    if (fs == NULL) {
        return -1;
    }
    fs->link_relative = relative;
    fs->link_as[0] = '\0';
    return start(fs, TIKU_OP_LINK, paths, n, dst,
                 TIKU_CONFLICT_ASK);
}

/** @brief Start a job making one link to @p path in @p dst, named
 *         @p name. */
static int
fs_link_as(tiku_fs_t *fs, const char *path, const char *dst,
           const char *name, int relative)
{
    const char *paths[1] = { path };

    if (fs == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    fs->link_relative = relative;
    snprintf(fs->link_as, sizeof fs->link_as, "%s", name);
    return start(fs, TIKU_OP_LINK, paths, 1, dst, TIKU_CONFLICT_ASK);
}

/** @brief Start a deletion job. */
static int
fs_delete(tiku_fs_t *fs, const char *const *paths, int n)
{
    return start(fs, TIKU_OP_DELETE, paths, n, NULL,
                 TIKU_CONFLICT_ASK);
}
/*---------------------------------------------------------------------------*/
/* the work                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Give @p dst the source's permissions, owner, group and times.
 *
 * Mode and times are part of what is copied (FS-031).  Ownership is
 * attempted and may fail: only root may give a file away.
 *
 * @param link  Non-zero when @p dst is a symlink, which must not be followed.
 */
static void
carry_over(const char *src, const char *dst, int link)
{
    struct stat st;
    struct timespec times[2];

    if ((link ? lstat(src, &st) : stat(src, &st)) != 0) {
        return;
    }
    times[0] = st.st_atim;
    times[1] = st.st_mtim;
    if (link) {
        if (lchown(dst, st.st_uid, st.st_gid) != 0) { errno = 0; }
        (void)utimensat(AT_FDCWD, dst, times, AT_SYMLINK_NOFOLLOW);
        return;
    }
    if (chown(dst, st.st_uid, st.st_gid) != 0) { errno = 0; }
    (void)chmod(dst, st.st_mode & 07777u);
    (void)utimensat(AT_FDCWD, dst, times, 0);
}

/**
 * @brief Carry an icon pose record along with a cross-folder copy.
 *
 * The copy takes the source's pose record from the state store.  An
 * explicit drop point, applied when the destination view hears of the new
 * item, still wins (FS-037).
 */
static void
copy_pose_info(tiku_fs_t *fs, const char *src, const char *dst)
{
    tiku_store_t *store;
    unsigned char raw[64];
    int n;

    if (fs == NULL || fs->backend == NULL || src == NULL || dst == NULL ||
        fs->backend->ops->state_store == NULL) {
        return;
    }
    store = fs->backend->ops->state_store(fs->backend);
    if (store == NULL || !tiku_store_writable(store)) {
        return;
    }
    n = tiku_state_read(store, src, TIKU_ATTR_POSE_INFO, raw,
                            sizeof raw);
    if (n > 0) {
        (void)tiku_state_write(store, dst, TIKU_ATTR_POSE_INFO, raw,
                                   (size_t)n);
    }
}

/**
 * @brief Copy one node from one store to another, through both of them.
 *
 * Between two stores only the backend ops apply.  Read takes no offset, so
 * a node larger than the 64 KB buffer is refused; and the ops cannot make a
 * directory, so a folder is refused.
 *
 * @return 0, -1 with @p err filled, or -2 when cancelled.
 */
static int
copy_across(tiku_fs_t *fs, tiku_backend_t *sb, const char *slocal,
            tiku_backend_t *db, const char *dlocal, const char *src,
            char *err, size_t errmax)
{
    static unsigned char across[64 * 1024];
    tiku_model_t m;
    int len;

    /* Cancel is asked once, before the one read and the one write. */
    if (cancelled(fs)) {
        snprintf(err, errmax, "cancelled");
        return -2;
    }
    if (sb->ops->stat(sb, slocal, &m) != 0) {
        snprintf(err, errmax, "cannot read '%.60s'", leaf_of(src));
        return -1;
    }
    if (tiku_model_is_container(&m)) {
        snprintf(err, errmax,
                 "a folder cannot be copied between stores yet: the "
                 "backend contract has no way to make one");
        return -1;
    }
    if (m.facts.size > (int64_t)sizeof across) {
        snprintf(err, errmax,
                 "'%.40s' is too big to copy between stores: they are "
                 "read whole, and this one is %lld bytes",
                 leaf_of(src), (long long)m.facts.size);
        return -1;
    }
        {
        int held = fs_rest(fs);

        len = sb->ops->read(sb, slocal, across, sizeof across);
        fs_back(fs, held);
    }
    if (len < 0) {
        snprintf(err, errmax, "cannot read '%.60s'", leaf_of(src));
        return -1;
    }
    /* A read shorter than the entry's size is refused: writing it would
     * leave a truncated file. */
    if (m.facts.size > 0 && (int64_t)len < m.facts.size) {
        snprintf(err, errmax,
                 "'%.40s' was read short, %d of %lld bytes", leaf_of(src),
                 len, (long long)m.facts.size);
        return -1;
    }
    if (db->ops->write == NULL) {
        snprintf(err, errmax, "that store does not take writes");
        return -1;
    }
        {
        int held = fs_rest(fs);
        int rc = db->ops->write(db, dlocal, across, (size_t)len, err,
                                errmax);

        fs_back(fs, held);
        return rc;
    }
}

/**
 * @brief Copy one file's bytes within one store: through the backend on a
 *        device, with stdio for local files.
 * @return 0, -1 with @p err filled, or -2 when cancelled, the partial file
 *         removed.
 */
static int
copy_bytes(tiku_fs_t *fs, const char *src, const char *dst, char *err,
           size_t errmax)
{
    static unsigned char buf[64 * 1024];
    FILE *in, *out;
    size_t n;

    /* A device file goes through the backend.  Both ends are on one store
     * here (copy_across() takes a crossing), so the source answers for
     * both. */
    if (on_device(fs, src)) {
        int len = fs->backend->ops->read(fs->backend, src, buf, sizeof buf);
        if (len < 0) {
            snprintf(err, errmax, "cannot read '%s'", src);
            return -1;
        }
        return fs->backend->ops->write(fs->backend, dst, buf, (size_t)len,
                                       err, errmax);
    }
    {   /* Space is checked again for each file: another process may have
         * taken it since the job started (FS-016). */
        struct stat st;
        char dir[TIKU_PATH_MAX];

        uint64_t freeb;

        parent_of(dst, dir, sizeof dir);
        freeb = tiku_fs_free_bytes(dir);
        /* One kilobyte of headroom per file. */
        if (freeb != 0u && stat(src, &st) == 0 &&
            (uint64_t)st.st_size + 1024u >= freeb) {
            snprintf(err, errmax, "There is not enough free space on the "
                                  "destination volume.");
            return -1;
        }
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
    for (;;) {
        /* Cancel is asked before each chunk. */
        if (cancelled(fs)) {
            (void)fclose(in);
            (void)fclose(out);
            /* The half-written destination is removed (FS-033). */
            (void)remove(dst);
            snprintf(err, errmax, "cancelled");
            return -2;
        }
                {
            int held = fs_rest(fs);

            n = fread(buf, 1, sizeof buf, in);
            fs_back(fs, held);
        }
        if (n == 0) {
            break;
        }
        fs->prog.bytes += (int64_t)n;
        {
            int held = fs_rest(fs);
            size_t put = fwrite(buf, 1, n, out);

            fs_back(fs, held);
            if (put == n) {
                continue;
            }
        }
        {
            (void)fclose(in);
            (void)fclose(out);
            snprintf(err, errmax, "write failed on '%s'", dst);
            return -1;
        }
    }
    (void)fclose(in);
    return (fclose(out) == 0) ? 0 : -1;
}



/*---------------------------------------------------------------------------*/
/* Copying and moving a tree (FS-020, FS-029)                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Remove @p path and everything under it.
 *
 * Best effort: a child that will not go does not stop the rest.  A
 * symbolic link is removed itself; what it points at is not entered.
 *
 * @return 0 when the top-level item went.
 */
static int
remove_tree(const char *path)
{
    struct stat st;
    DIR *d;
    struct dirent *e;

    if (lstat(path, &st) != 0) {
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return (unlink(path) == 0) ? 0 : -1;
    }
    d = opendir(path);
    while (d != NULL && (e = readdir(d)) != NULL) {
        char child[TIKU_PATH_MAX];

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (join(child, sizeof child, path, e->d_name) == 0) {
            (void)remove_tree(child);
        }
    }
    if (d != NULL) {
        (void)closedir(d);
    }
    return (rmdir(path) == 0) ? 0 : -1;
}

/**
 * @brief Copy @p src to @p dstpath, merging into a folder already there.
 *
 * Replacing a folder merges: the destination folder is kept, its children
 * with no counterpart in the source stay, same-named files are replaced and
 * same-named folders merge by the same rule.
 *
 * @param dstpath The full destination path, whose name a collision answer
 *                may have chosen.
 * @return 0, -1 with @p err filled, or -2 when cancelled.
 */
static int
copy_tree(tiku_fs_t *fs, const char *src, const char *dstpath, char *err,
          size_t errmax)
{
    struct stat st;
    dev_t sdev;
    DIR *d;
    struct dirent *e;
    {
        /* Between two stores the local filesystem calls below do not
         * apply; copy_across() copies through the backends. */
        char sl[TIKU_PATH_MAX], dl[TIKU_PATH_MAX];
        tiku_backend_t *sb = store_of(fs, src, sl, sizeof sl);
        tiku_backend_t *db = store_of(fs, dstpath, dl, sizeof dl);

        if (sb != NULL && db != NULL && sb != db) {
            return copy_across(fs, sb, sl, db, dl, src, err, errmax);
        }
    }

    /* lstat(), so a link is seen as a link (FS-032). */
    if (lstat(src, &st) != 0) {
        snprintf(err, errmax, "cannot read '%.60s'", leaf_of(src));
        return -1;
    }
    if (S_ISLNK(st.st_mode)) {
        /* A link is copied as a link, not as what it points at
         * (FS-032). */
        char target[TIKU_PATH_MAX];
        ssize_t n = readlink(src, target, sizeof target - 1u);

        if (n < 0) {
            snprintf(err, errmax, "cannot read the link '%.50s'",
                     leaf_of(src));
            return -1;
        }
        target[n] = '\0';
        (void)remove(dstpath);
        if (symlink(target, dstpath) != 0) {
            snprintf(err, errmax, "cannot create the link '%.50s' (%.40s)",
                     leaf_of(dstpath), strerror(errno));
            return -1;
        }
        /* Owner and times, but no attributes. */
        carry_over(src, dstpath, 1);
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) {
        int rc = copy_bytes(fs, src, dstpath, err, errmax);

        if (rc == 0) {
            carry_over(src, dstpath, 0);
        }
        return rc;
    }
    /* The source's device, kept before st is reused for the destination:
     * the mount-point test below compares each child against it. */
    sdev = st.st_dev;
    if (stat(dstpath, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            snprintf(err, errmax,
                     "cannot replace the file '%.50s' with a folder",
                     leaf_of(dstpath));
            return -1;
        }
        /* A folder is there: merge into it, its own attributes left as
         * they are. */
    } else if (mkdir(dstpath, 0755) != 0) {
        snprintf(err, errmax, "cannot create '%.60s' (%.40s)",
                 leaf_of(dstpath), strerror(errno));
        return -1;
    } else {
        carry_over(src, dstpath, 0);
    }
    d = opendir(src);
    if (d == NULL) {
        snprintf(err, errmax, "cannot read '%.60s'", leaf_of(src));
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char child[TIKU_PATH_MAX];

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        {
            char cdst[TIKU_PATH_MAX];

            if (join(child, sizeof child, src, e->d_name) != 0 ||
                join(cdst, sizeof cdst, dstpath, e->d_name) != 0) {
                continue;
            }
            /* A failure on one child does not stop the rest. */
            {   /* A directory on another filesystem is a mount point and
                 * is not copied, and an entry that is neither file,
                 * folder nor link, such as a device node, is left out
                 * (FS-036). */
                struct stat cs;

                if (lstat(child, &cs) == 0) {
                    if (S_ISDIR(cs.st_mode) && cs.st_dev != sdev) {
                        continue;
                    }
                    if (!S_ISDIR(cs.st_mode) && !S_ISREG(cs.st_mode) &&
                        !S_ISLNK(cs.st_mode)) {
                        continue;
                    }
                }
            }
            if (copy_tree(fs, child, cdst, err, errmax) == -2) {
                /* Cancelled: the folders made and the files written stay,
                 * and only the file being written is removed (FS-034). */
                (void)closedir(d);
                return -2;
            }
        }
    }
    (void)closedir(d);
    return 0;
}

/**
 * @brief Move @p src to @p dstpath, merging into a folder already there
 *        (FS-029).
 *
 * On one filesystem the move is a rename; across filesystems, or from local
 * files to another store, it is a copy and then a delete.  A same-named
 * folder at the destination is merged into, and the emptied source folders
 * are removed.
 *
 * @return 0, or negative with @p err filled.
 */
static int
move_tree(tiku_fs_t *fs, const char *src, const char *dstpath, char *err,
          size_t errmax)
{
    struct stat sst, dst_st;
    DIR *d;
    struct dirent *e;
    {
        /* Between two stores a move is a copy and then a delete, and the
         * delete needs the local filesystem: the backend ops remove
         * nothing.  A move off a device is refused. */
        char sl[TIKU_PATH_MAX], dl[TIKU_PATH_MAX];
        tiku_backend_t *sb = store_of(fs, src, sl, sizeof sl);
        tiku_backend_t *db = store_of(fs, dstpath, dl, sizeof dl);

        if (sb != NULL && db != NULL && sb != db) {
            int rc;

            if (sb->devid[0] != '\0') {
                snprintf(err, errmax,
                         "'%.40s' can be copied off that store but not "
                         "moved: nothing in the backend contract removes "
                         "anything", leaf_of(src));
                return -1;
            }
            rc = copy_across(fs, sb, sl, db, dl, src, err, errmax);
            if (rc != 0) {
                return rc;
            }
            return remove_tree(sl);
        }
    }

    if (stat(src, &sst) != 0) {
        snprintf(err, errmax, "cannot read '%.60s'", leaf_of(src));
        return -1;
    }
    if (stat(dstpath, &dst_st) != 0) {
        /* Nothing in the way: one rename moves the whole tree. */
        if (rename(src, dstpath) == 0) {
            return 0;
        }
        if (errno != EXDEV) {
            snprintf(err, errmax, "cannot move '%.60s' (%.40s)",
                     leaf_of(src), strerror(errno));
            return -1;
        }
        /* Across a filesystem boundary a move is a copy and then a delete
         * (FS-030). */
        if (copy_tree(fs, src, dstpath, err, errmax) != 0) {
            return -1;
        }
        return remove_tree(src);
    }
    if (!S_ISDIR(sst.st_mode) || !S_ISDIR(dst_st.st_mode)) {
        /* A file over a file: the refusal gate has already allowed this,
         * so the destination goes and the source takes its place. */
        (void)remove(dstpath);
        if (rename(src, dstpath) != 0) {
            snprintf(err, errmax, "cannot move '%.60s' (%.40s)",
                     leaf_of(src), strerror(errno));
            return -1;
        }
        return 0;
    }
    d = opendir(src);
    if (d == NULL) {
        snprintf(err, errmax, "cannot read '%.60s'", leaf_of(src));
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char child[TIKU_PATH_MAX];

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        {
            char cdst[TIKU_PATH_MAX];

            if (join(child, sizeof child, src, e->d_name) == 0 &&
                join(cdst, sizeof cdst, dstpath, e->d_name) == 0) {
                (void)move_tree(fs, child, cdst, err, errmax);
            }
        }
    }
    (void)closedir(d);
    /* The emptied source folder goes; one with something left in it
     * stays. */
    (void)rmdir(src);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Refusals (FS-023, FS-024, FS-025)                                         */
/*---------------------------------------------------------------------------*/

/** @brief Whether @p dir is @p path or contains it, at a component
 *         boundary. */
static int
path_contains(const char *dir, const char *path)
{
    size_t n;

    if (dir == NULL || path == NULL) {
        return 0;
    }
    n = strlen(dir);
    while (n > 1u && dir[n - 1u] == '/') {
        n--;
    }
    if (strncmp(dir, path, n) != 0) {
        return 0;
    }
    /* The same path counts as contained; a longer one only when the next
     * character is a separator, or "/a/b" would swallow "/a/bc". */
    return path[n] == '\0' || path[n] == '/';
}

/** @brief Whether @p path is a directory. */
static int
is_dir(const char *path)
{
    struct stat st;

    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

/** @brief The refusal, if any, for moving or copying @p src into
 *         @p dst_dir; @p msg receives the sentence to show. */
static tiku_refusal_t
fs_check_move(tiku_fs_t *fs, const char *src, const char *dst_dir,
                       char *msg, size_t max)
{
    char dstpath[TIKU_PATH_MAX];

    if (msg != NULL && max > 0u) {
        msg[0] = '\0';
    }
    if (fs == NULL || src == NULL || dst_dir == NULL) {
        return TIKU_REFUSE_NONE;
    }
    /* The Trash is not an item to be moved about; it is where items go. */
    if (strstr(src, "/" TIKU_TRASH_NAME) != NULL &&
        strcmp(leaf_of(src), TIKU_TRASH_NAME) == 0) {
        if (msg != NULL) {
            snprintf(msg, max, "You can't move or copy the trash.");
        }
        return TIKU_REFUSE_TRASH;
    }
    /*
     * A whole volume is not an item: a mount point moved off its device
     * gets EXDEV, and the fallback would copy every byte of the volume and
     * then delete the source.
     */
    if (!on_device(fs, src)) {
        tiku_volumes_t vs;

        if (tiku_volumes_scan(&vs) > 0 &&
            tiku_volume_is_root(&vs, src)) {
            if (msg != NULL) {
                snprintf(msg, max,
                         "\"%.40s\" is a disk, not an item: it can be "
                         "unmounted but not moved, copied or deleted.",
                         leaf_of(src));
            }
            return TIKU_REFUSE_VOLUME;
        }
    }
    /* A folder into itself, or into anything underneath it. */
    if (is_dir(src) && path_contains(src, dst_dir)) {
        if (msg != NULL) {
            snprintf(msg, max,
                     "You can't move a folder into itself or any of its own "
                     "sub-folders.");
        }
        return TIKU_REFUSE_INTO_SELF;
    }
    if (join(dstpath, sizeof dstpath, dst_dir, leaf_of(src)) != 0) {
        return TIKU_REFUSE_NONE;
    }
    {
        struct stat dst_st;

        if (stat(dstpath, &dst_st) != 0) {
            return TIKU_REFUSE_NONE;    /* no collision, no question  */
        }
        /* The second refusal is the first one inverted: here the thing
         * being overwritten is an ancestor of the thing overwriting it. */
        if (S_ISDIR(dst_st.st_mode) && path_contains(dstpath, src)) {
            if (msg != NULL) {
                snprintf(msg, max,
                         "You can't replace a folder with one of its "
                         "sub-folders.");
            }
            return TIKU_REFUSE_BY_CHILD;
        }
        /* A kind mismatch is refused outright. */
        if (S_ISDIR(dst_st.st_mode) != (is_dir(src) ? 1 : 0)) {
            if (msg != NULL) {
                snprintf(msg, max, is_dir(src)
                         ? "You cannot replace a file with a folder or a "
                           "symbolic link."
                         : "You cannot replace a folder or a symbolic link "
                           "with a file.");
            }
            return TIKU_REFUSE_KIND;
        }
    }
    return TIKU_REFUSE_NONE;
}

/*---------------------------------------------------------------------------*/
/* The Trash (FS-038, FS-039, FS-040, FS-044, FS-047, FS-048)                */
/*---------------------------------------------------------------------------*/

/** @brief Create @p path and every missing folder above it. */
static int
make_path(const char *path)
{
    char buf[TIKU_PATH_MAX];
    char *p;

    snprintf(buf, sizeof buf, "%s", path);
    for (p = buf + 1; *p != '\0'; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        (void)mkdir(buf, 0755);
        *p = '/';
    }
    return mkdir(buf, 0755);
}

/**
 * @brief Whether the store can hold a Trash, answered for its root: a
 *        device has none, as its nodes are not files (FS-044).
 *
 * Whether one item can be trashed is answered by tiku_fs_trash_dir().
 */
static int
fs_has_trash(const tiku_fs_t *fs)
{
    return (fs != NULL && fs->backend != NULL && !on_device(fs, "/"));
}

/**
 * @brief The root of the filesystem @p path lives on.
 *
 * Walked up until the device changes.  The Trash is on the item's own
 * filesystem, so trashing is a rename.
 */
static void
volume_root(const char *path, char *out, size_t max)
{
    char cur[TIKU_PATH_MAX], parent[TIKU_PATH_MAX];
    struct stat st, pst;

    snprintf(out, max, "/");
    if (path == NULL || path[0] == '\0') {
        return;
    }
    snprintf(cur, sizeof cur, "%s", path);
    /* The item itself may be gone, as when a trashed item is put back:
     * the walk starts at the nearest ancestor that exists. */
    while (stat(cur, &st) != 0) {
        char *slash = strrchr(cur, '/');

        if (slash == NULL || slash == cur) {
            return;                     /* nothing above it exists       */
        }
        *slash = '\0';
    }
    for (;;) {
        char *slash = strrchr(cur, '/');

        if (slash == NULL) {
            break;
        }
        if (slash == cur) {
            snprintf(parent, sizeof parent, "/");
        } else {
            snprintf(parent, sizeof parent, "%.*s", (int)(slash - cur), cur);
        }
        if (stat(parent, &pst) != 0 || pst.st_dev != st.st_dev) {
            snprintf(out, max, "%s", cur);   /* the parent is elsewhere   */
            return;
        }
        if (strcmp(parent, "/") == 0) {
            snprintf(out, max, "/");
            return;
        }
        snprintf(cur, sizeof cur, "%s", parent);
    }
}

/** @brief Make @p dir/.Trash if it is not there.  @return 0 on success. */
static int
trash_under(const char *dir, char *out, size_t max)
{
    struct stat st;
    int n;

    /* One slash between, so "/" gives "/.Trash", not "//.Trash". */
    n = snprintf(out, max, "%s%s%s", dir,
                 (dir[0] != '\0' && dir[strlen(dir) - 1u] == '/') ? "" : "/",
                 TIKU_TRASH_NAME);
    if (n < 0 || (size_t)n >= max) {
        return -1;
    }
    if (stat(out, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    return (mkdir(out, 0755) == 0) ? 0 : -1;
}

/** @brief tiku_fs_trash_dirs(): the Trash of every writable volume in the
 *         mount table. */
static int
fs_trash_dirs(tiku_fs_t *fs, char out[][TIKU_PATH_MAX],
                       int max)
{
    tiku_volumes_t vs;

    if (fs == NULL || out == NULL || max <= 0 ||
        !tiku_fs_has_trash(fs)) {
        return 0;
    }
    if (on_device(fs, "/") || tiku_volumes_scan(&vs) <= 0) {
        /* With no volume table, the one Trash is the home folder's. */
        if (tiku_fs_trash_dir(fs, NULL, out[0], TIKU_PATH_MAX) ==
                0) {
            return 1;
        }
        return 0;
    }
    return tiku_fs_trash_dirs_of(fs,
               (const struct tiku_volumes *)&vs, out, max);
}

/** @brief Make every writable volume's Trash.  @return How many there
 *         are. */
static int
fs_prepare_trash(tiku_fs_t *fs)
{
    char dirs[TIKU_VOLUME_MAX][TIKU_PATH_MAX];

    if (fs == NULL) {
        return 0;
    }
    /* Called at startup, not from tiku_fs_new(): making an engine creates
     * no directories. */
    return tiku_fs_trash_dirs(fs, dirs, TIKU_VOLUME_MAX);
}

/** @brief The Trash of each writable volume in @p v, each listed once. */
static int
fs_trash_dirs_of(tiku_fs_t *fs, const struct tiku_volumes *v,
                          char out[][TIKU_PATH_MAX], int max)
{
    const tiku_volumes_t *vsp = (const tiku_volumes_t *)v;
    int i, n = 0;

    if (fs == NULL || vsp == NULL || out == NULL || max <= 0) {
        return 0;
    }
    for (i = 0; i < vsp->n && n < max; i++) {
        char here[TIKU_PATH_MAX];
        int k, dupe = 0;

        if (vsp->v[i].read_only) {
            /* A read-only volume has no Trash; asking would try to make
             * one. */
            continue;
        }
        if (tiku_fs_trash_dir(fs, vsp->v[i].mount, here,
                                  sizeof here) != 0) {
            continue;
        }
        for (k = 0; k < n; k++) {
            if (strcmp(out[k], here) == 0) {
                dupe = 1;       /* two mounts resolving to one Trash */
                break;
            }
        }
        if (!dupe) {
            snprintf(out[n++], TIKU_PATH_MAX, "%s", here);
        }
    }
    return n;
}

/** @brief The Trash for @p path, made when absent; see
 *         tiku_fs_trash_dir().  @return 0, or -1. */
static int
fs_trash_dir(tiku_fs_t *fs, const char *path, char *out,
                      size_t max)
{
    char root[TIKU_PATH_MAX];
    const char *home = getenv("HOME");
    struct stat rs, hs;

    if (fs == NULL || out == NULL || path == NULL ||
        !tiku_fs_has_trash(fs) || on_device(fs, path)) {
        /* Asked per item: with a board mounted beside the disk, the disk
         * has a Trash and the board has none. */
        return -1;
    }
    /* Resolved from the item's own filesystem, so trashing is a
     * rename. */
    volume_root((path != NULL) ? path : home, root, sizeof root);
    /* The home folder's Trash first, whenever home is on that filesystem:
     * a volume root can be writable by everyone (a sticky /tmp), and a
     * Trash made there would belong to whoever trashed first.  Home is per
     * user and on the same filesystem, so the move stays a rename. */
    if (home != NULL && home[0] != '\0' && stat(home, &hs) == 0 &&
        stat(root, &rs) == 0 && hs.st_dev == rs.st_dev &&
        trash_under(home, out, max) == 0) {
        return 0;
    }
    /* A volume the home does not live on keeps its Trash at its root. */
    return (trash_under(root, out, max) == 0) ? 0 : -1;
}

/** @brief Where the trashed item at @p path came from, into @p out.
 *  @return 1, or 0 when nothing was recorded. */
static int
fs_original_path(tiku_fs_t *fs, const char *path, char *out,
                          size_t max)
{
    tiku_store_t *store;
    int n;

    if (fs == NULL || path == NULL || out == NULL || max == 0u ||
        fs->backend->ops->state_store == NULL) {
        return 0;
    }
    store = fs->backend->ops->state_store(fs->backend);
    if (store == NULL) {
        return 0;
    }
    n = tiku_state_read(store, path, TIKU_ATTR_ORIGINAL, out,
                            max - 1u);
    if (n <= 0) {
        out[0] = '\0';
        return 0;
    }
    out[n] = '\0';
    return 1;
}

/** @brief Remember where @p trashed came from, under its trashed name. */
static void
record_origin(tiku_fs_t *fs, const char *trashed, const char *origin)
{
    tiku_store_t *store;

    if (fs->backend->ops->state_store == NULL) {
        return;
    }
    store = fs->backend->ops->state_store(fs->backend);
    if (store == NULL || !tiku_store_writable(store)) {
        return;
    }
    /* Recorded under the path the item now has, in the Trash. */
    (void)tiku_state_write(store, trashed, TIKU_ATTR_ORIGINAL,
                               origin, strlen(origin));
    (void)tiku_store_flush(store);
}

/** @brief The Delete key's question for @p path; see tiku_delete_q_t. */
static tiku_delete_q_t
fs_delete_question(const tiku_fs_t *fs, const char *path,
                            int shift_held)
{
    if (fs == NULL) {
        return TIKU_DELETE_STRAIGHT;
    }
    if (shift_held) {
        return TIKU_DELETE_STRAIGHT;    /* the user has decided       */
    }
    {
        /* Asked of this item's filesystem: with no Trash there, the item
         * is deleted outright (FS-044). */
        char trash[TIKU_PATH_MAX];

        if (tiku_fs_trash_dir((tiku_fs_t *)fs, path, trash,
                                  sizeof trash) != 0) {
            return TIKU_DELETE_STRAIGHT;
        }
    }
    if (path != NULL && strstr(path, "/" TIKU_TRASH_NAME) != NULL) {
        /* Inside the Trash the item is deleted outright (FS-047). */
        return TIKU_DELETE_STRAIGHT;
    }
    return TIKU_DELETE_ASK;
}

/** @brief Put trashed items back where they came from; see
 *         tiku_fs_restore().  @return Number restored. */
static int
fs_restore(tiku_fs_t *fs, const char *const *paths, int n,
                    char *err, size_t errmax)
{
    int i, done = 0;

    if (fs == NULL || paths == NULL) {
        return 0;
    }
    if (err != NULL && errmax > 0u) {
        err[0] = '\0';
    }
    for (i = 0; i < n; i++) {
        char origin[TIKU_PATH_MAX];
        struct stat st;

        if (!tiku_fs_original_path(fs, paths[i], origin, sizeof origin)) {
            if (err != NULL) {
                snprintf(err, errmax,
                         "'%s' does not remember where it came from",
                         leaf_of(paths[i]));
            }
            continue;
        }
        if (stat(origin, &st) == 0) {
            /* Something at the original path is not overwritten: the item
             * stays in the Trash (FS-040). */
            if (err != NULL) {
                snprintf(err, errmax,
                         "cannot restore '%s': something is there already",
                         leaf_of(origin));
            }
            continue;
        }
        {   /* The folders it lived in may have gone since; make them. */
            char parent[TIKU_PATH_MAX];
            char *slash;

            snprintf(parent, sizeof parent, "%s", origin);
            slash = strrchr(parent, '/');
            if (slash != NULL && slash != parent) {
                *slash = '\0';
                if (stat(parent, &st) != 0) {
                    (void)make_path(parent);
                }
            }
        }
        if (rename(paths[i], origin) != 0) {
            if (err != NULL) {
                snprintf(err, errmax, "cannot restore '%s' (%s)",
                         leaf_of(paths[i]), strerror(errno));
            }
            continue;
        }
        landed(fs, paths[i], origin);
        done++;
    }
    return done;
}

/** @brief Start deleting what every Trash holds.  @return Items queued. */
static int
fs_empty_trash(tiku_fs_t *fs)
{
    char dirs[TIKU_VOLUME_MAX][TIKU_PATH_MAX];
    char paths[FS_QUEUE_MAX][TIKU_PATH_MAX];
    const char *sel[FS_QUEUE_MAX];
    int n = 0;
    int nd, k;

    if (fs == NULL) {
        return 0;
    }
    /* Every volume's Trash at once, the set the union window lists
     * (FS-048, PVN-027); a read-only volume has none.  The Trash folders
     * stay: only what is in them is queued. */
    nd = tiku_fs_trash_dirs(fs, dirs, TIKU_VOLUME_MAX);
    for (k = 0; k < nd; k++) {
        DIR *d = opendir(dirs[k]);
        struct dirent *e;

        if (d == NULL) {
            continue;
        }
        while ((e = readdir(d)) != NULL && n < FS_QUEUE_MAX) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
                continue;
            }
            if ((int)snprintf(paths[n], sizeof paths[n], "%s/%s", dirs[k],
                              e->d_name) >= (int)sizeof paths[n]) {
                continue;               /* a name that cannot be addressed */
            }
            sel[n] = paths[n];
            n++;
        }
        (void)closedir(d);
    }
    if (n == 0) {
        return 0;
    }
    /* Deleted, not trashed: a trashing drops anything inside the Trash
     * (FS-043, FS-048). */
    (void)tiku_fs_delete(fs, sel, n);
    return n;
}

/**
 * @brief Write the path of @p target as seen from inside @p dir.
 *
 * Built by walking the two absolute paths while they agree, then one ".."
 * per component of @p dir that is left, then the rest of the target.
 *
 * @return 0 on success.
 */
static int
relative_path(const char *dir, const char *target, char *out, size_t max)
{
    size_t i = 0, cut = 0;
    size_t n = 0;

    if (dir == NULL || target == NULL || dir[0] != '/' || target[0] != '/') {
        return -1;
    }
    while (dir[i] != '\0' && target[i] != '\0' && dir[i] == target[i]) {
        if (dir[i] == '/') {
            cut = i;                /* last slash they still share */
        }
        i++;
    }
    if (dir[i] == '\0' && (target[i] == '/' || target[i] == '\0')) {
        cut = i;
    }
    if (target[i] == '\0' && (dir[i] == '/' || dir[i] == '\0')) {
        cut = i;
    }
    out[0] = '\0';
    for (i = cut; dir[i] != '\0'; i++) {
        if (dir[i] == '/' && dir[i + 1] != '\0') {
            if (n + 3u >= max) {
                return -1;
            }
            memcpy(out + n, "../", 3u);
            n += 3u;
        }
    }
    {
        const char *rest = target + cut;

        while (*rest == '/') {
            rest++;
        }
        if (n + strlen(rest) + 1u > max) {
            return -1;
        }
        memcpy(out + n, rest, strlen(rest) + 1u);
    }
    return (out[0] != '\0') ? 0 : -1;
}

/**
 * @brief Stop on a failure and put "would you like to continue?" up.
 *
 * The item is not counted yet: tiku_fs_resolve_error() counts it when the
 * question is answered.  @p can_continue is 0 for a delete failure, whose
 * question offers no Continue.
 *
 * @return 0, so the caller can `return fail_ask(...)` from a step.
 */
static int
fail_ask(tiku_fs_t *fs, const char *msg, int can_continue)
{
    snprintf(fs->prog.error, sizeof fs->prog.error, "%s", msg);
    fs->prog.error_ask = 1;
    fs->prog.error_can_continue = can_continue;
    return 0;
}

/** @brief Note that the job stopped part-way and drop the rest. */
static int
give_up(tiku_fs_t *fs)
{
    fs->prog.cancelled = 1;
    fs->qcursor = fs->qcount;
    return 1;
}

/**
 * @brief Process one queued item.
 *
 * @return 1 when the item is done or the job ends, 0 when the job stopped
 *         on a question: a collision or an error.
 */
static int
step_one(tiku_fs_t *fs)
{
    const char *src = fs->queue[fs->qcursor];
    char dstdir[TIKU_PATH_MAX], dstpath[TIKU_PATH_MAX];
    char name[TIKU_NAME_MAX];
    char err[256];

    if (cancelled(fs)) {
        return give_up(fs);
    }
    snprintf(fs->prog.current, sizeof fs->prog.current, "%s", src);
    err[0] = '\0';

    if (fs->prog.op == TIKU_OP_TRASH && tiku_fs_has_trash(fs) &&
        strstr(src, "/" TIKU_TRASH_NAME) == NULL) {
        /* The item is renamed into the Trash, and where it came from is
         * recorded for a restore.  An item already in the Trash falls
         * through to deletion. */
        char trash[TIKU_PATH_MAX], want[TIKU_NAME_MAX];

        if (tiku_fs_trash_dir(fs, src, trash, sizeof trash) != 0) {
            /* No Trash on this item's filesystem: the item is deleted
             * below, and the question put to the user was a straight
             * delete (FS-044). */
            trash[0] = '\0';
        }
        if (trash[0] != '\0') {
        /* A name already taken in the Trash gets the same " copy" treatment
         * a duplicate does, so trashing two files of one name keeps both. */
        if (exists(fs, trash, leaf_of(src))) {
            (void)tiku_fs_duplicate_name(fs, trash, leaf_of(src), want,
                                             sizeof want);
        } else {
            snprintf(want, sizeof want, "%s", leaf_of(src));
        }
        if (join(dstpath, sizeof dstpath, trash, want) != 0 ||
            rename(src, dstpath) != 0) {
            snprintf(fs->prog.error, sizeof fs->prog.error,
                     "cannot move '%.80s' to the Trash (%.60s)",
                     leaf_of(src), strerror(errno));
            fs->prog.failed++;
        } else {
            /* Recorded after the move, under the name it now has. */
            record_origin(fs, dstpath, src);
            /* Undo moves it back, as for any move. */
            undo_push(fs, dstpath, src, 0);
            landed(fs, src, dstpath);
            fs->prog.done++;
        }
        fs->qcursor++;
        return 1;
        }
    }
    if (fs->prog.op == TIKU_OP_TRASH ||
        fs->prog.op == TIKU_OP_DELETE) {
        if (on_device(fs, src)) {
            /*
             * The backend ops remove nothing, so a device node cannot be
             * trashed or deleted; unlink() on its namespace path would
             * only report ENOENT.  The refusal names the limit and can be
             * continued past, so the rest of a queue that spans a board
             * and the disk still goes.
             */
            char msg[240];

            snprintf(msg, sizeof msg,
                     "\"%.60s\" is on a device: the connection can read "
                     "and write its nodes, but not remove them.",
                     leaf_of(src));
            return fail_ask(fs, msg, 1);
        } else if (is_dir(src) && !fs->qexpanded[fs->qcursor]) {
            /* A folder goes children first: each child is queued as an
             * item of its own, then the folder again, so a child that
             * cannot go is reported by name and its siblings still go
             * (FS-049). */
            DIR *d = opendir(src);
            int before = fs->qcount;

            if (d != NULL) {
                struct dirent *e;

                while ((e = readdir(d)) != NULL &&
                       fs->qcount < FS_QUEUE_MAX - 1) {
                    if (strcmp(e->d_name, ".") == 0 ||
                        strcmp(e->d_name, "..") == 0) {
                        continue;
                    }
                    if (join(fs->queue[fs->qcount], TIKU_PATH_MAX,
                             src, e->d_name) == 0) {
                        fs->qexpanded[fs->qcount] = 0;
                        fs->qcount++;
                    }
                }
                (void)closedir(d);
            }
            if (fs->qcount > before && fs->qcount < FS_QUEUE_MAX) {
                /* The folder's second visit, after its children. */
                fs->qexpanded[fs->qcount] = 1;
                snprintf(fs->queue[fs->qcount], TIKU_PATH_MAX, "%s",
                         src);
                fs->qcount++;
                fs->prog.total = fs->qcount;
                fs->qcursor++;
                return 1;
            }
            /* Nothing inside (or no room to expand): fall through to the
             * straight attempt below by treating it as visited. */
            goto delete_attempt;
        } else {
delete_attempt:
            if (remove(src) != 0) {
                if (errno == ENOTEMPTY || errno == EEXIST) {
                    /* While a descendant is still queued, the folder goes
                     * to the back of the queue: it can only be empty after
                     * them. */
                    size_t plen = strlen(src);
                    int k;

                    for (k = fs->qcursor + 1; k < fs->qcount; k++) {
                        if (strncmp(fs->queue[k], src, plen) == 0 &&
                            fs->queue[k][plen] == '/') {
                            break;
                        }
                    }
                    if (k < fs->qcount && fs->qcount < FS_QUEUE_MAX) {
                        fs->qexpanded[fs->qcount] = 1;
                        snprintf(fs->queue[fs->qcount], TIKU_PATH_MAX,
                                 "%s", src);
                        fs->qcount++;
                        fs->prog.total = fs->qcount;
                        fs->qcursor++;
                        return 1;
                    }
                }
                /* The dialog names the leaf, which is what was
                 * selected. */
                char msg[240];

                snprintf(msg, sizeof msg,
                         "Error deleting \"%.60s\": %.150s.",
                         leaf_of(src), strerror(errno));
                return fail_ask(fs, msg, 0);
            }
            fs->prog.done++;
        }
        fs->qcursor++;
        return 1;
    }

    if (fs->prog.op == TIKU_OP_LINK) {
        char target[TIKU_PATH_MAX];
        struct stat ss, ds;

        /* "<name> link" only when the plain name is taken, so linking into
         * another folder keeps the original name (FS-009). */
        if (tiku_fs_link_name(fs, fs->dst,
                              fs->link_as[0] != '\0' ? fs->link_as
                                                      : leaf_of(src),
                              name, sizeof name) != 0 ||
            join(dstpath, sizeof dstpath, fs->dst, name) != 0) {
            fs->prog.failed++;
            fs->qcursor++;
            return 1;
        }
        snprintf(target, sizeof target, "%s", src);
        if (fs->link_relative && stat(src, &ss) == 0 &&
            stat(fs->dst, &ds) == 0 && ss.st_dev == ds.st_dev) {
            char rel[TIKU_PATH_MAX];

            /* A relative link is made only when source and destination
             * share a device and a relative path can be built; otherwise
             * the link is absolute (FS-010). */
            if (relative_path(fs->dst, src, rel, sizeof rel) == 0) {
                snprintf(target, sizeof target, "%s", rel);
            }
        }
        if (symlink(target, dstpath) != 0) {
            char msg[240];

            /* EPERM or ENOSYS means the disk takes no links, and the
             * message says so (FS-011). */
            snprintf(msg, sizeof msg, "%s",
                     (errno == EPERM || errno == ENOSYS)
                         ? "The target disk does not support creating links."
                         : "Error creating link.");
            return fail_ask(fs, msg, 1);
        }
        undo_push(fs, dstpath, src, 1);
        fs->prog.done++;
        fs->qcursor++;
        return 1;
    }
    if (fs->prog.op == TIKU_OP_DUPLICATE) {
        /* Each item duplicates into its own parent, even when the
         * selection spans folders (FS-008). */
        parent_of(src, dstdir, sizeof dstdir);
        if (tiku_fs_duplicate_name(fs, dstdir, leaf_of(src), name,
                                       sizeof name) != 0) {
            fs->prog.failed++;
            fs->qcursor++;
            return 1;
        }
    } else {
        snprintf(dstdir, sizeof dstdir, "%s", fs->dst);
        snprintf(name, sizeof name, "%s", leaf_of(src));
        {   /* An item already in the destination is skipped, not copied
             * onto itself (FS-005). */
            char srcdir[TIKU_PATH_MAX];
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
    if (fs->prog.op != TIKU_OP_DUPLICATE && exists(fs, dstdir, name)) {
        switch (fs->policy) {
        case TIKU_CONFLICT_SKIP:
            /* A policy the caller set stands for the whole job; a one-off
             * answer comes through tiku_fs_resolve(). */
            fs->prog.skipped++;
            fs->qcursor++;
            return 1;
        case TIKU_CONFLICT_RENAME:
            if (tiku_fs_duplicate_name(fs, dstdir, name, name,
                                           sizeof name) != 0 ||
                join(dstpath, sizeof dstpath, dstdir, name) != 0) {
                fs->prog.failed++;
                fs->qcursor++;
                return 1;
            }
            break;
        case TIKU_CONFLICT_REPLACE:
            if (fs->pending_ask) {
                /* A one-shot "Replace": used up by this item. */
                fs->pending_ask = 0;
                fs->policy = TIKU_CONFLICT_ASK;
            }
            /* The old file is removed first, not written over: a file
             * that cannot be removed is not written into (FS-021).  A
             * directory is merged into, not removed. */
            if (!is_dir(dstpath) && remove(dstpath) != 0) {
                char msg[240];

                snprintf(msg, sizeof msg,
                         "There was a problem trying to replace \"%.60s\". "
                         "The item might be open or busy.", name);
                return fail_ask(fs, msg, 1);
            }
            break;
        default:
            fs->prog.conflict = 1;
            snprintf(fs->prog.conflict_path, sizeof fs->prog.conflict_path,
                     "%s", dstpath);
            /* What the question offers: a folder collision always gets the
             * four-way form, and so does any collision while more than one
             * is pending; a lone file-versus-file collision gets the short
             * form (FS-018, FS-019). */
            fs->prog.conflict_isdir = is_dir(dstpath);
            fs->prog.conflict_many = (fs->prog.collisions > 1) ||
                                     fs->prog.conflict_isdir;
            return 0;               /* stop and wait for an answer */
        }
    }

    if (fs->prog.op == TIKU_OP_MOVE) {
        /* Through the tree walker, so a folder moves too, merging into a
         * same-named folder at the destination (FS-029). */
        int rc = move_tree(fs, src, dstpath, err, sizeof err);

        if (rc == -2) {
            return give_up(fs);
        }
        if (rc != 0) {
            char msg[240];

            snprintf(msg, sizeof msg, "Error moving file \"%.60s\": %.150s.",
                     leaf_of(src), err[0] ? err : "move failed");
            return fail_ask(fs, msg, 1);
        }
        undo_push(fs, dstpath, src, 0);
        landed(fs, src, dstpath);
        fs->prog.done++;
    } else {
        /* The same for a copy: a folder already there is merged into,
         * and its children with no counterpart in the source stay
         * (FS-020). */
        int rc = copy_tree(fs, src, dstpath, err, sizeof err);

        if (rc == -2) {
            /* Cancelled mid-file.  What the copy managed stays, and is
             * recorded, so an undo can still take it away (FS-034). */
            undo_push(fs, dstpath, src, 1);
            return give_up(fs);
        }
        if (rc != 0) {
            char msg[240];

            snprintf(msg, sizeof msg,
                     "Error copying file \"%.60s\": %.150s.", leaf_of(src),
                     err[0] ? err : "copy failed");
            return fail_ask(fs, msg, 1);
        }
        {
            char srcdir[TIKU_PATH_MAX];

            parent_of(src, srcdir, sizeof srcdir);
            if (strcmp(srcdir, dstdir) != 0) {
                copy_pose_info(fs, src, dstpath);
            }
        }
        undo_push(fs, dstpath, src, 1);
        fs->prog.done++;
    }
    fs->qcursor++;
    return 1;
}

/** @brief Process up to @p budget items; nothing while a question is open.
 *  @return Items processed. */
static int
fs_step(tiku_fs_t *fs, int budget)
{
    int n = 0;

    if (fs == NULL || fs->prog.conflict || fs->prog.error_ask) {
        return 0;
    }
    while (n < budget && fs->qcursor < fs->qcount) {
        if (!step_one(fs)) {
            break;                  /* stopped on a question */
        }
        n++;
    }
    if (fs->qcursor >= fs->qcount && !fs->prog.conflict) {
        fs->prog.op = (fs->prog.total > 0) ? fs->prog.op : TIKU_OP_IDLE;
    }
    return n;
}

/** @brief Answer the collision the job stopped on; see tiku_fs_resolve(). */
static void
fs_resolve(tiku_fs_t *fs, tiku_conflict_t how)
{
    if (fs == NULL || !fs->prog.conflict) {
        return;
    }
    fs->prog.conflict = 0;
    /* The two "all" answers become the job's standing policy; Skip and
     * Replace answer the collision in hand, and the next one asks again
     * (FS-019). */
    switch (how) {
    case TIKU_CONFLICT_SKIP_ALL:
        fs->policy = TIKU_CONFLICT_SKIP;
        break;
    case TIKU_CONFLICT_REPLACE_ALL:
        fs->policy = TIKU_CONFLICT_REPLACE;
        break;
    case TIKU_CONFLICT_SKIP:
        fs->prog.skipped++;
        fs->qcursor++;
        fs->policy = TIKU_CONFLICT_ASK;
        break;
    case TIKU_CONFLICT_REPLACE:
        /* Applied to this item, then back to asking. */
        fs->policy = TIKU_CONFLICT_REPLACE;
        fs->pending_ask = 1;
        break;
    default:
        fs->policy = how;
        break;
    }
}

/** @brief Stop the job; what is done stays done. */
static void
fs_cancel(tiku_fs_t *fs)
{
    if (fs != NULL) {
        /* The flag reaches a copy inside a file at its next chunk, and the
         * copy removes that file (FS-033). */
        fs->cancelling = 1;
        fs->qcursor = fs->qcount;
        fs->prog.conflict = 0;
        fs->prog.error_ask = 0;
        fs->prog.cancelled = 1;
    }
}

/** @brief Install the check asked before every chunk. */
static void
fs_set_cancel_check(tiku_fs_t *fs, tiku_cancel_fn fn,
                             void *ctx)
{
    if (fs != NULL) {
        fs->cancel_fn = fn;
        fs->cancel_ctx = ctx;
    }
}

/** @brief Answer the error the job stopped on; see
 *         tiku_fs_resolve_error(). */
static void
fs_resolve_error(tiku_fs_t *fs, int keep_going)
{
    if (fs == NULL || !fs->prog.error_ask) {
        return;
    }
    fs->prog.error_ask = 0;
    fs->prog.failed++;
    fs->qcursor++;
    if (fs->prog.op == TIKU_OP_DELETE) {
        /* A deletion goes on past a failure whatever the answer, so a
         * child that will not go costs only that child (FS-049); only a
         * cancel stops it. */
        return;
    }
    if (!keep_going || !fs->prog.error_can_continue) {
        (void)give_up(fs);
    }
}

/** @brief Rename one entry; see tiku_fs_rename(). */
static int
fs_rename(tiku_fs_t *fs, const char *path, const char *new_name,
                   char *err, size_t errmax)
{
    char dir[TIKU_PATH_MAX], dst[TIKU_PATH_MAX];

    if (fs == NULL || path == NULL || new_name == NULL) {
        return -1;
    }
    if (!tiku_fs_name_ok(new_name, err, errmax)) {
        return -1;
    }
    if (!on_device(fs, path)) {
        tiku_volumes_t vs;
        char why[200];

        if (tiku_volumes_scan(&vs) > 0) {
            /* A volume's name is its filesystem's label, which nothing
             * here can write; renaming the mount point's entry would leave
             * the label as it was.  Refused (MA-052). */
            if (tiku_volume_is_root(&vs, path)) {
                if (err != NULL) {
                    snprintf(err, errmax,
                             "\"%.40s\" is a disk: its name is the disk's "
                             "own, which cannot be changed from here.",
                             leaf_of(path));
                }
                return -1;
            }
            /* The volume must take writes; its own refusal is the
             * message. */
            if (!tiku_volume_may_write(&vs, path, 1, why, sizeof why)) {
                if (err != NULL) {
                    snprintf(err, errmax, "%s", why);
                }
                return -1;
            }
        }
    }
    {
        /* The Trash and the Desktop are found by their names: renaming
         * either strands every rule that looks there (MA-051). */
        tiku_model_t m;

        if (fs->backend->ops->stat(fs->backend, path, &m) == 0 &&
            (m.kind == TIKU_KIND_TRASH ||
             m.kind == TIKU_KIND_DESKTOP)) {
            if (err != NULL) {
                snprintf(err, errmax,
                         "The %s cannot be renamed: it is found by its "
                         "name.", (m.kind == TIKU_KIND_TRASH)
                                      ? "Trash" : "Desktop");
            }
            return -1;
        }
    }
    if (strlen(new_name) > 255u) {
        /* 255 is NAME_MAX on every host this builds on (FS-080). */
        if (err != NULL) {
            snprintf(err, errmax, "The entered name is too long.");
        }
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
    undo_reset(fs, TIKU_OP_RENAME);
    undo_push(fs, dst, path, 0);
    landed(fs, path, dst);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* undo                                                                      */
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/* New folder (FS-079)                                                       */
/*---------------------------------------------------------------------------*/

/** @brief The first free "New folder" name in @p dir; see
 *         tiku_fs_new_folder_name(). */
static void
fs_new_folder_name(tiku_fs_t *fs, const char *dir, char *out,
                            size_t max)
{
    long n;

    if (out == NULL || max == 0) {
        return;
    }
    snprintf(out, max, "New folder");
    if (fs == NULL || dir == NULL || !exists(fs, dir, out)) {
        return;
    }
    for (n = 2; n < 10000; n++) {
        /* Past nine there is no space: "New folder 9" is followed by
         * "New folder10", the names Haiku Tracker gives. */
        if (n <= 9) {
            snprintf(out, max, "New folder %ld", n);
        } else {
            snprintf(out, max, "New folder%ld", n);
        }
        if (!exists(fs, dir, out)) {
            return;
        }
    }
}

/** @brief Make a new folder in @p dir, recorded for undo. */
static int
fs_new_folder(tiku_fs_t *fs, const char *dir, char *out,
                       size_t max, char *err, size_t errmax)
{
    char name[TIKU_PATH_MAX], path[TIKU_PATH_MAX];

    if (fs == NULL || dir == NULL) {
        return -1;
    }
    tiku_fs_new_folder_name(fs, dir, name, sizeof name);
    if (join(path, sizeof path, dir, name) != 0 || mkdir(path, 0755) != 0) {
        if (err != NULL) {
            snprintf(err, errmax, "Sorry, could not create a new folder.");
        }
        return -1;
    }
    undo_reset(fs, TIKU_OP_NEW_FOLDER);
    undo_push(fs, path, NULL, 1);
    if (out != NULL) {
        snprintf(out, max, "%s", path);
    }
    return 0;
}

/** @brief Copy a template into @p dst_dir under a fresh "New ..." name; see
 *         tiku_fs_new_from_template(). */
static int
fs_new_from_template(tiku_fs_t *fs, const char *template_path,
                              const char *dst_dir, char *out, size_t max,
                              char *err, size_t errmax)
{
    struct stat st;
    char name[TIKU_NAME_MAX];
    char candidate[TIKU_NAME_MAX];
    char dst[TIKU_PATH_MAX];
    const char *base;
    int n;

    if (err != NULL && errmax > 0) {
        err[0] = '\0';
    }
    if (fs == NULL || template_path == NULL || dst_dir == NULL ||
        out == NULL || max == 0) {
        if (err != NULL) {
            snprintf(err, errmax, "template destination is unavailable");
        }
        return -1;
    }
    /* Templates are local files; a device destination is refused. */
    if (fs->backend == NULL || on_device(fs, dst_dir) ||
        lstat(template_path, &st) != 0 ||
        (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode) &&
         !S_ISLNK(st.st_mode))) {
        if (err != NULL) {
            snprintf(err, errmax, "could not read template");
        }
        return -1;
    }
    base = leaf_of(template_path);
    if (base[0] == '\0' || strlen(base) >= sizeof name - 5u) {
        if (err != NULL) {
            snprintf(err, errmax, "template name is too long");
        }
        return -1;
    }
    snprintf(name, sizeof name, "New %s", base);
    snprintf(candidate, sizeof candidate, "%s", name);
    if (exists(fs, dst_dir, candidate)) {
        for (n = 2; n < 10000; n++) {
            snprintf(candidate, sizeof candidate, "New %s %d", base, n);
            if (!exists(fs, dst_dir, candidate)) {
                break;
            }
        }
        if (n >= 10000) {
            if (err != NULL) {
                snprintf(err, errmax, "could not find a free template name");
            }
            return -1;
        }
    }
    if (join(dst, sizeof dst, dst_dir, candidate) != 0) {
        if (err != NULL) {
            snprintf(err, errmax, "template destination path is too long");
        }
        return -1;
    }
    if (copy_tree(fs, template_path, dst, err, errmax) != 0) {
        return -1;
    }
    copy_pose_info(fs, template_path, dst);
    undo_reset(fs, TIKU_OP_COPY);
    undo_push(fs, dst, NULL, 1);
    snprintf(out, max, "%s", dst);
    return 0;
}

/** @brief Whether the last operation can be undone; @p what takes its
 *         name. */
static int
fs_can_undo(const tiku_fs_t *fs, char *what, size_t max)
{
    const undo_op_t *o;

    if (fs == NULL || fs->ucount == 0) {
        return 0;
    }
    o = &fs->ustack[fs->ucount - 1];
    if (o->n == 0) {
        return 0;
    }
    /* A delete cannot be undone, so it is not offered. */
    if (o->op == TIKU_OP_DELETE) {
        return 0;
    }
    if (what != NULL) {
        snprintf(what, max, "%s", tiku_fs_op_name(o->op));
    }
    return 1;
}

/** @brief The paths the last operation put in place; see
 *         tiku_fs_last_created(). */
static int
fs_last_created(const tiku_fs_t *fs,
                         char out[][TIKU_PATH_MAX], int max)
{
    const undo_op_t *o;
    int i, n = 0;

    if (fs == NULL || out == NULL || max <= 0 || fs->ucount <= 0) {
        return 0;
    }
    o = &fs->ustack[fs->ucount - 1];
    for (i = 0; i < o->n && n < max; i++) {
        /* A path the operation put in place: made outright, or the
         * destination of a copy or a move, whose 'to' names its origin. */
        if (o->rec[i].created || o->rec[i].to[0] != '\0') {
            snprintf(out[n], TIKU_PATH_MAX, "%s", o->rec[i].from);
            n++;
        }
    }
    return n;
}

/** @brief Whether the last undo can be redone; @p what takes its name. */
static int
fs_can_redo(const tiku_fs_t *fs, char *what, size_t max)
{
    const undo_op_t *o;

    if (fs == NULL || fs->rcount == 0) {
        return 0;
    }
    o = &fs->rstack[fs->rcount - 1];
    if (o->n == 0) {
        return 0;
    }
    if (what != NULL) {
        snprintf(what, max, "%s", tiku_fs_op_name(o->op));
    }
    return 1;
}

const char *
tiku_fs_op_name(tiku_op_t op)
{
    static const char *names[] = {
        "", "Copy", "Move", "Duplicate", "Move to Trash", "Delete",
        "Rename", "Create Link", "New Folder"
    };

    if ((size_t)op >= sizeof names / sizeof names[0]) {
        return "";
    }
    return names[op];
}

/**
 * @brief Put one record back the way it was.
 *
 * @return 1 when something was undone.
 */
static int
undo_one(tiku_fs_t *fs, const undo_rec_t *r, char *err, size_t errmax)
{
    if (r->created) {
        /* Whatever the operation made goes, tree and all: a copied folder
         * holds what the copy made inside it. */
        return (remove_tree(r->from) == 0);
    }
    if (rename(r->from, r->to) == 0) {
        landed(fs, r->from, r->to);
        return 1;
    }
    if (err != NULL) {
        snprintf(err, errmax, "cannot undo '%s' (%s)", r->from,
                 strerror(errno));
    }
    return 0;
}

/**
 * @brief Do one record over again, in the direction the operation went.
 *
 * A record the operation made is copied again from where it came from,
 * except a new folder's (@p op), which is made afresh; any other record is
 * renamed again.
 */
static int
redo_one(tiku_fs_t *fs, tiku_op_t op, const undo_rec_t *r, char *err,
         size_t errmax)
{
    if (op == TIKU_OP_NEW_FOLDER) {
        return (make_path(r->from) == 0);
    }
    if (r->created) {
        if (r->to[0] == '\0') {
            return 0;               /* nothing recorded to repeat from   */
        }
        return (copy_tree(fs, r->to, r->from, err, errmax) == 0);
    }
    if (rename(r->to, r->from) == 0) {
        landed(fs, r->to, r->from);
        return 1;
    }
    if (err != NULL) {
        snprintf(err, errmax, "cannot redo '%s' (%s)", r->to,
                 strerror(errno));
    }
    return 0;
}

/** @brief Reverse the last operation, its records in reverse order.
 *  @return Records reversed, or -1 when there is nothing to undo. */
static int
fs_undo(tiku_fs_t *fs, char *err, size_t errmax)
{
    undo_op_t *o;
    int i, n = 0;

    if (!tiku_fs_can_undo(fs, NULL, 0)) {
        return -1;
    }
    o = &fs->ustack[fs->ucount - 1];
    /* Reverse order, so a sequence that renamed around a collision unwinds
     * without colliding with itself. */
    for (i = o->n - 1; i >= 0; i--) {
        n += undo_one(fs, &o->rec[i], err, errmax);
    }
    /* The operation goes to the redo stack whatever its records did: a
     * partly reversed operation can still be redone. */
    (void)undo_stack_push(fs->rstack, &fs->rcount, o);
    memset(o, 0, sizeof *o);
    fs->ucount--;
    return n;
}

/** @brief Do the last undone operation again.
 *  @return Records redone, or -1 when there is nothing to redo. */
static int
fs_redo(tiku_fs_t *fs, char *err, size_t errmax)
{
    undo_op_t *o;
    int i, n = 0;

    if (!tiku_fs_can_redo(fs, NULL, 0)) {
        return -1;
    }
    o = &fs->rstack[fs->rcount - 1];
    for (i = 0; i < o->n; i++) {
        n += redo_one(fs, o->op, &o->rec[i], err, errmax);
    }
    (void)undo_stack_push(fs->ustack, &fs->ucount, o);
    memset(o, 0, sizeof *o);
    fs->rcount--;
    return n;
}

/** @brief Where backend_text_entry() appends names. */
typedef struct {
    char  *out;
    size_t max, used;
} backend_text_ctx_t;

/** @brief tiku_backend_text()'s listing half: append one child's name and
 *         a '|'; stop when the buffer is full. */
static int
backend_text_entry(const tiku_model_t *m, void *ctx)
{
    backend_text_ctx_t *t = ctx;

    t->used += (size_t)snprintf(t->out + t->used,
                                (t->used < t->max) ? t->max - t->used : 0u,
                                "%s|", m->name);
    return (t->used < t->max) ? 0 : 1;      /* full: stop the walk */
}

int
tiku_backend_text(tiku_backend_t *b, const char *path,
                  char *out, size_t max)
{
    tiku_model_t m;

    if (out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (b == NULL || b->ops == NULL || path == NULL || path[0] != '/' ||
        b->ops->stat(b, path, &m) != 0) {
        return 0;               /* nothing serves it                    */
    }
    if (tiku_model_is_container(&m)) {
        backend_text_ctx_t t = { out, max, 0u };

        if (b->ops->list != NULL) {
            (void)b->ops->list(b, path, backend_text_entry, &t);
        }
        return 1;
    }
    if (b->ops->read != NULL) {
        int n = b->ops->read(b, path, out, max - 1u);

        if (n < 0) {
            n = 0;
        }
        if ((size_t)n >= max) {
            n = (int)max - 1;
        }
        out[n] = '\0';
    }
    return 1;
}

/*---------------------------------------------------------------------------*/
/* EVERY WAY IN TAKES THE HAND                                               */
/*---------------------------------------------------------------------------*/

tiku_fs_t *
tiku_fs_new(tiku_backend_t *backend)
{
    tiku_fs_t *fs = fs_new(backend);

    if (fs != NULL) {
        (void)pthread_mutex_init(&fs->hand, NULL);
    }
    return fs;
}

void
tiku_fs_free(tiku_fs_t *fs)
{
    if (fs != NULL) {
        (void)pthread_mutex_destroy(&fs->hand);
        fs_free(fs);
    }
}

const tiku_progress_t *
tiku_fs_progress(const tiku_fs_t *fs)
{
    tiku_fs_t *w = (tiku_fs_t *)fs;

    if (w == NULL) {
        return NULL;
    }
    /* A copy, taken under the hand: the caller reads it at leisure
     * while a worker goes on changing the original. */
    fs_take(w);
    w->prog_seen = w->prog;
    fs_give(w);
    return &w->prog_seen;
}

void
tiku_fs_set_moved(tiku_fs_t *fs, tiku_fs_moved_fn fn, void *ctx)
{
    if (fs == NULL) {
        return;
    }
    fs_take((tiku_fs_t *)fs);
    fs_set_moved(fs, fn, ctx);
    fs_give((tiku_fs_t *)fs);
}

tiku_op_t
tiku_fs_drop_action(tiku_fs_t *fs, const char *src, const char *dst_dir, int force_copy)
{
    tiku_op_t r;

    if (fs == NULL) {
        return (tiku_op_t)0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_drop_action(fs, src, dst_dir, force_copy);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_duplicate_name(tiku_fs_t *fs, const char *dir, const char *name, char *out, size_t max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_duplicate_name(fs, dir, name, out, max);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_link_name(tiku_fs_t *fs, const char *dir, const char *name, char *out, size_t max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_link_name(fs, dir, name, out, max);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_copy(tiku_fs_t *fs, const char *const *paths, int n, const char *dst_dir, tiku_conflict_t policy)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_copy(fs, paths, n, dst_dir, policy);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_move(tiku_fs_t *fs, const char *const *paths, int n, const char *dst_dir, tiku_conflict_t policy)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_move(fs, paths, n, dst_dir, policy);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_duplicate(tiku_fs_t *fs, const char *const *paths, int n)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_duplicate(fs, paths, n);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_trash(tiku_fs_t *fs, const char *const *paths, int n)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_trash(fs, paths, n);
    fs_give((tiku_fs_t *)fs);
    return r;
}

tiku_refusal_t
tiku_fs_check_move(tiku_fs_t *fs, const char *src, const char *dst_dir, char *msg, size_t max)
{
    tiku_refusal_t r;

    if (fs == NULL) {
        return (tiku_refusal_t)0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_check_move(fs, src, dst_dir, msg, max);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_delete(tiku_fs_t *fs, const char *const *paths, int n)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_delete(fs, paths, n);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_trash_dirs(tiku_fs_t *fs, char out[][TIKU_PATH_MAX], int max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_trash_dirs(fs, out, max);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_prepare_trash(tiku_fs_t *fs)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_prepare_trash(fs);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_trash_dirs_of(tiku_fs_t *fs, const struct tiku_volumes *vs, char out[][TIKU_PATH_MAX], int max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_trash_dirs_of(fs, vs, out, max);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_trash_dir(tiku_fs_t *fs, const char *path, char *out, size_t max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_trash_dir(fs, path, out, max);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_has_trash(const tiku_fs_t *fs)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_has_trash(fs);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_restore(tiku_fs_t *fs, const char *const *paths, int n, char *err, size_t errmax)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_restore(fs, paths, n, err, errmax);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_original_path(tiku_fs_t *fs, const char *path, char *out, size_t max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_original_path(fs, path, out, max);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_empty_trash(tiku_fs_t *fs)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_empty_trash(fs);
    fs_give((tiku_fs_t *)fs);
    return r;
}

tiku_delete_q_t
tiku_fs_delete_question(const tiku_fs_t *fs, const char *path, int shift_held)
{
    tiku_delete_q_t r;

    if (fs == NULL) {
        return (tiku_delete_q_t)0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_delete_question(fs, path, shift_held);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_rename(tiku_fs_t *fs, const char *path, const char *new_name, char *err, size_t errmax)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_rename(fs, path, new_name, err, errmax);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_step(tiku_fs_t *fs, int budget)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_step(fs, budget);
    fs_give((tiku_fs_t *)fs);
    return r;
}

void
tiku_fs_resolve(tiku_fs_t *fs, tiku_conflict_t how)
{
    if (fs == NULL) {
        return;
    }
    fs_take((tiku_fs_t *)fs);
    fs_resolve(fs, how);
    fs_give((tiku_fs_t *)fs);
}

void
tiku_fs_cancel(tiku_fs_t *fs)
{
    if (fs == NULL) {
        return;
    }
    fs_take((tiku_fs_t *)fs);
    fs_cancel(fs);
    fs_give((tiku_fs_t *)fs);
}

void
tiku_fs_set_cancel_check(tiku_fs_t *fs, tiku_cancel_fn fn, void *ctx)
{
    if (fs == NULL) {
        return;
    }
    fs_take((tiku_fs_t *)fs);
    fs_set_cancel_check(fs, fn, ctx);
    fs_give((tiku_fs_t *)fs);
}

void
tiku_fs_resolve_error(tiku_fs_t *fs, int keep_going)
{
    if (fs == NULL) {
        return;
    }
    fs_take((tiku_fs_t *)fs);
    fs_resolve_error(fs, keep_going);
    fs_give((tiku_fs_t *)fs);
}

int
tiku_fs_busy(const tiku_fs_t *fs)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_busy(fs);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_in_trash(tiku_fs_t *fs, const char *path)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_in_trash(fs, path);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_link(tiku_fs_t *fs, const char *const *paths, int n, const char *dst, int relative)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_link(fs, paths, n, dst, relative);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_link_as(tiku_fs_t *fs, const char *path, const char *dst,
                const char *name, int relative)
{
    int r;

    if (fs == NULL) {
        return -1;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_link_as(fs, path, dst, name, relative);
    fs_give((tiku_fs_t *)fs);
    return r;
}

void
tiku_fs_new_folder_name(tiku_fs_t *fs, const char *dir, char *out, size_t max)
{
    if (fs == NULL) {
        return;
    }
    fs_take((tiku_fs_t *)fs);
    fs_new_folder_name(fs, dir, out, max);
    fs_give((tiku_fs_t *)fs);
}

int
tiku_fs_new_folder(tiku_fs_t *fs, const char *dir, char *out, size_t max, char *err, size_t errmax)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_new_folder(fs, dir, out, max, err, errmax);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_new_from_template(tiku_fs_t *fs, const char *template_path, const char *dst_dir, char *out, size_t max, char *err, size_t errmax)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_new_from_template(fs, template_path, dst_dir, out, max, err, errmax);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_can_undo(const tiku_fs_t *fs, char *what, size_t max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_can_undo(fs, what, max);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_undo(tiku_fs_t *fs, char *err, size_t errmax)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_undo(fs, err, errmax);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_can_redo(const tiku_fs_t *fs, char *what, size_t max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_can_redo(fs, what, max);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_last_created(const tiku_fs_t *fs, char out[][TIKU_PATH_MAX], int max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_last_created(fs, out, max);
    fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_redo(tiku_fs_t *fs, char *err, size_t errmax)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fs_take((tiku_fs_t *)fs);
    r = fs_redo(fs, err, errmax);
    fs_give((tiku_fs_t *)fs);
    return r;
}

