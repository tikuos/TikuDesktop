/* tiku_ide_events.c -- input for the IDE's windows: a page's keys,
 * toolbar and scrollbar presses, resizes, the console's and Messages
 * window's input strip, and the answers to the Project window's
 * questions.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

/** @brief The names of the pages holding unsaved changes, comma-separated
 *         in @p out, which is empty when there are none.
 *         @return how many pages there are. */
int
tiku_ide_unsaved(const ide_t *st, char *out, size_t max)
{
    int i, n = 0;
    size_t used = 0u;

    if (max > 0u) {
        out[0] = '\0';
    }
    for (i = 0; i < IDE_EDITS; i++) {
        const edit_t *e = &st->edit[i];

        if (e->surface == NULL || !tiku_editdoc_modified(e->doc)) {
            continue;
        }
        used += (size_t)snprintf(out + used, (used < max) ? max - used : 0u,
                                 "%s%s", (n > 0) ? ", " : "", e->name);
        n++;
    }
    return n;
}

/** @brief Whether @p key moves the caret. */
static int
move_key(unsigned key)
{
    return key >= TIKU_KEY_UP && key <= TIKU_KEY_PAGE_DOWN;
}

/** @brief Whether @p key is a modifier key: Command, Shift or Option. */
static int
modifier_key(unsigned key)
{
    return key == TIKU_KEY_CMD || key == TIKU_KEY_SHIFT ||
           key == TIKU_KEY_OPTION;
}

/**
 * @brief A key on a page that no panel or tool took: the find bar's while
 *        it is open, else the page's Command keys, moves and edits.
 *
 * Moves, Backspace, Delete and typed text are the view's.  Return is the
 * page's, for BASIC's line numbers; Tab inserts nothing.
 */
