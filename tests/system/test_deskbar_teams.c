/* The Deskbar's applications, a row each: every window listed under the
 * application whose session opened it and this shell's own under
 * TikuTracker; the manifest's icon worn; a second instance sharing its
 * row; titles less the application's name; a row gone with its last
 * window; and what a click on a row reaches -- that application's windows,
 * no one else's.  SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "../../system/desktop/tiku_trk_desktop_teams.c"
#include "../../system/desktop/tiku_trk_desktop_deskbar.c"

static int closes, forcequits;

/** @brief The shell's window closer, standing in: counted, then gone. */
void close_window(desktop_t *app, tiku_window_t *w)
{
    closes++;
    tiku_workspace_close(app->desk, w);
}

/** @brief The Force Quit panel, standing in: counted. */
void desktop_forcequit_open(desktop_t *app)
{
    (void)app;
    forcequits++;
}

/** @brief A window called @p title opened on the desk. */
static tiku_window_t *open_window(desktop_t *app, const char *title, int x)
{
    tiku_window_t *w = tiku_workspace_open(app->desk, title,
        (tiku_rect_t){x, 60, 200, 150}, NULL, NULL, NULL);

    assert(w);
    w->tag = w;                     /* the window, as its own identity */
    return w;
}

/** @brief Window @p w held by session @p i, an application called @p name. */
static void hold(desktop_t *app, int i, const char *name, tiku_window_t *w)
{
    tiku_remote_session_t *s = &app->remote.session[i];
    int j = 0;

    while (s->win[j].window != NULL) j++;
    s->used = 1;
    snprintf(s->name, sizeof s->name, "%s", name);
    s->win[j].window = w;
    s->win[j].opened = 1;
}

/** @brief The Deskbar entry called @p name: its windows, which must be
 *         @p n, and whether it is the active one. */
static const tiku_trk_dbwin_t *team(desktop_t *app, const char *name, int n,
                                    int *active)
{
    const tiku_trk_dbwin_t *wins = NULL;
    int t = tiku_trk_deskbar_find_team(app->deskbar, name);

    assert(t >= 0);
    assert(tiku_trk_deskbar_team_windows(app->deskbar, t, &wins, active) ==
           n);
    return wins;
}

/** @brief A desk with this shell's folder, two instances of TikuDiff and
 *         one of TikuPDF; only TikuDiff's manifest names an icon. */
static desktop_t *setup(tiku_window_t **diff1, tiku_window_t **diff2,
                        tiku_window_t **pdf)
{
    desktop_t *app = calloc(1, sizeof *app);

    assert(app);
    app->surf = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    app->desk = tiku_workspace_new(app->surf);
    app->deskbar = tiku_trk_deskbar_new(NULL);
    app->team = tiku_trk_deskbar_add_team(app->deskbar, "TikuTracker");
    snprintf(app->apps.app[0].name, sizeof app->apps.app[0].name, "TikuDiff");
    snprintf(app->apps.app[0].icon, sizeof app->apps.app[0].icon,
             "diff_viewer");
    snprintf(app->apps.app[1].name, sizeof app->apps.app[1].name, "TikuPDF");
    app->apps.count = 2;
    (void)open_window(app, "trk_script", 10);
    *diff1 = open_window(app, "TikuDiff - one.c : two.c", 100);
    *pdf = open_window(app, "TikuPDF - guide.pdf", 200);
    *diff2 = open_window(app, "TikuDiff", 300);
    hold(app, 0, "TikuDiff", *diff1);
    hold(app, 1, "TikuPDF", *pdf);
    hold(app, 2, "TikuDiff", *diff2);
    return app;
}

/** @brief Each window under its application's row, each row its icon;
 *         the row with the focused window active. */
static void rows_test(desktop_t *app)
{
    const tiku_trk_dbwin_t *w;
    int active = 0;

    desktop_publish_teams(app);
    assert(tiku_trk_deskbar_team_count(app->deskbar) == 3);
    w = team(app, "TikuTracker", 1, &active);
    assert(!strcmp(w[0].title, "trk_script") && !active);
    w = team(app, "TikuDiff", 2, &active);
    assert(!strcmp(w[0].title, "TikuDiff") && !strcmp(w[1].title,
           "one.c : two.c") && active);         /* diff2 is in front */
    w = team(app, "TikuPDF", 1, NULL);
    assert(!strcmp(w[0].title, "guide.pdf"));
    assert(!strcmp(tiku_trk_deskbar_team_icon(app->deskbar,
           tiku_trk_deskbar_find_team(app->deskbar, "TikuDiff")),
           "diff_viewer"));
    assert(!strcmp(tiku_trk_deskbar_team_icon(app->deskbar,
           tiku_trk_deskbar_find_team(app->deskbar, "TikuPDF")),
           "application"));
    assert(!strcmp(tiku_trk_deskbar_team_icon(app->deskbar, app->team),
           "folder"));
}

