/* The device tray on the global bar without a desktop: a cell per board,
 * from connecting to connected with the ring that closes as it lands, red
 * for two seconds on a failure, lent while another program has the board
 * and back when it is reclaimed; drawn right to left short of the menu
 * titles; the tip, the cell menu and its commands, the double-click that
 * opens the folder.  SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "../../system/desktop/tiku_trk_desktop_devtray.c"

static int64_t clock_us = 1000000;
static int damages, opened, prefs, dropped, reclaimed;
static char last[TIKU_PATH_MAX];

/** @brief The clock, moved by the test. */
int64_t tiku_trk_now(void) { return clock_us; }
/** @brief Damage is counted. */
void desktop_damage(desktop_t *app, tiku_rect_t r)
{
    (void)app; (void)r;
    damages++;
}
/** @brief A full frame is the flag, as the other system tests keep it. */
void desktop_damage_all(desktop_t *app) { app->dirty = 1; }
/** @brief The mounts here are local files: no port. */
int desktop_board_port(desktop_t *app, const char *name, char *out,
                       size_t max)
{
    (void)app; (void)name;
    out[0] = '\0';
    (void)max;
    return 0;
}
/** @brief Board "nrf" reports a processor of another name. */
int desktop_board_mcu(desktop_t *app, const char *name, char *out,
                      size_t max)
{
    (void)app;
    snprintf(out, max, "%s", strcmp(name, "nrf") == 0 ? "nRF54L15" : "");
    return (int)strlen(out);
}
/** @brief A port's leaf. */
void device_port_where(const char *port, char *out, size_t max)
{
    const char *leaf = strrchr(port, '/');

    snprintf(out, max, "%s", (leaf != NULL) ? leaf + 1 : port);
}
/** @brief The commands the cell menu runs, recorded. */
folder_t *open_folder(desktop_t *app, const char *path)
{
    (void)app;
    opened++;
    snprintf(last, sizeof last, "%s", path);
    return NULL;
}
/** @brief Recorded. */
void desktop_deviceprefs_open_on(desktop_t *app, const char *name)
{
    (void)app;
    prefs++;
    snprintf(last, sizeof last, "%s", name);
}
/** @brief Recorded. */
int device_disconnect(desktop_t *app, const char *name)
{
    (void)app;
    dropped++;
    snprintf(last, sizeof last, "%s", name);
    return 1;
}
/** @brief Recorded. */
int desktop_board_reclaim(desktop_t *app, const char *name, int session,
                          struct desktop_embed *slot)
{
    (void)app; (void)session; (void)slot;
    reclaimed++;
    snprintf(last, sizeof last, "%s", name);
    return 1;
}

static const tiku_rect_t strip = { 0, 0, 1000, 21 };

/** @brief A turn: the tick, then a frame drawn with motion off. */
static int turn(desktop_t *app)
{
    tiku_rect_t d;
    int changed = desktop_devtray_tick(app, clock_us, &d);

    desktop_devtray_draw(app, strip, 100, 966, 0);
    return changed;
}

/** @brief How many cells are in use. */
static int cells(desktop_t *app)
{
    int i, n = 0;

    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        n += app->devtray[i].state != DESKTOP_DEVTRAY_NONE;
    }
    return n;
}

/** @brief The pixel at cell @p i's pin-one mark. */
static tiku_rgb_t mark(desktop_t *app, int i)
{
    tiku_rect_t b = app->devtray_box[i];

    return tiku_peek(app->surf, b.x + b.w / 2 - 2, b.y + b.h / 2 - 2);
}

/** @brief A primary press at (@p x, @p y). */
static tiku_event_t press_at(int x, int y)
{
    tiku_event_t ev;

    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_POINTER_DOWN;
    ev.button = 1u;
    ev.x = x;
    ev.y = y;
    return ev;
}

/** @brief The centre of cell @p i. */
static tiku_event_t press_cell(desktop_t *app, int i)
{
    tiku_rect_t b = app->devtray_box[i];

    return press_at(b.x + b.w / 2, b.y + b.h / 2);
}