static int
page_key(ide_t *st, edit_t *e, const tiku_event_t *event, unsigned ch)
{
    int edited = 0;
    uint64_t generation = tiku_editdoc_generation(e->doc);

    if (e->finding) {
        if (event->type != TIKU_EVENT_KEY_DOWN) { return 0; }
        tiku_ide_search_key(st, e, event);
        tiku_ide_edit_paint(st, e);
        tiku_ide_edit_publish(st, e);
        return 0;
    }
    /* Cmd-F opens the find bar, Cmd-H with replace in use; the bar then
     * has the keys. */
    if ((event->modifiers & TIKU_MOD_CMD) &&
        (event->key == 'f' || event->key == 'h')) {
        tiku_ide_search_open(e, event->key == 'h');
        tiku_ide_edit_paint(st, e);
        return 0;
    }
    /* The File menu's keys: Cmd-S saves, Cmd-W closes this page, Cmd-R
     * runs its program and Cmd-. stops a run. */
    if ((event->modifiers & TIKU_MOD_CMD) && event->key == 's') {
        (void)tiku_ide_edit_save_or_ask(st, e);
        tiku_ide_edit_paint(st, e);
        tiku_ide_edit_publish(st, e);
        tiku_ide_project_publish(st);
        return 0;
    }
    if ((event->modifiers & TIKU_MOD_CMD) && event->key == 'w') {
        tiku_ide_edit_drop(st, e);
        return 0;
    }
    if ((event->modifiers & TIKU_MOD_CMD) && event->key == 'r' &&
        e->lang == TIKU_SYNTAX_BASIC) {
        (void)tiku_ide_edit_save(e);
        tiku_ide_run_file_where(st, e->path);
        tiku_ide_edit_paint(st, e);
        tiku_ide_edit_publish(st, e);
        tiku_ide_project_publish(st);
        return 0;
    }
    if ((event->modifiers & TIKU_MOD_CMD) && event->key == '.' &&
        e->lang == TIKU_SYNTAX_BASIC) {
        tiku_ide_run_stop(st, "? the run was stopped by hand");
        tiku_ide_edit_paint(st, e);
        return 0;
    }
    if ((event->modifiers & TIKU_MOD_CMD) && event->key == '/') {
        return tiku_ide_pick(st, e->id, CMD_COMMENT);
    }
    if ((event->modifiers & TIKU_MOD_CMD) &&
        (event->key == '\'' || event->key == '"')) {
        return tiku_ide_pick(st, e->id,
                             (event->modifiers & TIKU_MOD_SHIFT) ||
                             event->key == '"' ? CMD_PREV_PROBLEM
                                               : CMD_NEXT_PROBLEM);
    }
    /* The desk's editing keys, Cmd X/C/V/A and Cmd-Z / Cmd-Shift-Z for
     * undo and redo, are caught before typing, so their letters are never
     * typed. */
    if ((event->modifiers & TIKU_MOD_CMD) &&
        (event->key == 'x' || event->key == 'c' || event->key == 'v' ||
         event->key == 'a' || event->key == 'z')) {
        int cmd = (event->key == 'x') ? CMD_CUT
                : (event->key == 'c') ? CMD_COPY
                : (event->key == 'v') ? CMD_PASTE
                : (event->key == 'a') ? CMD_SELALL
                : ((event->modifiers & TIKU_MOD_SHIFT) ? CMD_REDO : CMD_UNDO);
        int did = tiku_ide_edit_clip(st, e, cmd);

        if (did && cmd != CMD_COPY && cmd != CMD_SELALL) {
            tiku_ide_edit_reveal(e);
            tiku_ide_edit_publish(st, e);
            tiku_ide_project_publish(st);
        }
        tiku_ide_edit_paint(st, e);
        return 0;
    }
    if (move_key(event->key)) {
        /* The view's moves; a modifier it has no move for is dropped, so
         * Option with Up, or Command with a page key, moves as the key
         * alone does. */
        if (!tiku_codeview_event(e->view, event)) {
            tiku_event_t plain = *event;

            plain.modifiers &= TIKU_MOD_SHIFT;
            (void)tiku_codeview_event(e->view, &plain);
        }
    } else if (event->key == TIKU_KEY_RETURN) {
        /* In BASIC, Return types the line break and the next line's
         * number, read from the line the break is made in once a held
         * selection has gone; a line with no number gives none, so
         * prose in a .bas file is left alone. */
        char text[24];
        int n = 0;

        tiku_ide_edit_normalize_word(e);   /* the return ends a word */
        if (e->lang == TIKU_SYNTAX_BASIC) {
            int64_t line;

            (void)tiku_ide_caret(e, &line, NULL);
            n = tiku_ide_next_number(e, line);
        }
        snprintf(text, sizeof text, n > 0 ? "%s%d " : "%s", e->newline, n);
        if (e->lang == TIKU_SYNTAX_C) {
            (void)tiku_codeview_newline(e->view);   /* C keeps its levels */
        } else {
            (void)tiku_codeview_insert(e->view, text, strlen(text), 1);
        }
        edited = 1;
    } else if (event->key == TIKU_KEY_BACKSPACE ||
               event->key == TIKU_KEY_DELETE) {
        (void)tiku_codeview_delete(e->view, event->key == TIKU_KEY_DELETE);
        edited = 1;
    } else if (event->key == TIKU_KEY_TAB ||
               (event->modifiers & TIKU_MOD_CMD) || ch < 32u || ch == 127u) {
        return 0;               /* not the page's key */
    } else {
        /* A non-word character ends the word before it; a keyword there
         * is put in capitals before the character is inserted. */
        if (ch < 128u && !tiku_ide_norm_is_word((int)ch)) {
            tiku_ide_edit_normalize_word(e);
        }
        if (event->text[0] != '\0') {
            (void)tiku_codeview_event(e->view, event);
        } else {
            char one[2] = { (char)ch, '\0' };

            (void)tiku_codeview_insert(e->view, one, 1, 1);
        }
        edited = 1;
    }
    tiku_ide_edit_reveal(e);
    if (edited) {
        e->feedback[0] = '\0';
    }
    tiku_ide_edit_paint(st, e);
    if (edited || tiku_editdoc_generation(e->doc) != generation) {
        tiku_ide_edit_publish(st, e);
        tiku_ide_project_publish(st);   /* the "(changed)" mark */
    }
    return 0;
}

