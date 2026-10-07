/* tiku_ide_commands.c -- the menu commands of every IDE window, and the
 * answers to the desktop's file panel.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

/** @brief Toggle comment on page @p e: "//" on C lines and REM after the
 *         number on BASIC ones. */
static int
toggle_comment(ide_t *st, edit_t *e)
{
    if (tiku_codeview_toggle_comment(e->view) == TIKU_EDIT_OK) {
        tiku_ide_edit_reveal(e);
        tiku_ide_edit_publish(st, e);
        tiku_ide_project_publish(st);
    }
    tiku_ide_edit_paint(st, e);
    return 0;
}

/**
 * @brief Carry out a project command: the file-panel questions, saving
 *        and closing page @p e, the project list's run, build and board
 *        commands, and Close Project.
 * @return 1 for Close Project with nothing unsaved; otherwise 0.
 */
static int
pick_project_command(ide_t *st, edit_t *e, int command)
{
    switch (command) {
    case CMD_OPEN:
        if (st->services->pick != NULL) {
            st->asked = ASK_OPEN;
            /* The panel takes only a file named project.tiku; other
             * files show greyed. */
            (void)st->services->pick(st->services->ctx, st->proj_id,
                                     TIKU_APP_PICK_OPEN, NULL,
                                     "project.tiku");
        }
        break;
    case CMD_ABOUT:
        tiku_ide_about_open(st);
        break;
    case CMD_NEWPROJ:
        if (st->services->pick != NULL) {
            st->asked = ASK_NEWPROJ;
            /* The Save panel offers "untitled"; the name chosen becomes
             * a folder, which tiku_ide_project_create() fills. */
            (void)st->services->pick(st->services->ctx, st->proj_id,
                                     TIKU_APP_PICK_SAVE, NULL,
                                     "untitled");
        }
        break;
    case CMD_NEWFILE:
        if (st->services->pick != NULL && st->proj_file[0] != '\0') {
            st->asked = ASK_FILE;
            (void)st->services->pick(st->services->ctx, st->proj_id,
                                     TIKU_APP_PICK_SAVE, st->proj.dir,
                                     APP_NEW_FILE);
        }
        break;
    case CMD_OPENFILE:
        if (st->services->pick != NULL) {
            st->asked = ASK_EDIT;
            (void)st->services->pick(st->services->ctx, st->proj_id,
                                     TIKU_APP_PICK_OPEN,
                                     (st->proj_file[0] != '\0')
                                         ? st->proj.dir : NULL, NULL);
        }
        break;
    case CMD_ADDFILE:
        if (st->services->pick != NULL && st->proj_file[0] != '\0') {
            st->asked = ASK_FILE;
            (void)st->services->pick(st->services->ctx, st->proj_id,
                                     TIKU_APP_PICK_OPEN, st->proj.dir,
                                     NULL);
        }
        break;
    case CMD_SAVE:
        if (e != NULL) {
            (void)tiku_ide_edit_save_or_ask(st, e);
            tiku_ide_edit_paint(st, e);
            tiku_ide_edit_publish(st, e);
            tiku_ide_project_publish(st);
        }
        break;
    case CMD_QUIT:
        if (e != NULL) {
            tiku_ide_edit_drop(st, e);
        }
        break;
    case CMD_REMOVEFILE:
        tiku_ide_project_remove_file(st);
        break;
    case CMD_RUN: {
        int row = tiku_list_chosen(&st->tv_list);

        if (row >= 0 && row < st->proj.nrow &&
            !st->proj.row[row].heading) {
            edit_t *open_now = tiku_ide_edit_of_path(st, st->proj.row[row].path);

            /* A run reads the file on disk, so a page holding changes
             * to it is saved first. */
            if (open_now != NULL &&
                tiku_editdoc_modified(open_now->doc)) {
                (void)tiku_ide_edit_save(open_now);
                tiku_ide_edit_paint(st, open_now);
                tiku_ide_edit_publish(st, open_now);
            }
            tiku_ide_run_file_where(st, st->proj.row[row].path);
            tiku_ide_project_publish(st);
        }
        break;
    }
    case CMD_SAVEBOARD: {
        int row = tiku_list_chosen(&st->tv_list);

        if (row >= 0 && row < st->proj.nrow &&
            !st->proj.row[row].heading) {
            edit_t *open_now = tiku_ide_edit_of_path(st, st->proj.row[row].path);

            if (open_now != NULL &&
                tiku_editdoc_modified(open_now->doc)) {
                (void)tiku_ide_edit_save(open_now);
                tiku_ide_edit_paint(st, open_now);
                tiku_ide_edit_publish(st, open_now);
            }
            tiku_ide_save_file_on_board(st, st->proj.row[row].path);
            tiku_ide_project_publish(st);
        }
        break;
    }
    case CMD_STOP:
        tiku_ide_run_stop(st, "? the run was stopped by hand");
        tiku_ide_project_publish(st);
        break;
    case CMD_BUILD:
        tiku_ide_build_project(st);
        tiku_ide_project_publish(st);
        break;
    case CMD_FOLLOW:
        tiku_ide_message_follow(st);
        break;
    case CMD_SHUT: {
        char names[192];

        if (tiku_ide_unsaved(st, names, sizeof names) == 0) {
            return 1;           /* nothing unsaved: close without asking */
        }
        {
            char says[320];

            snprintf(says, sizeof says,
                     "Close the project? %s %s changes nothing has "
                     "saved.", names,
                     (strchr(names, ',') != NULL) ? "hold" : "holds");
            tiku_alert_open(&st->ask, TIKU_ALERT_WARN, 0, says,
                            "Cancel|Discard");
            st->ask.dflt = 0;       /* Cancel */
        }
        if (st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx,
                                             st->proj_id);
        }
        tiku_ide_project_paint(st);
        break;
    }
    default:
        break;
    }
    return 0;
}