int main(void)
{
    desktop_t *app = calloc(1, sizeof *app);
    tiku_rgb_t ink = TIKU_C_TEXT, panel = TIKU_C_PANEL;
    char tip[256];
    tiku_event_t ev;
    tiku_rect_t d;
    int i;

    assert(app);
    app->surf = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    app->menus.bar.height = strip.h;
    app->gbar_on = 1;
    app->backend = tiku_mount_open();
    assert(app->backend &&
           tiku_mount_add(app->backend, "/", tiku_backend_posix_open(), 1)
               == 0);

    /* Nothing mounted: no cells. */
    assert(turn(app) == 0 && cells(app) == 0);
    assert(desktop_devtray_at(app, 950, 10) < 0);

    /* A board mounted with no hook gets a cell, beside the camera. */
    assert(tiku_mount_add(app->backend, "/devices/board1",
                          tiku_backend_posix_open(), 1) == 0);
    assert(desktop_devtray_tick(app, clock_us, &d) == 1 && d.w == 1000);
    desktop_devtray_draw(app, strip, 100, 966, 0);
    assert(app->devtray[0].state == DESKTOP_DEVTRAY_CONNECTED &&
           strcmp(app->devtray[0].name, "board1") == 0);
    assert(app->devtray_box[0].x == 942 && app->devtray_box[0].w == 24);
    assert(mark(app, 0) == ink);
    assert(desktop_devtray_at(app, 950, 10) == 0);
    app->ptr_x = 950;
    app->ptr_y = 10;
    assert(desktop_devtray_tip(app, tip, sizeof tip) &&
           strcmp(tip, "board1, connected just now") == 0);
    app->gbar_on = 0;
    assert(!desktop_devtray_tip(app, tip, sizeof tip));
    app->gbar_on = 1;
    clock_us += 125LL * 60 * 1000000;
    assert(desktop_devtray_tip(app, tip, sizeof tip) &&
           strcmp(tip, "board1, connected for 2 h 5 min") == 0);

    /* A connect: dimmed inside the ring's track with motion off, the next
     * cell to the left; its tip names the port. */
    desktop_devtray_connecting(app, "/dev/cu.usbmodem7");
    assert(cells(app) == 2 && turn(app) == 0);
    assert(app->devtray[1].state == DESKTOP_DEVTRAY_CONNECTING &&
           app->devtray_box[1].x == 918);
    assert(mark(app, 1) == mix(panel, ink, 0.5f));
    app->ptr_x = 930;
    assert(desktop_devtray_tip(app, tip, sizeof tip) &&
           strcmp(tip, "Connecting to cu.usbmodem7") == 0);
    /* With motion, the arc turns and asks for the next frame. */
    damages = 0;
    desktop_devtray_draw(app, strip, 100, 966, clock_us + 250000);
    assert(damages == 1);
    /* No menu while it connects: the press is taken, nothing drops. */
    ev = press_cell(app, 1);
    assert(desktop_devtray_press(app, &ev, clock_us) == 1 &&
           app->popup == NULL);

    /* It lands as nrf: connected, the ring closing over the next frames;
     * the tip names the processor too. */
    assert(tiku_mount_add(app->backend, "/devices/nrf",
                          tiku_backend_posix_open(), 1) == 0);
    desktop_devtray_landed(app, "/dev/cu.usbmodem7", "/devices/nrf");
    assert(app->devtray[1].state == DESKTOP_DEVTRAY_CONNECTED &&
           strcmp(app->devtray[1].name, "nrf") == 0 &&
           app->devtray[1].landing.active);
    assert(desktop_devtray_tick(app, clock_us, &d) == 0 && cells(app) == 2);
    damages = 0;
    desktop_devtray_draw(app, strip, 100, 966, clock_us + 100000);
    assert(damages == 1 && mark(app, 1) != ink);
    desktop_devtray_draw(app, strip, 100, 966, clock_us + 500000);
    assert(mark(app, 1) == ink && !app->devtray[1].landing.active);
    assert(desktop_devtray_tip(app, tip, sizeof tip) &&
           strcmp(tip, "nrf (nRF54L15) on cu.usbmodem7, connected just now")
               == 0);

    /* A connect that nothing answers: red for two seconds with the
     * reason in its tip, then gone. */
    desktop_devtray_connecting(app, "/dev/cu.empty");
    desktop_devtray_failed(app, "/dev/cu.empty", NULL, "nothing answered");
    assert(turn(app) == 0 && cells(app) == 3);
    assert(app->devtray[2].state == DESKTOP_DEVTRAY_GONE &&
           mark(app, 2) == TIKU_C_DANGER);
    app->ptr_x = 906;
    assert(desktop_devtray_tip(app, tip, sizeof tip) &&
           strcmp(tip, "cu.empty: nothing answered") == 0);
    clock_us += 1900000;
    assert(turn(app) == 0 && cells(app) == 3);
    clock_us += 200000;
    assert(turn(app) == 1 && cells(app) == 2);
    assert(desktop_devtray_at(app, 906, 10) < 0);

    /* A connect running past its time counts as failed. */
    desktop_devtray_connecting(app, "/dev/cu.slow");
    clock_us += 91000000;
    assert(turn(app) == 1 && app->devtray[2].state == DESKTOP_DEVTRAY_GONE &&
           strcmp(app->devtray[2].why, "no answer") == 0);
    clock_us += 2000000;
    assert(turn(app) == 1 && cells(app) == 2);

    /* The connected cell's menu: Open, Devices Preferences (no port here,
     * so off), Disconnect; each runs on the board it dropped from. */
    ev = press_cell(app, 0);
    clock_us += 1000000;
    assert(desktop_devtray_press(app, &ev, clock_us) == 1 &&
           app->popup == &app->devtraymenu);
    assert(app->devtraymenu.count == 4 &&
           strcmp(app->devtraymenu.item[0].label, "Open board1") == 0 &&
           !app->devtraymenu.item[1].enabled &&
           app->devtraymenu.item[2].separator &&
           strcmp(app->devtraymenu.item[3].label, "Disconnect") == 0);
    assert(app->popup_frame.y == strip.h + 1);
    assert(desktop_devtray_invoke(app, TIKU_TRK_CMD_DEVTRAY_OPEN) == 1 &&
           opened == 1 && strcmp(last, "/devices/board1") == 0);
    assert(desktop_devtray_invoke(app, TIKU_TRK_CMD_DEVTRAY_DISCONNECT) &&
           dropped == 1 && strcmp(last, "board1") == 0);
    assert(desktop_devtray_invoke(app, TIKU_TRK_CMD_DEVTRAY_PREFS) &&
           prefs == 1 && strcmp(last, "board1") == 0);
    assert(desktop_devtray_invoke(app, TIKU_TRK_CMD_DEVICE_SCAN) == 0);
    /* Drawn open, the cell is a pill in the selection colour. */
    desktop_devtray_draw(app, strip, 100, 966, 0);
    assert(tiku_peek(app->surf, 945, 10) == TIKU_C_SELECT &&
           mark(app, 0) == TIKU_C_SELTEXT);
    /* The second press of a double-click, on the cell, opens the folder;
     * no tip shows while the menu does. */
    assert(!desktop_devtray_tip(app, tip, sizeof tip));
    clock_us += 200000;
    assert(desktop_devtray_again(app, &ev, clock_us) == 1 && opened == 2);
    app->popup = NULL;
    /* Slower, it is two presses: the second drops the menu again. */
    clock_us += 1000000;
    assert(desktop_devtray_press(app, &ev, clock_us) == 1);
    clock_us += 600000;
    assert(desktop_devtray_again(app, &ev, clock_us) == 0 && opened == 2);
    app->popup = NULL;

    /* Lent: unmounted while released keeps a dimmed cell with a badge,
     * and its menu reclaims it. */
    snprintf(app->released[0].name, sizeof app->released[0].name, "board1");
    app->released[0].used = 1;
    assert(tiku_mount_remove(app->backend, "/devices/board1") == 0);
    assert(turn(app) == 1 && app->devtray[0].state == DESKTOP_DEVTRAY_LENT);
    assert(mark(app, 0) == tiku_dim(ink, panel));
    app->ptr_x = 950;
    assert(desktop_devtray_tip(app, tip, sizeof tip) &&
           strcmp(tip, "board1, lent to another program") == 0);
    ev = press_cell(app, 0);
    clock_us += 1000000;
    assert(desktop_devtray_press(app, &ev, clock_us) == 1 &&
           app->devtraymenu.count == 1 &&
           strcmp(app->devtraymenu.item[0].label, "Reclaim board1") == 0);
    assert(desktop_devtray_invoke(app, TIKU_TRK_CMD_DEVTRAY_RECLAIM) &&
           reclaimed == 1 && strcmp(last, "board1") == 0);
    app->popup = NULL;
    /* A reclaim that finds nothing turns it red; it stays lent. */
    desktop_devtray_failed(app, NULL, "board1", "not found on any port");
    assert(turn(app) == 0 && mark(app, 0) == TIKU_C_DANGER);
    clock_us += 2100000;
    assert(turn(app) == 1 && app->devtray[0].state == DESKTOP_DEVTRAY_LENT);
    /* Back: mounted again, connected with the ring closing. */
    app->released[0].used = 0;
    assert(tiku_mount_add(app->backend, "/devices/board1",
                          tiku_backend_posix_open(), 1) == 0);
    assert(desktop_devtray_tick(app, clock_us, &d) == 1 &&
           app->devtray[0].state == DESKTOP_DEVTRAY_CONNECTED &&
           app->devtray[0].landing.active);

    /* A lost link: red under its own name, then the cell goes and the
     * one after it moves up beside the camera. */
    desktop_devtray_failed(app, NULL, "board1", "the link was lost");
    assert(tiku_mount_remove(app->backend, "/devices/board1") == 0);
    assert(turn(app) == 0 && app->devtray[0].state == DESKTOP_DEVTRAY_GONE);
    clock_us += 2100000;
    assert(turn(app) == 1 && cells(app) == 1 &&
           strcmp(app->devtray[0].name, "nrf") == 0 &&
           app->devtray_box[0].x == 942);
    /* A disconnect unmounts with no failure: the cell goes at once. */
    assert(tiku_mount_remove(app->backend, "/devices/nrf") == 0);
    assert(turn(app) == 1 && cells(app) == 0);

    /* The cells stop short of the menu titles. */
    for (i = 0; i < 4; i++) {
        char port[32];

        snprintf(port, sizeof port, "/dev/cu.p%d", i);
        desktop_devtray_connecting(app, port);
    }
    desktop_devtray_draw(app, strip, 966 - 3 * 24 - 8, 966, 0);
    assert(app->devtray_box[2].w > 0 && app->devtray_box[3].w == 0);
    /* Full: a new connect takes the place of a red cell, else none. */
    for (i = 4; i < TIKU_MOUNT_MAX; i++) {
        char port[32];

        snprintf(port, sizeof port, "/dev/cu.p%d", i);
        desktop_devtray_connecting(app, port);
    }
    assert(cells(app) == TIKU_MOUNT_MAX);
    desktop_devtray_connecting(app, "/dev/cu.late");
    assert(cell_on_port(app, "/dev/cu.late") == NULL);
    desktop_devtray_failed(app, "/dev/cu.p2", NULL, "nothing answered");
    desktop_devtray_connecting(app, "/dev/cu.late");
    assert(cells(app) == TIKU_MOUNT_MAX &&
           cell_on_port(app, "/dev/cu.p2") == NULL &&
           app->devtray[TIKU_MOUNT_MAX - 1].state ==
               DESKTOP_DEVTRAY_CONNECTING &&
           strcmp(app->devtray[TIKU_MOUNT_MAX - 1].port, "/dev/cu.late")
               == 0);
    /* A port that carries windows with no shell lets its cell go. */
    desktop_devtray_forget(app, "/dev/cu.late");
    assert(cells(app) == TIKU_MOUNT_MAX - 1);

    /* The pointer onto a cell or off one redraws the tip band. */
    turn(app);
    damages = 0;
    app->ptr_x = 950;
    desktop_devtray_hover(app, 500, 300);
    assert(damages == 1);
    desktop_devtray_hover(app, 951, 11);
    assert(damages == 1);

    printf("devtray: ok\n");
    return 0;
}
