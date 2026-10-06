/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_mount.c - one namespace over several backends.
 *
 * The router is tested against FAKE backends rather than against POSIX
 * and a board: what is being proven is the routing, and a fake can be
 * asked what path it was handed -- which is the whole question, and the
 * one a real filesystem answers only by succeeding or failing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_fs.h"
#include "tiku_mount.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/*---------------------------------------------------------------------------*/
/* A backend that only remembers what it was asked                           */
/*---------------------------------------------------------------------------*/

typedef struct {
    tiku_backend_t base;
    char           seen[TIKU_PATH_MAX];   /* the last path handed in */
    int            calls;
    uint64_t       id;                    /* what it calls its nodes */
    const char    *kid;                   /* the one child it lists  */
    /*
     * The paths it HAS.  A backend that answers for everything is not a
     * backend, and a fake that does hides the case the router exists
     * for: /devices is a directory precisely because the local files
     * say they have never heard of it.
     */
    const char    *knows[4];
} fake_t;

static int
fake_has(const fake_t *f, const char *path)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (f->knows[i] != NULL && strcmp(f->knows[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

static fake_t *fake_of(tiku_backend_t *b) { return (fake_t *)b; }

static int
fake_stat(tiku_backend_t *b, const char *path, tiku_model_t *out)
{
    fake_t *f = fake_of(b);

    snprintf(f->seen, sizeof f->seen, "%s", path);
    f->calls++;
    if (!fake_has(f, path)) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    snprintf(out->path, sizeof out->path, "%s", path);
    {   /* Its own name for it: "/" for its root, as a device would. */
        const char *leaf = strrchr(path, '/');

        snprintf(out->name, sizeof out->name, "%s",
                 (leaf != NULL && leaf[1] != '\0') ? leaf + 1 : "/");
    }
    out->kind = TIKU_KIND_DIRECTORY;
    out->node_id = f->id;
    out->backend = b;
    return 0;
}

static int
fake_list(tiku_backend_t *b, const char *path, tiku_entry_fn fn, void *ctx)
{
    fake_t *f = fake_of(b);
    tiku_model_t m;

    snprintf(f->seen, sizeof f->seen, "%s", path);
    f->calls++;
    if (!fake_has(f, path)) {
        return -1;
    }
    if (f->kid == NULL) {
        return 0;
    }
    memset(&m, 0, sizeof m);
    if (path[1] == '\0') {
        snprintf(m.path, sizeof m.path, "/%s", f->kid);
    } else {
        snprintf(m.path, sizeof m.path, "%s/%s", path, f->kid);
    }
    snprintf(m.name, sizeof m.name, "%s", f->kid);
    m.kind = TIKU_KIND_FILE;
    m.node_id = f->id;
    m.backend = b;
    return fn(&m, ctx) == 0 ? 1 : 1;
}

static int
fake_read(tiku_backend_t *b, const char *path, void *buf, size_t max)
{
    fake_t *f = fake_of(b);

    snprintf(f->seen, sizeof f->seen, "%s", path);
    (void)buf;
    (void)max;
    return 7;
}

static int
fake_write(tiku_backend_t *b, const char *path, const void *buf, size_t len,
           char *err, size_t errmax)
{
    fake_t *f = fake_of(b);

    snprintf(f->seen, sizeof f->seen, "%s", path);
    (void)buf; (void)len; (void)err; (void)errmax;
    return 0;
}

static int
fake_changes(tiku_backend_t *b, tiku_model_change_t *out, int max,
             int *complete)
{
    fake_t *f = fake_of(b);

    if (max < 1) {
        return 0;
    }
    (void)complete;
    out[0].id = f->id;
    out[0].op = TIKU_CH_CHANGED;
    return 1;
}

static int
fake_path_of_id(tiku_backend_t *b, uint64_t id, char *out, size_t max)
{
    fake_t *f = fake_of(b);

    if (id != f->id) {
        return -1;
    }
    snprintf(out, max, "%s", "/found");
    return 0;
}

static const tiku_backend_ops_t fake_ops = {
    "fake", fake_stat, fake_list, fake_read, fake_write, NULL,
    fake_changes, fake_path_of_id, NULL, NULL, NULL, NULL
};

/* A store with no change ring, which is what a local filesystem is: the
 * NULL is not an omission, it is the answer "ask me again, I cannot
 * tell you". */
static const tiku_backend_ops_t mute_ops = {
    "mute", fake_stat, fake_list, fake_read, fake_write, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL
};

static void
fake_init(fake_t *f, uint64_t id, const char *kid, const char *a,
          const char *b)
{
    memset(f, 0, sizeof *f);
    f->base.ops = &fake_ops;
    /*
     * Its own state, as every real backend's impl is.  Left NULL, this
     * fake would be safe to mistake for a namespace by accident -- and
     * the check below that a plain store is NOT one would pass however
     * the router decided it, which is no check at all.
     */
    f->base.impl = f;
    f->id = id;
    f->kid = kid;
    f->knows[0] = "/";
    f->knows[1] = a;
    f->knows[2] = b;
}

/*---------------------------------------------------------------------------*/

struct seen_list {
    char name[8][TIKU_NAME_MAX];
    char path[8][TIKU_PATH_MAX];
    uint64_t id[8];
    tiku_backend_t *owner[8];
    int n;
};

static int
collect(const tiku_model_t *m, void *ctx)
{
    struct seen_list *s = ctx;

    if (s->n < 8) {
        snprintf(s->name[s->n], sizeof s->name[0], "%s", m->name);
        snprintf(s->path[s->n], sizeof s->path[0], "%s", m->path);
        s->id[s->n] = m->node_id;
        s->owner[s->n] = m->backend;
        s->n++;
    }
    return 0;
}

static int
has_name(const struct seen_list *s, const char *want)
{
    int i;

    for (i = 0; i < s->n; i++) {
        if (strcmp(s->name[i], want) == 0) {
            return 1;
        }
    }
    return 0;
}

int
main(void)
{
    tiku_backend_t *r = tiku_mount_open();
    fake_t root, one, two;
    tiku_model_t m;
    char local[TIKU_PATH_MAX];

    setvbuf(stdout, NULL, _IOLBF, 0);
    fake_init(&root, 1000, "home", "/home", "/etc");
    /* The SAME id as root's, on purpose. */
    fake_init(&one, 1000, "mem", "/sys", "/mem");
    fake_init(&two, 2000, "led", "/led", NULL);

    if (r == NULL) {
        printf("[FAIL] no namespace\n");
        return 1;
    }

    printf("what a namespace is made of (TS-MOUNT)\n");
    check(tiku_mount_add(r, "/", &root.base, 0) == 0, "TS-MOUNT",
          "the local files go at the root");
    check(tiku_mount_add(r, "/devices/board1", &one.base, 0) == 0,
          "TS-MOUNT", "and a device under a name of its own");
    check(tiku_mount_add(r, "/devices/board1", &two.base, 0) == -1,
          "TS-MOUNT",
          "a prefix already taken is refused rather than shadowed: two "
          "backends at one name is a question with two answers");
    check(tiku_mount_add(r, "devices/board2", &two.base, 0) == -1 &&
          tiku_mount_add(r, "/devices/board2/", &two.base, 0) == -1,
          "TS-MOUNT",
          "and a prefix that is relative or ends in a slash is refused, "
          "because every routing decision below assumes it did not");
    check(tiku_mount_add(r, "/devices/board2", &two.base, 0) == 0,
          "TS-MOUNT", "a second device sits beside the first");
    check(tiku_mount_count(r) == 3, "TS-MOUNT", "three mounts, then");

    printf("\nthe longest prefix wins (TS-MOUNT)\n");
    check(tiku_mount_route(r, "/etc/passwd", local, sizeof local) ==
          &root.base && strcmp(local, "/etc/passwd") == 0, "TS-MOUNT",
          "an ordinary path goes to the root, named as it always was");
    check(tiku_mount_route(r, "/devices/board1/sys/mem", local,
                           sizeof local) == &one.base &&
          strcmp(local, "/sys/mem") == 0, "TS-MOUNT",
          "a device path goes to the device, with the prefix taken OFF: "
          "the board has never heard of the name it is mounted under");
    check(tiku_mount_route(r, "/devices/board1", local, sizeof local) ==
          &one.base && strcmp(local, "/") == 0, "TS-MOUNT",
          "and the mount point itself is that backend's own root");
    check(tiku_mount_route(r, "/devices/board10/x", local, sizeof local) ==
          &root.base, "TS-MOUNT",
          "board10 is not board1: a prefix has to end on a component "
          "boundary or every device shadows the ones named after it");

    printf("\nand answers come back in the mounted namespace (TS-MOUNT)\n");
    check(tiku_mount_route(r, "/devices/board1/sys", NULL, 0) != NULL &&
          r->ops->stat(r, "/devices/board1/sys", &m) == 0, "TS-MOUNT",
          "a device node stats");
    check(strcmp(one.seen, "/sys") == 0, "TS-MOUNT",
          "the device was asked about /sys, which is what IT calls it");
    check(strcmp(m.path, "/devices/board1/sys") == 0, "TS-MOUNT",
          "and the answer says /devices/board1/sys, which is what the "
          "CALLER must be able to hand back");
    check(m.backend == r, "TS-MOUNT",
          "owned by the namespace, not by the device: a model keeps its "
          "backend, and the next call made with it carries a mounted "
          "path that only the namespace can resolve");

    printf("\ntwo backends may call two nodes by one name (TS-MOUNT)\n");
    {
        tiku_model_t a, b;

        (void)r->ops->stat(r, "/etc", &a);
        (void)r->ops->stat(r, "/devices/board1/sys", &b);
        check(a.node_id != b.node_id, "TS-MOUNT",
              "both backends said node 1000 and the two ids differ: a "
              "change notice names ONE node, where an untagged id would "
              "have moved a row on the wrong mount");
        check((a.node_id & 0x00FFFFFFFFFFFFFFull) ==
              (b.node_id & 0x00FFFFFFFFFFFFFFull), "TS-MOUNT",
              "and what the backend called it is still in there, so the "
              "tag is a label rather than a rewrite");
    }

    printf("\nthe ancestors of a mount exist (TS-MOUNT)\n");
    check(r->ops->stat(r, "/devices", &m) == 0 &&
          m.kind == TIKU_KIND_DIRECTORY, "TS-MOUNT",
          "/devices is a directory although nothing serves it: it is "
          "there because something is mounted under it");
    check(strcmp(m.path, "/devices") == 0 && m.backend == r, "TS-MOUNT",
          "and it belongs to the namespace itself");
    {
        struct seen_list s;

        memset(&s, 0, sizeof s);
        check(r->ops->list(r, "/devices", collect, &s) == 2, "TS-MOUNT",
              "and listing it gives the two devices");
        check(has_name(&s, "board1") && has_name(&s, "board2") &&
              s.n == 2, "TS-MOUNT",
              "each asked of the device itself, so a board shows as what "
              "it IS rather than as an empty folder with its name");
    }
    printf("\nand the root holds both worlds at once (TS-MOUNT)\n");
    {
        struct seen_list s;

        memset(&s, 0, sizeof s);
        (void)r->ops->list(r, "/", collect, &s);
        check(has_name(&s, "home"), "TS-MOUNT",
              "what the local root holds is still in the root");
        check(has_name(&s, "devices"), "TS-MOUNT",
              "and /devices is beside it, offered ONCE though two "
              "devices put it there");
        check(s.n == 2, "TS-MOUNT",
              "which is the whole root: one local child and one shared "
              "ancestor, not one row per device at the top level");
    }

    printf("\na change notice can be traced home (TS-MOUNT)\n");
    {
        tiku_model_change_t ch[8];
        int complete = 0, n;
        char path[TIKU_PATH_MAX];

        n = r->ops->changes(r, ch, 8, &complete);
        check(n == 3, "TS-MOUNT", "every mount is asked, and all answered");
        check(r->ops->path_of_id(r, ch[1].id, path, sizeof path) == 0,
              "TS-MOUNT", "and an id says which mount it came from");
        check(strncmp(path, "/devices/", 9) == 0, "TS-MOUNT",
              "so the path it resolves to is the MOUNTED one, which is "
              "the only name the view above ever had");
    }

    printf("\nand a device can leave (TS-MOUNT)\n");
    check(tiku_mount_remove(r, "/devices/board2") == 0 &&
          tiku_mount_count(r) == 2, "TS-MOUNT",
          "unmounting takes it out of the namespace");
    check(tiku_mount_route(r, "/devices/board2/led", local,
                           sizeof local) == &root.base, "TS-MOUNT",
          "and its paths stop being its own");
    {
        /* The slot is emptied, not closed up: a third device takes the
         * free slot, and the ids already issued for board1 keep meaning
         * board1. */
        tiku_model_t before, after;
        fake_t three;

        (void)r->ops->stat(r, "/devices/board1/sys", &before);
        fake_init(&three, 3000, "x", "/x", NULL);
        check(tiku_mount_add(r, "/devices/board3", &three.base, 0) == 0,
              "TS-MOUNT", "a new device takes the empty slot");
        (void)r->ops->stat(r, "/devices/board1/sys", &after);
        check(before.node_id == after.node_id, "TS-MOUNT",
              "and board1's identities do not shift under it: the mount "
              "number is in every id already handed out, so a table that "
              "closed its gaps would rename other people's nodes");
    }
    check(tiku_mount_remove(r, "/devices/nothing") == -1, "TS-MOUNT",
          "unmounting what is not there says so");

    printf("\nnothing is mounted at all (TS-MOUNT)\n");
    {
        tiku_backend_t *empty = tiku_mount_open();
        tiku_model_t junk;

        check(empty != NULL &&
              empty->ops->stat(empty, "/anything", &junk) == -1 &&
              empty->ops->read(empty, "/anything", local, 4) == -1,
              "TS-MOUNT",
              "an empty namespace answers nothing rather than reaching "
              "through a null backend");
        if (empty != NULL) {
            empty->ops->close(empty);
        }
    }

    printf("\none store that cannot tell silences the whole namespace\n");
    {
        /*
         * A local filesystem has no change ring and says so with a NULL
         * op; a device has one and answers.  Put them in one namespace
         * and the router must report "cannot tell" for BOTH -- because a
         * caller cannot poll one mount and trust notices from another.
         *
         * This was a live regression: the router SKIPPED a store with no
         * ring instead of taking its silence as the answer, so with a
         * POSIX root beside one board it said "nothing changed, and that
         * is complete" -- and every local window stopped relisting the
         * moment a device was mounted.
         */
        tiku_backend_t *ns2 = tiku_mount_open();
        fake_t local, board;
        tiku_model_change_t ch2[8];
        int complete2 = 1;

        fake_init(&local, 500, "home", "/home", NULL);
        fake_init(&board, 600, "mem", "/sys", NULL);
        local.base.ops = &mute_ops;          /* no ring, like a disk */

        check(ns2 != NULL &&
              tiku_mount_add(ns2, "/", &local.base, 0) == 0 &&
              ns2->ops->changes(ns2, ch2, 8, &complete2) == -1,
              "TS-MOUNT",
              "a namespace holding only a store that cannot tell says it "
              "cannot tell");
        check(tiku_mount_add(ns2, "/devices/b", &board.base, 0) == 0 &&
              ns2->ops->changes(ns2, ch2, 8, &complete2) == -1,
              "TS-MOUNT",
              "and mounting one that CAN does not change that: the "
              "silence of the first is an answer about the whole "
              "namespace, not a mount to step over");
        if (ns2 != NULL) {
            ns2->ops->close(ns2);
        }
    }

    printf("\nand a plain backend is not one of these (TS-MOUNT)\n");
    {
        /*
         * EVERY backend has an impl pointer and it is its own private
         * state, so "impl is not null" tells a namespace from a store
         * not at all -- it would hand a POSIX store's struct back as a
         * mount table and read whatever lay at those offsets.  The test
         * is the ops table, and these are the calls that would have gone
         * wandering without it.
         */
        char l[TIKU_PATH_MAX];

        check(!tiku_mount_is(&root.base) &&
              tiku_mount_slots(&root.base) == 0 &&
              tiku_mount_count(&root.base) == 0 &&
              tiku_mount_prefix_at(&root.base, 0) == NULL &&
              tiku_mount_at(&root.base, 0) == NULL &&
              tiku_mount_route(&root.base, "/x", l, sizeof l) == NULL,
              "TS-MOUNT",
              "asked the questions a namespace answers, a plain store "
              "says it is not one rather than reading its own state as "
              "a mount table");
        check(tiku_backend_serving(&root.base, "/x", l, sizeof l) ==
              &root.base && strcmp(l, "/x") == 0, "TS-MOUNT",
              "and it answers for itself, by the name it was given -- "
              "which is what lets a caller ask without knowing whether "
              "there is a namespace in the way at all");
    }

    printf("\nthe namespace is a noun table (TS-MOUNT)\n");
    {
        char text[256];

        check(tiku_backend_text(r, "/devices/board1", text,
                                sizeof text) == 1 &&
              strstr(text, "mem|") != NULL, "TS-MOUNT",
              "a mounted device's root reads as its children by name -- "
              "the device idiom as a text answer");
        check(tiku_backend_text(r, "/devices/board9/x", text,
                                sizeof text) == 0 && text[0] == '\0',
              "TS-MOUNT",
              "a path nothing serves is refused and reads as nothing, "
              "never as something");
    }

    r->ops->close(r);
    printf("\n%s\n", fails ? "FAILURES" : "the namespace routes");
    return fails ? 1 : 0;
}