/**
 * @brief Carry out menu command @p command, picked in window @p window.
 *
 * Nothing is done while a question is open, nor, on a page a debug run
 * holds, for a command that would change it.  Commands on the page are
 * done here; the rest go to pick_project_command().
 *
 * @return 1 when the application is done: Close Project with nothing
 *         unsaved; otherwise 0.
 */
int
tiku_ide_pick(void *state, uint32_t window, int command)
{
    ide_t *st = state;
    edit_t *e = tiku_ide_edit_of_id(st, window);

    if (st->ask.open) { return 0; }
    if (e && e->lang == TIKU_SYNTAX_BASIC) {
        if (command == CMD_HELPWORD || (command == CMD_COMPLETE && !tiku_ide_debug_locked(st, e))) {
            tiku_ide_assist_open(e, command == CMD_COMPLETE); tiku_ide_edit_paint(st, e); return 0;
        }
        if (command == CMD_DEBUG || command == CMD_DEBUG_PANEL || command == CMD_BREAKPOINT || command == CMD_CLEARBREAKS ||
            command == CMD_DEBUG_STEP || command == CMD_DEBUG_CONT) {
            tiku_ide_debug_show(e);
            if (command == CMD_DEBUG) { tiku_ide_debug_start(st, e); }
            if (command == CMD_BREAKPOINT && !st->running) {
                tiku_ide_debug_break(e);
            }
            if (command == CMD_CLEARBREAKS && !st->running) {
                tiku_ide_debug_clear(e);
            }
            if (tiku_ide_debug_locked(st, e) && (command == CMD_DEBUG_STEP || command == CMD_DEBUG_CONT)) {
                tiku_ide_debug_command(st, command == CMD_DEBUG_STEP ? "DEBUG STEP" : "DEBUG CONT");
            }
            tiku_ide_edit_paint(st, e); tiku_ide_edit_publish(st, e); tiku_ide_project_publish(st);
            return 0;
        }
        if (tiku_ide_debug_locked(st, e) && command != CMD_COPY && command != CMD_STOP &&
            command != CMD_QUIT && command < CMD_WINDOW) { return 0; }
    }
    if (command == CMD_RECOVER) { tiku_ide_recovery_scan(st); return 0; }
    if (e && command == CMD_PROJECT_FIND) {
        tiku_ide_search_project(st, e);
        tiku_ide_edit_paint(st, e);
        return 0;
    }
    if (command >= CMD_WINDOW) {
        int i = command - CMD_WINDOW;

        if (i >= 0 && i < IDE_EDITS && st->edit[i].surface != NULL &&
            st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx,
                                             st->edit[i].id);
        }
        return 0;
    }
    if (e != NULL && command == CMD_RUN &&
        e->lang == TIKU_SYNTAX_BASIC) {
        /* Run from a page runs that page's file. */
        (void)tiku_ide_edit_save(e);
        tiku_ide_run_file_where(st, e->path);
        tiku_ide_edit_paint(st, e);
        tiku_ide_edit_publish(st, e);
        tiku_ide_project_publish(st);
        return 0;
    }
    if (e != NULL && command == CMD_STOP) {
        tiku_ide_run_stop(st, "? the run was stopped by hand");
        tiku_ide_edit_paint(st, e);
        tiku_ide_edit_publish(st, e);
        return 0;
    }
    if (e != NULL && (command == CMD_FIND || command == CMD_REPLACE)) {
        tiku_ide_search_open(e, command == CMD_REPLACE);
        tiku_ide_edit_paint(st, e);
        return 0;
    }
    if (e != NULL && command == CMD_COMMENT) {
        return toggle_comment(st, e);
    }
    if (e != NULL && (command == CMD_NEXT_PROBLEM ||
                      command == CMD_PREV_PROBLEM)) {
        return tiku_ide_problems_go(st, e, command == CMD_PREV_PROBLEM);
    }
    if (e != NULL && command == CMD_RENUM) {
        tiku_ide_renumber_basic(e);
        tiku_ide_edit_paint(st, e);
        tiku_ide_edit_publish(st, e);
        tiku_ide_project_publish(st);
        return 0;
    }
    if (e != NULL && (command == CMD_CUT || command == CMD_COPY ||
                      command == CMD_PASTE || command == CMD_SELALL ||
                      command == CMD_UNDO || command == CMD_REDO)) {
        if (tiku_ide_edit_clip(st, e, command)) {
            tiku_ide_edit_reveal(e);
            tiku_ide_edit_paint(st, e);
            if (command != CMD_COPY && command != CMD_SELALL) {
                tiku_ide_edit_publish(st, e);
                tiku_ide_project_publish(st);
            }
        }
        return 0;
    }
    return pick_project_command(st, e, command);
}

/** @brief Take the file panel's answer to the question st->asked names:
 *         make a project, add a file, open a file in a page, or open a
 *         project. */
void
tiku_ide_picked(void *state, uint32_t window, const char *path)
{
    ide_t *st = state;

    (void)window;
    if (path == NULL || path[0] == '\0') {
        return;
    }
    switch (st->asked) {
    case ASK_NEWPROJ:
        tiku_ide_project_create(st, path);
        break;
    case ASK_FILE:
        tiku_ide_project_add_file(st, path);
        break;
    case ASK_EDIT: {
        /* The file opens in a page wherever it is, without being added
         * to the project; a file already open is raised. */
        prow_t want;
        const char *slash = strrchr(path, '/');

        memset(&want, 0, sizeof want);
        snprintf(want.path, sizeof want.path, "%s", path);
        snprintf(want.name, sizeof want.name, "%s",
                 (slash != NULL) ? slash + 1 : path);
        tiku_ide_edit_open(st, &want);
        break;
    }
    default:
        tiku_ide_project_take(st, path);
        break;
    }
}
