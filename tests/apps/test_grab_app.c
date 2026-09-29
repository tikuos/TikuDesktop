/* TikuGrab driven without a desktop, a fake one answering its messages:
 * the blank window and the list it asks for; a window chosen and grabbed;
 * the picture shown, transparency over a checkerboard; Open, Show and
 * Save As; the delay counted down; a refusal said.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <unistd.h>
#include "../../apps/grab/tiku_grabview.c"

static tiku_workspace_t *desk;
static tiku_msg_t *asked;           /* the last request to the desktop */
static int requests, saves;
static char opened[TIKU_PATH_MAX];

/** @brief The desktop's service, standing in: each request kept. */
static int host_send(void *ctx, const struct tiku_msg *m)
{
    (void)ctx;
    tiku_msg_free(asked);
    asked = tiku_msg_copy(m);
    requests++;
    return 1;
}

/** @brief The Save panel, as a counted question. */
static void host_save(void *ctx, const char *start, const char *name)
{
    (void)ctx;
    (void)start;
    assert(name && !strncmp(name, "tiku-window-", 12));
    saves++;
}

/** @brief open_path, remembered. */
static int host_open(void *ctx, const char *path)
{
    (void)ctx;
    snprintf(opened, sizeof opened, "%s", path);
    return 1;
}

/** @brief What the last request asked for, TIKU_GRAB_*. */
static int32_t last_what(void)
{
    int32_t what = -1;

    assert(asked && tiku_msg_what(asked) == TIKU_MSG_GRAB);
    (void)tiku_msg_find_int32(asked, "what", 0, &what);
    return what;
}

/** @brief The desktop's list: two windows. */
static void answer_list(tiku_grabview_t *c)
{
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_GRABBED);

    assert(m && tiku_msg_add_int32(m, "list", 1));
    assert(tiku_msg_add_string(m, "title", "Tracker") &&
           tiku_msg_add_int64(m, "id", 5));
    assert(tiku_msg_add_string(m, "title", "Notes") &&
           tiku_msg_add_int64(m, "id", 3));
    tiku_grabview_message(c, m);
    tiku_msg_free(m);
}

/** @brief A key pressed in the window. */
static void press(tiku_grabview_t *c, unsigned key, unsigned mods)
{
    tiku_event_t ev = {.type = TIKU_EVENT_KEY_DOWN, .key = key,
                       .modifiers = mods};

    gb_event(c->window, &ev, c);
}

/** @brief The surface's pixel at @p x, @p y, its colour alone. */
static uint32_t at(const tiku_surface_t *s, int x, int y)
{
    return s->px[(size_t)y * s->w + x] & 0xffffffu;
}

/** @brief Before anything is grabbed: the list asked for, the preview
 *         blank, nothing said, nothing to open or save. */
static void empty_tests(tiku_grabview_t *c, const tiku_surface_t *s)
{
    gb_layout_t l = gb_layout(c, tiku_window_content(c->window));

    assert(requests == 1 && last_what() == TIKU_GRAB_LIST);
    tiku_workspace_draw(desk);
    assert(!c->status[0] && at(s, l.view.x + l.view.w / 2,
                              l.view.y + l.view.h / 2) ==
                            (TIKU_C_DOC & 0xffffffu));
    assert(!gb_enabled(c, GB_OPEN) && !gb_enabled(c, GB_SAVE));
}

/** @brief The list answered, a window chosen with the keys and grabbed:
 *         the request names it, and Grab waits for the answer. */
static void grab_tests(tiku_grabview_t *c)
{
    int64_t id = 0;

    answer_list(c);
    assert(c->ntarget == 3 && !strcmp(c->target[2].title, "Notes"));
    press(c, TIKU_KEY_DOWN, 0);
    press(c, TIKU_KEY_DOWN, 0);
    assert(c->sel == 2);
    press(c, 'g', TIKU_MOD_CMD);
    assert(last_what() == TIKU_GRAB_WINDOW && c->waiting &&
           !gb_enabled(c, GB_GRAB));
    assert(tiku_msg_find_int64(asked, "id", 0, &id) && id == 3);
}

/** @brief The desktop's answer, a picture with a transparent corner at
 *         @p shot: shown, the corner over the checkerboard, said. */
