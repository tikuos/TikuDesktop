/* TikuDiff driven without a desktop: the blank window; two files asked
 * for and compared, their rows, changed words, the change shown and the
 * way through them; the difference copied and saved; sides swapped;
 * whitespace and line endings; binary files; folders and their list, an
 * entry opened and left; a folder patch that patch(1) applies; archives
 * as folders and as one file; closing.  SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <unistd.h>
#include "../../apps/diff/tiku_diffview.c"
#include "tiku_icons.h"
#include "../fixtures/archive.inc"

static tiku_workspace_t *desk;
static char scratch[PATH_MAX];
static int asked[3];                /* the questions put, by kind        */

/** @brief The panels, as counted questions; the answer is the test's. */
static void host_pick(void *ctx, int what, const char *start,
                      const char *name)
{
    (void)ctx;
    (void)start;
    (void)name;
    asked[what]++;
}

/** @brief open_path, which TikuDiff does not use yet. */
static int host_open(void *ctx, const char *path)
{
    (void)ctx;
    (void)path;
    return 1;
}

/** @brief Every job run and landed, the window drawn each turn. */
static void settle(tiku_diffview_t *c)
{
    for (int i = 0; i < 8000; i++) {
        (void)tiku_work_collect();
        if (c) tiku_diffview_tick(c);
        if (desk) tiku_workspace_draw(desk);
        if (!tiku_work_busy() && (!c || !c->busy)) return;
        usleep(1000);
    }
    assert(!"a job never finished");
}

/** @brief @p text written to @p name under the scratch folder, its path
 *         into @p out when given. */
static void put(const char *name, const char *text, size_t len, char *out)
{
    char path[PATH_MAX];
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", scratch, name);
    f = fopen(path, "wb");
    assert(f && fwrite(text, 1, len, f) == len);
    fclose(f);
    if (out) snprintf(out, PATH_MAX, "%s", path);
}

/** @brief put() for a string. */
static void put_text(const char *name, const char *text, char *out)
{
    put(name, text, strlen(text), out);
}

