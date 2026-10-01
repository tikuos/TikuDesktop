/* tiku_ide_commands.c -- commands.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

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
            if (command == CMD_BREAKPOINT && !st->running) { tiku_ide_debug_break(e); }
            if (command == CMD_CLEARBREAKS && !st->running) { memset(e->breaks, 0, sizeof e->breaks); }
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
        /* Run, asked ON a page: THIS file, not whichever row the
         * project window happens to hold selected. */
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
            (void)tiku_textview_reveal(&e->tv, tiku_ide_edit_rows(e));
            tiku_ide_edit_paint(st, e);
            if (command != CMD_COPY && command != CMD_SELALL) {
                tiku_ide_edit_publish(st, e);
                tiku_ide_project_publish(st);
            }
        }
        return 0;
    }
    switch (command) {
    case CMD_OPEN:
        if (st->services->pick != NULL) {
            st->asked = ASK_OPEN;
            /* Only a project DESCRIPTION answers this question, and
             * the panel says so: every other file shows greyed. */
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
            /* The shell's own Save panel names the place and the file;
             * the offered name is the one the opener looks for. */
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

            /* What is RUN is what is on disk, so a window holding
             * changes is saved first -- running the last saved copy
             * while showing another is how a person debugs a program
             * they are not looking at. */
            if (open_now != NULL &&
                tiku_textview_modified(&open_now->tv)) {
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
                tiku_textview_modified(&open_now->tv)) {
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
            return 1;           /* nothing at stake: closing is closing */
        }
        {
            char says[320];

            snprintf(says, sizeof says,
                     "Close the project? %s %s changes nothing has "
                     "saved.", names,
                     (strchr(names, ',') != NULL) ? "hold" : "holds");
            tiku_alert_open(&st->ask, TIKU_ALERT_WARN, 0, says,
                            "Cancel|Discard");
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
        /* An editor on the file, wherever it lives: opening is not
         * adding, and a file already open comes forward. */
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