/** @brief What a click on a row reaches: that application's windows. */
static void action_test(desktop_t *app, tiku_window_t *pdf)
{
    tiku_trk_db_pick_t pick;
    int diff = tiku_trk_deskbar_find_team(app->deskbar, "TikuDiff");

    memset(&pick, 0, sizeof pick);
    pick.what = TIKU_TRK_DB_HIDE_ALL;
    pick.team = diff;
    assert(deskbar_pick_team(app, &pick, 0, 0));
    for (int i = 0; i < tiku_workspace_count(app->desk); i++) {
        tiku_window_t *w = tiku_workspace_at(app->desk, i);
        assert(w->minimized == desktop_team_has(app, diff, w));
    }
    pick.what = TIKU_TRK_DB_ACTIVATE_TEAM;
    assert(deskbar_pick_team(app, &pick, 0, 0));
    assert(!tiku_workspace_at(app->desk, 1)->minimized &&
           desktop_team_has(app, diff, tiku_workspace_focused(app->desk)));
    pick.what = TIKU_TRK_DB_QUIT_TEAM;
    pick.team = tiku_trk_deskbar_find_team(app->deskbar, "TikuPDF");
    assert(deskbar_pick_team(app, &pick, 0, 0));
    assert(closes == 1 && forcequits == 0 &&
           tiku_workspace_count(app->desk) == 3);
    app->remote.session[1].win[0].window = NULL;   /* the session let go */
    (void)pdf;
    pick.team = app->team;          /* this shell's: no polite quit */
    assert(deskbar_pick_team(app, &pick, 0, 0) && forcequits == 1);
}

/** @brief A row goes with its application's last window; this shell's
 *         row stays with none. */
static void gone_test(desktop_t *app)
{
    tiku_trk_db_pick_t pick;

    desktop_publish_teams(app);
    assert(tiku_trk_deskbar_find_team(app->deskbar, "TikuPDF") < 0 &&
           tiku_trk_deskbar_team_count(app->deskbar) == 2);
    memset(&pick, 0, sizeof pick);
    pick.what = TIKU_TRK_DB_CLOSE_ALL;
    pick.team = tiku_trk_deskbar_find_team(app->deskbar, "TikuDiff");
    assert(deskbar_pick_team(app, &pick, 0, 0) && closes == 3);
    memset(app->remote.session, 0, sizeof app->remote.session);
    desktop_publish_teams(app);
    assert(tiku_trk_deskbar_team_count(app->deskbar) == 1 &&
           app->team == tiku_trk_deskbar_find_team(app->deskbar,
                                                   "TikuTracker"));
    (void)team(app, "TikuTracker", 1, NULL);
    pick.what = TIKU_TRK_DB_CLOSE_ALL;
    pick.team = app->team;
    assert(deskbar_pick_team(app, &pick, 0, 0) && closes == 3 &&
           tiku_workspace_count(app->desk) == 0);
    desktop_publish_teams(app);
    (void)team(app, "TikuTracker", 0, NULL);
}

/** @brief Every group, in order, on one desk. */
int main(void)
{
    tiku_window_t *diff1, *diff2, *pdf;
    desktop_t *app = setup(&diff1, &diff2, &pdf);
    char out[64];

    rows_test(app);
    action_test(app, pdf);
    gone_test(app);
    tiku_trk_deskbar_strip_title("TikuPDF", "TikuPDF - ", out, sizeof out);
    assert(!strcmp(out, "TikuPDF - "));     /* nothing after: kept whole */
    tiku_trk_deskbar_free(app->deskbar);
    tiku_workspace_free(app->desk);
    tiku_surface_free(app->surf);
    free(app);
    puts("PASS: Deskbar rows per application -- windows sorted, icons "
         "worn, instances shared, rows gone, clicks reach one application");
    return 0;
}