/** @brief A fixture of the archive tests, decoded, written as @p name. */
static void fixture(const char *which, const char *name, char *out)
{
    static const char abc[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const char *b64 = NULL;
    char *bytes = malloc(1 << 20);
    unsigned v = 0;
    int bits = 0;
    size_t n = 0;

    for (size_t k = 0; k < sizeof fixtures / sizeof fixtures[0]; k++)
        if (!strcmp(fixtures[k].name, which)) b64 = fixtures[k].data;
    assert(b64 && bytes);
    for (; *b64 && *b64 != '='; b64++) {
        v = v << 6 | (unsigned)(strchr(abc, *b64) - abc);
        if ((bits += 6) >= 8) {
            bits -= 8;
            bytes[n++] = (char)(v >> bits);
        }
    }
    put(name, bytes, n, out);
    free(bytes);
}

/** @brief A key pressed in the viewer. */
static void press(tiku_diffview_t *c, unsigned key, unsigned mods)
{
    tiku_event_t ev = {.type = TIKU_EVENT_KEY_DOWN, .key = key,
                       .modifiers = mods};

    dv_event(c->window, &ev, c);
}

/** @brief Compare Files..., or Folders, answered with @p left and then
 *         @p right, and the comparison left to finish. */
static void compare(tiku_diffview_t *c, int folders, const char *left,
                    const char *right)
{
    int kind = folders ? TIKU_DIFFVIEW_ASK_FOLDER : TIKU_DIFFVIEW_ASK_FILE;
    int before = asked[kind];

    dv_action(c, folders ? DV_COMPARE_DIRS : DV_COMPARE);
    assert(asked[kind] == before + 1);
    tiku_diffview_picked(c, left);
    assert(asked[kind] == before + 2);      /* then the right is asked */
    tiku_diffview_picked(c, right);
    settle(c);
}

/** @brief Whether every pixel of @p r, inside its frame, is the page's. */
static int blank(const tiku_surface_t *s, tiku_rect_t r)
{
    for (int y = r.y + 1; y < r.y + r.h - 1; y++)
        for (int x = r.x + 1; x < r.x + r.w - 1; x++)
            if ((s->px[(size_t)y * s->w + x] & 0xffffffu) !=
                (TIKU_C_DOC & 0xffffffu))
                return 0;
    return 1;
}

/** @brief Nothing compared: the window blank, nothing said, nothing to
 *         go to or write. */
static void empty_tests(tiku_diffview_t *c, const tiku_surface_t *s)
{
    dv_layout_t l = dv_layout(c, tiku_window_content(c->window));

    tiku_workspace_draw(desk);
    assert(c->mode == DV_EMPTY && !c->status[0] && blank(s, l.body));
    assert(!dv_enabled(c, DV_NEXT) && !dv_enabled(c, DV_COPY) &&
           !dv_enabled(c, DV_SWAP) && dv_enabled(c, DV_COMPARE));
}

/** @brief The rows two small C files make, the change shown, the words a
 *         changed line changed, and the way from change to change. */
static void file_tests(tiku_diffview_t *c, const char *a, const char *b)
{
    const dv_files_t *f;
    long rng[2][2 * DV_RANGES];
    int nrng[2];

    compare(c, 0, a, b);
    f = c->files;
    assert(c->mode == DV_FILES && f && !f->binary && !f->same);
    assert(f->d.removed == 2 && f->d.added == 4 && f->nchange == 3);
    assert(c->cur == 0 && strstr(c->status, "2 lines removed, 4 added"));
    assert(strstr(c->window->title, "TikuDiff - one.c : two.c"));
    /* The first change: "int x = 1;" became "int x = 2;". */
    assert(f->row[f->change[0]].kind == DV_ROW_CHANGE);
    dv_words(f, &f->row[f->change[0]], rng, nrng);
    assert(nrng[0] == 1 && nrng[1] == 1 &&
           !strncmp(f->line[0][f->row[f->change[0]].a].text + rng[0][0], "1",
                    (size_t)(rng[0][1] - rng[0][0])));
    press(c, ']', TIKU_MOD_CMD);
    assert(c->cur == 1 && f->row[f->change[1]].kind == DV_ROW_ADD);
    press(c, ']', TIKU_MOD_CMD);
    assert(c->cur == 2 && f->row[f->change[2]].kind == DV_ROW_DEL);
    assert(!dv_enabled(c, DV_NEXT));
    press(c, '[', TIKU_MOD_CMD);
    assert(c->cur == 1);
    dv_pick_change(c, 0);
    assert(c->cur == 0);
}

/** @brief The difference as unified text: on the clipboard, and in a file
 *         the Save panel names. */
static void output_tests(tiku_diffview_t *c)
{
    char path[PATH_MAX], text[4096];
    FILE *f;
    size_t n;

    press(c, 'c', TIKU_MOD_CMD);
    assert(strstr(tiku_clipboard_text(), "@@ -1,") &&
           strstr(tiku_clipboard_text(), "-int x = 1;\n+int x = 2;\n"));
    dv_action(c, DV_SAVE);
    assert(asked[TIKU_DIFFVIEW_ASK_SAVE] == 1);
    snprintf(path, sizeof path, "%s/changes.diff", scratch);
    tiku_diffview_picked(c, path);
    f = fopen(path, "rb");
    assert(f);
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = '\0';
    assert(!strcmp(text, tiku_clipboard_text()) && strstr(c->status, "in "));
}

/** @brief Sides turned round; whitespace; line endings; binary files; a
 *         file against a folder refused. */
static void option_tests(tiku_diffview_t *c, const char *b)
{
    char x[PATH_MAX], y[PATH_MAX];

    dv_action(c, DV_SWAP);
    settle(c);
    assert(c->files->d.removed == 4 && c->files->d.added == 2 &&
           !strcmp(c->side[0].path, b));
    put_text("spaced.txt", "a  b\nc\n", x);
    put_text("tight.txt", "a b \nc\n", y);
    compare(c, 0, x, y);
    assert(!c->files->same);
    dv_action(c, DV_SPACE);
    settle(c);
    assert(c->space && c->files->same && strstr(c->status, "whitespace"));
    dv_action(c, DV_SPACE);
    settle(c);
    put_text("crlf.txt", "one\r\ntwo\r\n", x);
    put_text("lf.txt", "one\ntwo\n", y);
    compare(c, 0, x, y);
    assert(c->files->same && c->files->endings &&
           strstr(c->status, "line endings"));
    put("one.bin", "ab\0cd", 5, x);
    put("two.bin", "ab\0ce", 5, y);
    compare(c, 0, x, y);
    assert(c->files->binary && !c->files->same &&
           strstr(c->status, "Binary files differ") && !dv_enabled(c, DV_COPY));
    compare(c, 0, x, scratch);
    assert(c->files && c->files->binary && strstr(c->status, "A folder can"));
}

/** @brief Two folders: their list, the same files hidden until asked
 *         for, an entry opened as files and left again. */
static void folder_tests(tiku_diffview_t *c, const char *left,
                         const char *right)
{
    const dv_folders_t *d;

    compare(c, 1, left, right);
    d = c->folders;
    assert(c->mode == DV_FOLDERS && d && d->n == 4);
    assert(d->count[DV_ENT_SAME] == 1 && d->count[DV_ENT_CHANGED] == 1 &&
           d->count[DV_ENT_LEFT] == 1 && d->count[DV_ENT_RIGHT] == 1);
    assert(c->nshown == 3 && strstr(c->status, "1 changed"));
    dv_action(c, DV_SHOW_SAME);
    assert(c->nshown == 4);
    dv_action(c, DV_SHOW_SAME);
    for (c->sel = 0; c->sel < c->nshown; c->sel++)
        if (d->e[c->shown[c->sel]].status == DV_ENT_CHANGED) break;
    press(c, TIKU_KEY_RETURN, 0);
    settle(c);
    assert(c->mode == DV_FILES && c->from_folder && c->files &&
           c->files->d.added == 1 && strstr(c->window->title, "notes.txt"));
    press(c, TIKU_KEY_ESCAPE, 0);
    assert(c->mode == DV_FOLDERS && !c->files && c->folders == d);
}

/** @brief The folder list's differences as one patch, which patch(1),
 *         where there is one, turns the left folder into the right. */
static void patch_tests(tiku_diffview_t *c, const char *left,
                        const char *right)
{
    char cmd[4 * PATH_MAX], copy[PATH_MAX], pd[PATH_MAX];
    FILE *f;

    press(c, 'c', TIKU_MOD_CMD);
    settle(c);
    assert(strstr(tiku_clipboard_text(), "--- a/sub/notes.txt\n") &&
           strstr(tiku_clipboard_text(), "+++ /dev/null\n") &&
           strstr(tiku_clipboard_text(), "--- /dev/null\n"));
    if (system("command -v patch > /dev/null 2>&1") != 0) return;
    snprintf(copy, sizeof copy, "%s/patched", scratch);
    snprintf(pd, sizeof pd, "%s/p.diff", scratch);
    f = fopen(pd, "wb");
    assert(f && fputs(tiku_clipboard_text(), f) >= 0);
    fclose(f);
    /* A patch(1) that empties a file it is told to delete, rather than
     * removing it, is still right: empty files are cleared before the
     * two folders are held side by side. */
    snprintf(cmd, sizeof cmd, "cp -R '%s' '%s' && cd '%s' && patch -s -p1 "
             "< '%s' > /dev/null && find . -type f -empty -exec rm {} + && "
             "diff -r '%s' '%s' > /dev/null", left, copy, copy, pd, copy,
             right);
    assert(system(cmd) == 0);
}

/** @brief Archives: a tar.gz against itself holds nothing different, and
 *         against a pax tar nothing alike; a gzip's one file is a file. */
static void archive_tests(tiku_diffview_t *c)
{
    char x[PATH_MAX], y[PATH_MAX];

    fixture("tgz", "pkg-1.tar.gz", x);
    fixture("tgz", "pkg-2.tgz", y);
    compare(c, 0, x, y);
    assert(c->mode == DV_FOLDERS && c->side[0].archive &&
           c->folders->count[DV_ENT_SAME] == 2 && c->nshown == 0);
    fixture("tar_pax", "other.tar", y);
    compare(c, 0, x, y);
    assert(c->mode == DV_FOLDERS && c->folders->count[DV_ENT_SAME] == 0 &&
           c->folders->count[DV_ENT_LEFT] == 2);
    fixture("gz_fields", "notes.gz", x);
    put_text("plain.txt", "plain\n", y);
    compare(c, 0, x, y);
    assert(c->mode == DV_FILES && c->side[0].archive && !c->side[0].dir &&
           c->files && !c->files->binary && !c->files->same);
}

/** @brief A small C file, and the same changed three ways. */
static void c_files(char *a, char *b)
{
    put_text("one.c", "int x = 1;\nint y;\n\nvoid f(void)\n{\n}\n"
             "int gone;\nint z;\n", a);
    put_text("two.c", "int x = 2;\nint y;\n\nvoid g(void);\nvoid h(void);\n"
             "\nvoid f(void)\n{\n}\nint z;\n", b);
}

/** @brief Two folders: one file the same, one changed, one on each side
 *         alone, one down a folder. */
static void folders(char *left, char *right)
{
    char path[PATH_MAX];

    snprintf(left, PATH_MAX, "%s/L", scratch);
    snprintf(right, PATH_MAX, "%s/R", scratch);
    assert(!mkdir(left, 0755) && !mkdir(right, 0755));
    snprintf(path, sizeof path, "%s/sub", left);
    assert(!mkdir(path, 0755));
    snprintf(path, sizeof path, "%s/sub", right);
    assert(!mkdir(path, 0755));
    put_text("L/same.txt", "same\n", NULL);
    put_text("R/same.txt", "same\n", NULL);
    put_text("L/notes.txt", "one\ntwo\n", NULL);
    put_text("R/notes.txt", "one\ntwo\nthree\n", NULL);
    put_text("L/sub/notes.txt", "only on the left\n", NULL);
    put_text("R/new.txt", "only on the right\n", NULL);
}

/** @brief Every group, in order, on one window; a picture when asked. */
int main(int argc, char **argv)
{
    int dirty = 0;
    tiku_diffview_host_t host = { host_pick, host_open, NULL };
    char a[PATH_MAX], b[PATH_MAX], left[PATH_MAX], right[PATH_MAX];
    char temp[PATH_MAX];

    snprintf(scratch, sizeof scratch, "/tmp/tikudiff-test-XXXXXX");
    assert(mkdtemp(scratch));
    c_files(a, b);
    folders(left, right);
    tiku_surface_t *s = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    desk = tiku_workspace_new(s);
    tiku_diffview_t *c = tiku_diffview_new(desk, &dirty, &host);
    assert(c);
    tiku_diffview_open(c);
    empty_tests(c, s);
    file_tests(c, a, b);
    tiku_workspace_draw(desk);
    if (argc > 1) assert(tiku_surface_png(s, argv[1]) == 0);
    output_tests(c);
    option_tests(c, b);
    folder_tests(c, left, right);
    patch_tests(c, left, right);
    archive_tests(c);
    snprintf(temp, sizeof temp, "%s", c->temp);
    assert(temp[0] && !access(temp, F_OK));
    tiku_diffview_destroy(c);
    settle(NULL);
    assert(access(temp, F_OK) != 0);     /* what archives made, gone */
    tiku_workspace_free(desk);
    tiku_surface_free(s);
    snprintf(temp, sizeof temp, "rm -rf '%s'", scratch);
    (void)system(temp);
    puts("TikuDiff: blank, files, words, changes, copy and save, swap, "
         "whitespace, endings, binary, folders, patch, archives PASS");
    return 0;
}