static void picture_tests(tiku_grabview_t *c, const tiku_surface_t *s,
                          const char *shot)
{
    gb_layout_t l = gb_layout(c, tiku_window_content(c->window));
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_GRABBED);
    static uint32_t px[40 * 30];
    int x0, y0;

    for (int i = 0; i < 40 * 30; i++)
        px[i] = (i % 40 < 6 && i / 40 < 4) ? 0 : 0xff2080d0u;
    assert(tiku_png_write(shot, px, 40, 30, 1) == 0);
    assert(m && tiku_msg_add_string(m, "path", shot) &&
           tiku_msg_add_int32(m, "w", 40) && tiku_msg_add_int32(m, "h", 30) &&
           tiku_msg_add_int64(m, "size", 321));
    tiku_grabview_message(c, m);
    tiku_msg_free(m);
    assert(!c->waiting && c->shot.w == 40 && strstr(c->status, "Saved as "
           "tiku-window-Notes-1.png, 321 B"));
    assert(last_what() == TIKU_GRAB_LIST);  /* the titles asked for again */
    tiku_workspace_draw(desk);
    x0 = l.view.x + 8 + (l.view.w - 16 - 40) / 2;
    y0 = l.view.y + 8 + (l.view.h - 16 - 30) / 2;
    assert(at(s, x0 + 20, y0 + 20) == 0x2080d0u);
    assert(at(s, x0 + 1, y0 + 1) == 0xffffffu ||
           at(s, x0 + 1, y0 + 1) == 0xe4e4e4u);   /* the checkerboard */
}

/** @brief Open, Show and Save As, on the picture at @p shot in @p dir. */
static void output_tests(tiku_grabview_t *c, const char *dir,
                         const char *shot)
{
    char copy[256];
    FILE *f;

    gb_action(c, GB_OPEN);
    assert(!strcmp(opened, shot));
    gb_action(c, GB_SHOW);
    assert(!strcmp(opened, dir));
    gb_action(c, GB_SAVE);
    assert(saves == 1);
    snprintf(copy, sizeof copy, "%s/kept.png", dir);
    tiku_grabview_picked(c, copy);
    assert(strstr(c->status, "A copy is in kept.png"));
    f = fopen(copy, "rb");
    assert(f);
    fclose(f);
}

/** @brief Five seconds' delay: nothing asked until they have passed, the
 *         status counting down; then a refusal, said. */
static void delay_tests(tiku_grabview_t *c)
{
    tiku_msg_t *m;

    gb_action(c, GB_DELAY_5);
    c->sel = 0;
    tiku_grabview_tick(c, 10000000);
    requests = 0;
    gb_action(c, GB_GRAB);
    assert(requests == 0 && strstr(c->status, "Grabbing in 5"));
    tiku_grabview_tick(c, 12100000);
    assert(requests == 0 && strstr(c->status, "Grabbing in 3"));
    tiku_grabview_tick(c, 15000001);
    assert(requests == 1 && last_what() == TIKU_GRAB_DESK && c->waiting);
    m = tiku_msg_new(TIKU_MSG_GRABBED);
    assert(m && tiku_msg_add_string(m, "error", "That window has closed."));
    tiku_grabview_message(c, m);
    tiku_msg_free(m);
    assert(!c->waiting && !strcmp(c->status, "That window has closed."));
}

/** @brief Every group, in order, on one window. */
int main(void)
{
    int dirty = 0;
    tiku_grabview_host_t host = { host_send, host_save, host_open, NULL };
    char dir[] = "/tmp/tikugrabapp-XXXXXX", shot[256], cmd[300];
    tiku_surface_t *s = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    tiku_grabview_t *c;

    assert(mkdtemp(dir));
    desk = tiku_workspace_new(s);
    c = tiku_grabview_new(desk, &dirty, &host);
    assert(c);
    tiku_grabview_open(c);
    empty_tests(c, s);
    grab_tests(c);
    snprintf(shot, sizeof shot, "%s/tiku-window-Notes-1.png", dir);
    picture_tests(c, s, shot);
    output_tests(c, dir, shot);
    delay_tests(c);
    tiku_grabview_destroy(c);
    tiku_workspace_free(desk);
    tiku_surface_free(s);
    tiku_msg_free(asked);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    (void)system(cmd);
    puts("TikuGrab: blank, list, grab a window, preview, open, show, save "
         "as, delay, refusal PASS");
    return 0;
}