/**
 * @brief Route an event to the window it is for: a page, the console, the
 *        Messages window, the splash, the About box or the Project window.
 *
 * While the Project window has a question open, its events answer the
 * question and other windows' events are dropped.
 *
 * @return 1 when the application is done (Discard on the close
 *         question); otherwise 0.
 */
int
tiku_ide_event(void *state, uint32_t window, const tiku_event_t *event)
{
    ide_t *st = state;
    edit_t *e = tiku_ide_edit_of_id(st, window);

    if (e != NULL) {
        int result = tiku_text_input_event(&e->commit, event);
        tiku_ide_edit_lock(st, e);
        tiku_ide_edit_bounds(e);
        if (event->type == TIKU_EVENT_TEXT_INPUT) {
            int field = e->finding || e->watch_focus;

            if ((tiku_ide_debug_locked(st, e) && !e->watch_focus) || st->ask.open) return 0;
            /* Text for the page goes to the view piece by piece; e->commit
             * stages the same pieces and says whether the whole text
             * arrived. */
            if (!field) {
                (void)tiku_codeview_event(e->view, event);
            }
            if (result == TIKU_TEXT_REJECTED) {
                snprintf(e->feedback, sizeof e->feedback, "Text input is invalid or too long.");
            } else if (result == TIKU_TEXT_COMPLETE) {
                int accepted;
                if (field) {
                    tiku_field_t *f = e->finding
                        ? (e->find_focus ? &e->replacement : &e->find) : &e->watches;
                    accepted = tiku_field_commit(f, e->commit.text) !=
                               TIKU_FIELD_REJECTED;
                } else {
                    tiku_ide_assist_close(e);
                    accepted = tiku_codeview_status(e->view) == TIKU_EDIT_OK;
                    tiku_ide_edit_reveal(e);
                }
                snprintf(e->feedback, sizeof e->feedback, "%s",
                         accepted ? "" : "Text does not fit this field or line.");
            }
            tiku_ide_edit_paint(st, e); tiku_ide_edit_publish(st, e); tiku_ide_project_publish(st);
            return 0;
        }
    } else if (window == st->con_id || window == st->msg_id) {
        int result = tiku_text_input_event(&st->answer_commit, event);
        if (event->type == TIKU_EVENT_TEXT_INPUT) {
            if (st->asking && result == TIKU_TEXT_COMPLETE)
                (void)tiku_field_commit(&st->answer, st->answer_commit.text);
            if (window == st->con_id) tiku_ide_console_paint(st);
            else tiku_ide_messages_show(st);
            return 0;
        }
    }
    if (event->type != TIKU_EVENT_KEY_DOWN &&
        !(event->type == TIKU_EVENT_KEY_UP && modifier_key(event->key)) &&
        event->type != TIKU_EVENT_POINTER_DOWN &&
        event->type != TIKU_EVENT_POINTER_MOVE &&
        event->type != TIKU_EVENT_POINTER_UP &&
        event->type != TIKU_EVENT_RESIZE &&
        event->type != TIKU_EVENT_WHEEL) {
        return 0;
    }
    if (window == st->about_id && st->about_surface != NULL) {
        if (event->type == TIKU_EVENT_POINTER_DOWN) {
            /* The About box has no close button: a click closes it. */
            st->services->close(st->services->ctx, st->about_id);
            return 0;
        }
        if (event->type == TIKU_EVENT_KEY_DOWN &&
            (event->key == TIKU_KEY_ESCAPE ||
             event->key == TIKU_KEY_RETURN)) {
            tiku_ide_about_drop(st);
        }
        return 0;
    }
    if (window == st->splash_id && st->splash_surface != NULL) {
        tiku_ide_splash_drop(st);        /* a key or a click closes it */
        return 0;
    }
    if (st->ask.open && window == st->proj_id) {
        int at = -1;
        int tag = st->ask.tag;

        if (event->type == TIKU_EVENT_KEY_DOWN) {
            at = tiku_alert_key(&st->ask, event->key);
        } else if (event->type == TIKU_EVENT_POINTER_DOWN) {
            /* A press arms the button under it; the answer comes when
             * the press is let go over the same button. */
            if (tiku_alert_press(&st->ask, st->ask_frame.w, st->ask_frame.h,
                                 event->x - st->ask_frame.x,
                                 event->y - st->ask_frame.y)) {
                tiku_ide_project_paint(st);
            }
            return 0;
        } else if (event->type == TIKU_EVENT_POINTER_MOVE) {
            if (tiku_alert_track(&st->ask, st->ask_frame.w, st->ask_frame.h,
                                 event->x - st->ask_frame.x,
                                 event->y - st->ask_frame.y)) {
                tiku_ide_project_paint(st);
            }
            return 0;
        } else if (event->type == TIKU_EVENT_POINTER_UP) {
            at = tiku_alert_release(&st->ask, st->ask_frame.w,
                                    st->ask_frame.h,
                                    event->x - st->ask_frame.x,
                                    event->y - st->ask_frame.y);
            if (at < 0) {
                tiku_ide_project_paint(st);      /* the button comes back up */
                return 0;
            }
        }
        if (at < 0) {
            return 0;           /* not the question's key: ignored */
        }
        tiku_alert_reset(&st->ask);
        if (tag == CMD_QUIT) {
            edit_t *closing = tiku_ide_edit_of_id(st, st->closing_editor);
            if (closing && at == 1) {
                tiku_ide_recovery_release(closing, 1);
                tiku_editdoc_saved(closing->doc,
                                   tiku_editdoc_state(closing->doc));
                tiku_ide_edit_drop(st, closing);
            } else if (closing && at == 2) {
                if (tiku_ide_edit_save_or_ask(st, closing)) { tiku_ide_edit_drop(st, closing); }
                else { tiku_ide_edit_paint(st, closing); }
            }
            tiku_ide_project_paint(st);
            return 0;
        }
        if (tag == CMD_SAVE) {
            edit_t *page = tiku_ide_edit_of_id(st, st->overwrite_editor);

            if (page != NULL && at == 1) {
                page->disk_known = 0;   /* Save Over It: skip the disk check */
                (void)tiku_ide_edit_save(page);
                tiku_ide_edit_publish(st, page);
                tiku_ide_project_publish(st);
            }
            if (page != NULL) { tiku_ide_edit_paint(st, page); }
            tiku_ide_project_paint(st);
            return 0;
        }
        if (tag == CMD_SAVEBOARD) {
            if (at == 1) { tiku_ide_upload_confirm(st); }
            else { tiku_ide_project_paint(st); }
            return 0;
        }
        if (at == 1) {
            int i;
            for (i = 0; i < IDE_EDITS; i++) {
                tiku_ide_recovery_release(&st->edit[i], 1);
            }
            return 1;           /* Discard: the drafts go, the IDE closes */
        }
        tiku_ide_project_paint(st);      /* Cancel: nothing closes */
        return 0;
    }
    if (st->ask.open) { return 0; }
    if (event->type == TIKU_EVENT_RESIZE &&
        (window == st->con_id || window == st->msg_id)) {
        /* The console or the Messages window was resized: a surface of
         * the new size, never smaller than the window opened at. */
        int con = window == st->con_id;
        int least_w = con ? CON_W : MSG_W, least_h = con ? CON_H : MSG_H;
        int w = (event->width > least_w) ? event->width : least_w;
        int h = (event->height > least_h) ? event->height : least_h;
        int *pw = con ? &st->con_w : &st->msg_w;
        int *ph = con ? &st->con_h : &st->msg_h;
        tiku_surface_t **ps = con ? &st->con_surface : &st->msg_surface;

        if (*ps != NULL && (w != *pw || h != *ph)) {
            tiku_surface_t *grown =
                tiku_surface_new(w, h, con ? CON_BACK : TIKU_C_PANEL);

            if (grown != NULL) {
                tiku_surface_free(*ps);
                *ps = grown;
                *pw = w;
                *ph = h;
            }
        }
        if (con) { tiku_ide_console_paint(st); } else { tiku_ide_messages_paint(st); }
        return 0;
    }
    if (window == st->proj_id && event->type == TIKU_EVENT_RESIZE) {
        /* The Project window was resized: a surface of the new size,
         * never smaller than the window opened at. */
        int w = (event->width > IDE_W) ? event->width : IDE_W;
        int h = (event->height > IDE_H) ? event->height : IDE_H;
        tiku_surface_t *grown = (w != st->proj_w || h != st->proj_h)
            ? tiku_surface_new(w, h, TIKU_C_PANEL) : NULL;

        if (grown != NULL) {
            tiku_surface_free(st->proj_surface);
            st->proj_surface = grown;
            st->proj_w = w;
            st->proj_h = h;
        }
        tiku_ide_project_paint(st);
        return 0;
    }
    if (e != NULL && event->type == TIKU_EVENT_RESIZE) {
        /* The page was resized: it gets a surface of the new size, at
         * least 360 x 100, and is repainted; the desktop sends a resize
         * at each step of a drag. */
        int w = (event->width > 360) ? event->width : 360;
        int h = (event->height > 100) ? event->height : 100;
        tiku_surface_t *grown = tiku_surface_new(w, h,
                                                      TIKU_C_PANEL);

        if (grown != NULL) {
            if (h < 320) { tiku_ide_assist_close(e); e->finding = 0; }
            if (h < 260) { e->debug_show = e->watch_focus = 0; }
            tiku_surface_free(e->surface);
            e->surface = grown;
            e->w = w;
            e->h = h;
            tiku_ide_edit_paint(st, e);
        }
        return 0;
    }
    if (e != NULL && event->type == TIKU_EVENT_WHEEL) {
        /* The wheel scrolls the page and leaves the caret where it is. */
        (void)tiku_codeview_event(e->view, event);
        tiku_ide_edit_paint(st, e);
        return 0;
    }
    if (e != NULL) {
        /* An event for a page. */
        unsigned ch = (unsigned char)event->text[0];

        if (tiku_ide_debug_event(st, e, event)) {
            tiku_ide_edit_paint(st, e); tiku_ide_edit_publish(st, e); tiku_ide_project_publish(st);
            return 0;
        }
        if ((event->type == TIKU_EVENT_KEY_DOWN ||
             event->type == TIKU_EVENT_KEY_UP) && modifier_key(event->key)) {
            /* A modifier alone may make the jump target under the
             * pointer live or not; nothing moves. */
            if (tiku_codeview_event(e->view, event)) {
                tiku_ide_edit_paint(st, e);
            }
            return 0;
        }
        if (event->type == TIKU_EVENT_KEY_DOWN && (event->modifiers & TIKU_MOD_CMD)) {
            int cmd = event->key == 'd' ? CMD_DEBUG : event->key == 'b' ? CMD_BREAKPOINT :
                      event->key == 'j' ? CMD_DEBUG_STEP : event->key == 'g' ? CMD_DEBUG_CONT :
                      event->key == 'k' ? CMD_HELPWORD : event->key == ' ' ? CMD_COMPLETE : 0;
            if (cmd && e->lang == TIKU_SYNTAX_BASIC) { return tiku_ide_pick(st, window, cmd); }
        }
        if (tiku_ide_debug_locked(st, e)) {
            int tool = tiku_ide_tb_tool_at(e, event->x, event->y);
            if (event->type == TIKU_EVENT_POINTER_DOWN && tool >= 0 && tool != 2) { return 0; }
            if (event->type == TIKU_EVENT_KEY_DOWN &&
                !(event->key >= TIKU_KEY_UP && event->key <= TIKU_KEY_PAGE_DOWN) &&
                !((event->modifiers & TIKU_MOD_CMD) && (event->key == 'c' || event->key == '.')) &&
                !(e->assist_open && event->key == TIKU_KEY_ESCAPE)) { return 0; }
        }
        if (tiku_ide_assist_event(e, event)) {
            tiku_ide_edit_paint(st, e); tiku_ide_edit_publish(st, e); tiku_ide_project_publish(st);
            return 0;
        }
        if (event->type == TIKU_EVENT_POINTER_DOWN) {
            tiku_rect_t save_b = TB_SAVE;
            tiku_rect_t run_b = TB_RUN;
            uint64_t generation = tiku_editdoc_generation(e->doc);

            if (tiku_ide_search_pointer(e, event->x, event->y)) {
                tiku_ide_edit_paint(st, e);
                tiku_ide_edit_publish(st, e);
                tiku_ide_project_publish(st);
                return 0;
            }
            if (event->x >= save_b.x && event->x < save_b.x + save_b.w &&
                event->y >= save_b.y && event->y < save_b.y + save_b.h) {
                (void)tiku_ide_edit_save_or_ask(st, e);
                tiku_ide_edit_paint(st, e);
                tiku_ide_edit_publish(st, e);
                tiku_ide_project_publish(st);
                return 0;
            }
            if (e->lang == TIKU_SYNTAX_BASIC &&
                event->x >= run_b.x && event->x < run_b.x + run_b.w &&
                event->y >= run_b.y && event->y < run_b.y + run_b.h) {
                /* A run reads the file on disk, so the button saves
                 * first.  The page is painted after the run starts, so
                 * the Stop tool shows red. */
                (void)tiku_ide_edit_save(e);
                tiku_ide_run_file_where(st, e->path);
                tiku_ide_edit_paint(st, e);
                tiku_ide_edit_publish(st, e);
                tiku_ide_project_publish(st);
                return 0;
            }
            {
                /* Undo, redo and find. */
                tiku_rect_t u = TB_UNDO, r = TB_REDO, fb = TB_FIND;
                int hit_u = event->x >= u.x && event->x < u.x + u.w &&
                            event->y >= u.y && event->y < u.y + u.h;
                int hit_r = event->x >= r.x && event->x < r.x + r.w &&
                            event->y >= r.y && event->y < r.y + r.h;
                int hit_f = event->x >= fb.x && event->x < fb.x + fb.w &&
                            event->y >= fb.y && event->y < fb.y + fb.h;

                if (hit_u || hit_r) {
                    if (tiku_ide_edit_clip(st, e, hit_u ? CMD_UNDO : CMD_REDO)) {
                        tiku_ide_edit_reveal(e);
                        tiku_ide_edit_paint(st, e);
                        tiku_ide_edit_publish(st, e);
                        tiku_ide_project_publish(st);
                    }
                    return 0;
                }
                if (hit_f) {
                    if (e->finding) { e->finding = 0; }
                    else { tiku_ide_search_open(e, 0); }
                    tiku_ide_edit_paint(st, e);
                    return 0;
                }
            }
            {
                int tool = tiku_ide_tb_tool_at(e, event->x, event->y);

                if (tool == 2 && e->lang == TIKU_SYNTAX_BASIC) {
                    /* Stop. */
                    tiku_ide_run_stop(st, "? the run was stopped by hand");
                    tiku_ide_edit_paint(st, e);
                    return 0;
                }
                if (tool == 1 && e->lang == TIKU_SYNTAX_C) {
                    /* On a C page the Run tool is Build; the page is
                     * saved first. */
                    (void)tiku_ide_edit_save(e);
                    tiku_ide_edit_paint(st, e);
                    tiku_ide_edit_publish(st, e);
                    tiku_ide_build_project(st);
                    tiku_ide_project_publish(st);
                    return 0;
                }
                if (tool == 6 && e->lang == TIKU_SYNTAX_BASIC) {
                    tiku_ide_renumber_basic(e);
                    tiku_ide_edit_paint(st, e);
                    tiku_ide_edit_publish(st, e);
                    tiku_ide_project_publish(st);
                    return 0;
                }
            }
            /* A press on the scrollbar's track pages the view toward the
             * press and leaves the caret where it is. */
            if (event->x >= e->w - PAGE_SB &&
                event->y >= tiku_ide_edit_page_top(e) && event->y < tiku_ide_edit_page_bottom(e)) {
                int64_t rows = tiku_codeview_rows_visible(e->view);
                int64_t top = tiku_codeview_top(e->view);
                int ptop = tiku_ide_edit_page_top(e);
                int mid = ptop + (tiku_ide_edit_page_bottom(e) - ptop) / 2;
                int64_t step = (rows > 1) ? rows - 1 : 1;

                tiku_codeview_scroll_to(e->view,
                    top + ((event->y < mid) ? -step : step),
                    tiku_codeview_left(e->view));
                tiku_ide_edit_paint(st, e);
                return 0;
            }
            /* Not a button or the bar: the page itself, where the view
             * puts the caret, selects words and lines and starts a drag,
             * and a shown list or tip takes the presses on it. */
            if (event->x < e->w - PAGE_SB &&
                event->y >= tiku_ide_edit_page_top(e) &&
                event->y < tiku_ide_edit_page_bottom(e) &&
                tiku_codeview_event(e->view, event)) {
                tiku_ide_edit_paint(st, e);
                if (tiku_codeview_status(e->view) == TIKU_EDIT_OK &&
                    tiku_editdoc_generation(e->doc) != generation) {
                    tiku_ide_edit_publish(st, e);
                    tiku_ide_project_publish(st);
                }
            }
            return 0;
        }
        if (event->type == TIKU_EVENT_POINTER_MOVE) {
            /* Over the toolbar, a tip names the tool under the pointer;
             * it goes when the pointer leaves the tool. */
            int tool = (event->y < TOOLBAR_H)
                           ? tiku_ide_tb_tool_at(e, event->x, event->y) : -1;

            if (tool != e->tip) {
                e->tip = tool;
                tiku_ide_edit_paint(st, e);
            }
            /* A drag from a page press extends the selection to the
             * pointer; a move with no such press is not the view's. */
            if (tiku_codeview_event(e->view, event)) {
                tiku_ide_edit_paint(st, e);
            }
            return 0;
        }
        if (event->type == TIKU_EVENT_POINTER_UP) {
            if (tiku_codeview_event(e->view, event)) {
                tiku_ide_edit_paint(st, e);
            }
            return 0;
        }
        if (ch == 0u) {
            ch = event->key;
        }
        if (event->type != TIKU_EVENT_KEY_DOWN) {
            return 0;
        }
        return page_key(st, e, event, ch);
    }
        if (window == st->con_id && st->con_surface != NULL) {
        if (event->type == TIKU_EVENT_WHEEL) {
            int rows = (st->con_h - STRIP_H - 2 * MARGIN) /
                       (tiku_font_mono(0)->height + 2);
            int top = (st->con_top >= 0) ? st->con_top : st->ncon - rows;

            top += (event->dy > 0) ? 3 : -3;
            if (top >= st->ncon - rows) {
                st->con_top = -1;   /* at the tail: follow new output */
            } else {
                st->con_top = (top < 0) ? 0 : top;
            }
            tiku_ide_console_paint(st);
            return 0;
        }
        if (event->type != TIKU_EVENT_KEY_DOWN) {
            return 0;
        }
        if ((event->modifiers & TIKU_MOD_CMD) &&
            (event->key == 'c' || event->text[0] == 'c') && st->running) {
            if (st->local_debug) {
                tiku_ide_run_stop(st, "? debug run stopped");
                tiku_ide_console_paint(st);
            } else if (st->services->board_stop != NULL) {
                (void)st->services->board_stop(st->services->ctx);
            }
            return 0;
        }
        if (st->asking) {
            unsigned ch = (unsigned char)event->text[0];

            if (ch == 0u) {
                ch = event->key;
            }
            if (event->key == TIKU_KEY_RETURN) {
                /* The prompt and the answer go into the console. */
                char echo[200];

                snprintf(echo, sizeof echo, "%s%s", st->ask_prompt,
                         st->answer.text);
                tiku_ide_console_take(st, echo, 0);
                if (st->local_debug) {
                    (void)tiku_board_basic_input(st->local_debug, st->answer.text);
                } else if (st->services->board_input != NULL) {
                    (void)st->services->board_input(st->services->ctx,
                                                    st->answer.text);
                }
                st->asking = 0;
                tiku_field_stop(&st->answer);
            } else if (event->key == TIKU_KEY_BACKSPACE) {
                (void)tiku_field_key(&st->answer, TIKU_FIELD_BACKSPACE, 0,
                                     '\0');
            } else if (event->key == TIKU_KEY_LEFT) {
                (void)tiku_field_key(&st->answer, TIKU_FIELD_LEFT, 0, '\0');
            } else if (event->key == TIKU_KEY_RIGHT) {
                (void)tiku_field_key(&st->answer, TIKU_FIELD_RIGHT, 0, '\0');
            } else if (ch >= 32u && ch < 127u &&
                       !(event->modifiers & TIKU_MOD_CMD)) {
                (void)tiku_field_key(&st->answer, TIKU_FIELD_CHAR, 0,
                                     (char)ch);
            }
            tiku_ide_console_paint(st);
        }
        return 0;
    }
    if (window == st->msg_id && st->msg_surface != NULL) {
        tiku_rect_t body = { MARGIN, MARGIN, st->msg_w - 2 * MARGIN,
                                  st->msg_h - STRIP_H - 2 * MARGIN };
        if (event->type == TIKU_EVENT_POINTER_DOWN) {
            tiku_list_click(&st->msg_list,
                            tiku_list_at(&st->msg_list, tiku_inset(body, 2),
                                         event->x, event->y),
                            event->modifiers);
            tiku_ide_messages_show(st);
            return 0;
        }
                if (st->asking) {
            /* The strip has the keys while the program waits: Return
             * sends the line, the rest edit it. */
            unsigned ch = (unsigned char)event->text[0];

            if (ch == 0u) {
                ch = event->key;
            }
            if (event->key == TIKU_KEY_RETURN) {
                if (st->services->board_input != NULL) {
                    (void)st->services->board_input(st->services->ctx,
                                                    st->answer.text);
                }
                st->asking = 0;
                tiku_field_stop(&st->answer);
            } else if (event->key == TIKU_KEY_BACKSPACE) {
                (void)tiku_field_key(&st->answer, TIKU_FIELD_BACKSPACE, 0,
                                     '\0');
            } else if (event->key == TIKU_KEY_LEFT) {
                (void)tiku_field_key(&st->answer, TIKU_FIELD_LEFT, 0, '\0');
            } else if (event->key == TIKU_KEY_RIGHT) {
                (void)tiku_field_key(&st->answer, TIKU_FIELD_RIGHT, 0, '\0');
            } else if (ch >= 32u && ch < 127u &&
                       !(event->modifiers & TIKU_MOD_CMD)) {
                (void)tiku_field_key(&st->answer, TIKU_FIELD_CHAR, 0,
                                     (char)ch);
            }
            tiku_ide_messages_paint(st);
            return 0;
        }
        if (event->key == TIKU_KEY_RETURN) {
            /* Return follows the chosen message; the list leaves Return
             * to the application. */
            tiku_ide_message_follow(st);
            return 0;
        }
        if (tiku_list_key(&st->msg_list, event->key, event->modifiers,
                          tiku_inset(body, 2))) {
            tiku_ide_messages_show(st);
        }
        return 0;
    }
    if (window != st->proj_id) {
        return 0;
    }
    if (event->type == TIKU_EVENT_POINTER_DOWN) {
        int row = tiku_list_at(&st->tv_list, tiku_ide_project_list_area(st), event->x,
                               event->y);

        /* A second click on the chosen row within 400 ms opens it, as
         * Return does. */
        if (row >= 0 && row == st->click_row &&
            event->time_us - st->click_at < 400000 &&
            tiku_list_chosen(&st->tv_list) == row) {
            st->click_row = -1;
            tiku_ide_open_chosen(st);
            return 0;
        }
        st->click_row = row;
        st->click_at = event->time_us;
        tiku_list_click(&st->tv_list, row, event->modifiers);
        tiku_ide_project_paint(st);
        tiku_ide_project_publish(st);    /* Run's row names the chosen row */
        return 0;
    }
    if (event->key == TIKU_KEY_RETURN) {
        /* The list leaves Return to the application, which opens the
         * chosen row. */
        tiku_ide_open_chosen(st);
        return 0;
    }
    if (tiku_list_key(&st->tv_list, event->key, event->modifiers,
                      tiku_ide_project_list_area(st))) {
        tiku_ide_project_paint(st);
        tiku_ide_project_publish(st);    /* Run's row names the chosen row */
    }
    return 0;
}
