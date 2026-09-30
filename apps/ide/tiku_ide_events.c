/* tiku_ide_events.c -- events.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

/**
 * @brief Name what closing would throw away, or say there is nothing.
 *
 * @return the number of documents holding unsaved work.
 */
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

        if (e->surface == NULL || !tiku_textview_modified(&e->tv)) {
            continue;
        }
        used += (size_t)snprintf(out + used, (used < max) ? max - used : 0u,
                                 "%s%s", (n > 0) ? ", " : "", e->name);
        n++;
    }
    return n;
}

int
tiku_ide_event(void *state, uint32_t window, const tiku_event_t *event)
{
    ide_t *st = state;
    edit_t *e = tiku_ide_edit_of_id(st, window);

    if (e != NULL) {
        int result = tiku_text_input_event(&e->commit, event);
        if (event->type == TIKU_EVENT_TEXT_INPUT) {
            if ((tiku_ide_debug_locked(st, e) && !e->watch_focus) || st->ask.open) return 0;
            if (result == TIKU_TEXT_REJECTED) {
                snprintf(e->feedback, sizeof e->feedback, "Text input is invalid or too long.");
            } else if (result == TIKU_TEXT_COMPLETE) {
                int accepted;
                if (e->finding || e->watch_focus) {
                    tiku_field_t *field = e->finding
                        ? (e->find_focus ? &e->replacement : &e->find) : &e->watches;
                    accepted = tiku_field_commit(field, e->commit.text) != TIKU_FIELD_REJECTED;
                } else {
                    e->assist_open = 0;
                    accepted = tiku_textview_commit(&e->tv, e->commit.text);
                    (void)tiku_textview_reveal(&e->tv, tiku_ide_edit_rows(e));
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
        event->type != TIKU_EVENT_POINTER_DOWN &&
        event->type != TIKU_EVENT_POINTER_MOVE &&
        event->type != TIKU_EVENT_POINTER_UP &&
        event->type != TIKU_EVENT_RESIZE &&
        event->type != TIKU_EVENT_WHEEL) {
        return 0;
    }
    if (window == st->about_id && st->about_surface != NULL) {
        if (event->type == TIKU_EVENT_POINTER_DOWN) {
            /* A plain panel wears no close button: a click IS the
             * "read it, done" the old About boxes answered to. */
            st->services->close(st->services->ctx, st->about_id);
            return 0;
        }
        if (event->type == TIKU_EVENT_KEY_DOWN &&
            (event->key == TIKU_KEY_ESCAPE ||
             event->key == TIKU_KEY_RETURN)) {
            tiku_ide_about_drop(st);     /* read, and put away */
        }
        return 0;
    }
    if (window == st->splash_id && st->splash_surface != NULL) {
        tiku_ide_splash_drop(st);        /* a person already working is not an
                                 * audience */
        return 0;
    }
    if (st->ask.open && window == st->proj_id) {
        int at = -1;
        int tag = st->ask.tag;

        if (event->type == TIKU_EVENT_KEY_DOWN) {
            at = tiku_alert_key(&st->ask, event->key);
        } else if (event->type == TIKU_EVENT_POINTER_DOWN) {
            /* Armed, not answered: the button goes down under the hand
             * and answers when the hand lets go on it. */
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
                tiku_textview_saved(&closing->tv);
                tiku_ide_edit_drop(st, closing);
            } else if (closing && at == 2) {
                if (tiku_ide_edit_save(closing)) { tiku_ide_edit_drop(st, closing); }
                else { tiku_ide_edit_paint(st, closing); }
            }
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
            return 1;           /* Discard: the project, and all of it */
        }
        tiku_ide_project_paint(st);      /* Cancel: everything stands */
        return 0;
    }
    if (st->ask.open) { return 0; }
    if (event->type == TIKU_EVENT_RESIZE &&
        (window == st->con_id || window == st->msg_id)) {
        /* The console's or the messages' corner: more lines in view,
         * never fewer than the window opened with. */
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
        /* The project's corner: more rows in view, never fewer than
         * the window opened with. */
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
        /* The corner moved: repaint at the size the window now is, and
         * the next frame hands it over.  This arrives on every step of
         * the drag, so the page tracks the frame. */
        int w = (event->width > 360) ? event->width : 360;
        int h = (event->height > 100) ? event->height : 100;
        tiku_surface_t *grown = tiku_surface_new(w, h,
                                                      TIKU_C_PANEL);

        if (grown != NULL) {
            if (h < 320) { e->assist_open = e->finding = 0; }
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
        /* The wheel scrolls the page and leaves the caret where it is,
         * the way the scrollbar's track does: a person reading ahead
         * has not moved where they type. */
        int rows = tiku_ide_edit_rows(e);
        int top = tiku_textview_top(&e->tv);

        (void)tiku_textview_scroll_to(&e->tv, top + event->dy, rows);
        tiku_ide_edit_paint(st, e);
        return 0;
    }
    if (e != NULL) {
        /* An editor window: the document takes it. */
        unsigned ch = (unsigned char)event->text[0];

        if (tiku_ide_debug_event(st, e, event)) {
            tiku_ide_edit_paint(st, e); tiku_ide_edit_publish(st, e); tiku_ide_project_publish(st);
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

            if (tiku_ide_search_pointer(e, event->x, event->y)) {
                tiku_ide_edit_paint(st, e);
                tiku_ide_edit_publish(st, e);
                tiku_ide_project_publish(st);
                return 0;
            }
            if (event->x >= save_b.x && event->x < save_b.x + save_b.w &&
                event->y >= save_b.y && event->y < save_b.y + save_b.h) {
                (void)tiku_ide_edit_save(e);
                tiku_ide_edit_paint(st, e);
                tiku_ide_edit_publish(st, e);
                tiku_ide_project_publish(st);
                return 0;
            }
            if (e->lang == TIKU_SYNTAX_BASIC &&
                event->x >= run_b.x && event->x < run_b.x + run_b.w &&
                event->y >= run_b.y && event->y < run_b.y + run_b.h) {
                /* What is run is what is on disk, so the button saves
                 * first -- the same rule the menu's Run keeps.  Painted
                 * AFTER the run starts, so the stop square is already
                 * red when the eye comes looking for it. */
                (void)tiku_ide_edit_save(e);
                tiku_ide_run_file_where(st, e->path);
                tiku_ide_edit_paint(st, e);
                tiku_ide_edit_publish(st, e);
                tiku_ide_project_publish(st);
                return 0;
            }
            {
                /* The new tools: undo, redo, and find. */
                tiku_rect_t u = TB_UNDO, r = TB_REDO, fb = TB_FIND;
                int hit_u = event->x >= u.x && event->x < u.x + u.w &&
                            event->y >= u.y && event->y < u.y + u.h;
                int hit_r = event->x >= r.x && event->x < r.x + r.w &&
                            event->y >= r.y && event->y < r.y + r.h;
                int hit_f = event->x >= fb.x && event->x < fb.x + fb.w &&
                            event->y >= fb.y && event->y < fb.y + fb.h;

                if (hit_u || hit_r) {
                    if (tiku_ide_edit_clip(st, e, hit_u ? CMD_UNDO : CMD_REDO)) {
                        (void)tiku_textview_reveal(&e->tv, tiku_ide_edit_rows(e));
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
                    /* Stop: the person's hand on a run that will not
                     * end on its own. */
                    tiku_ide_run_stop(st, "? the run was stopped by hand");
                    tiku_ide_edit_paint(st, e);
                    return 0;
                }
                if (tool == 1 && e->lang == TIKU_SYNTAX_C) {
                    /* Build, the C page's run: saved first, the same
                     * rule Run keeps. */
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
            /* A press on the scrollbar's track PAGES the view toward
             * the hand -- the same gesture the panels and folders
             * answer -- and moves the page WITHOUT the caret, because a
             * person looking elsewhere has not moved where they type. */
            if (event->x >= e->w - PAGE_SB &&
                event->y >= tiku_ide_edit_page_top(e) && event->y < tiku_ide_edit_page_bottom(e)) {
                int rows = tiku_ide_edit_rows(e);
                int top = tiku_textview_top(&e->tv);
                int ptop = tiku_ide_edit_page_top(e);
                int mid = ptop + (tiku_ide_edit_page_bottom(e) - ptop) / 2;
                int step = (rows > 1) ? rows - 1 : 1;

                (void)tiku_textview_scroll_to(&e->tv,
                    top + ((event->y < mid) ? -step : step), rows);
                tiku_ide_edit_paint(st, e);
                return 0;
            }
            /* Not a button or the bar: the page itself, so the caret
             * goes where the pointer is.  The press ANCHORS a selection
             * there -- a drag from here grows it, and a click that does
             * not move leaves anchor and caret together, which is none. */
            if (event->x < e->w - PAGE_SB &&
                event->y >= tiku_ide_edit_page_top(e) && event->y < tiku_ide_edit_page_bottom(e)) {
                tiku_ide_edit_place_from_xy(e, event->x, event->y);
                tiku_textview_select_none(&e->tv);
                tiku_textview_select_begin(&e->tv);
                e->drag_select = 1;
                (void)tiku_textview_reveal(&e->tv, tiku_ide_edit_rows(e));
                tiku_ide_edit_paint(st, e);
            }
            return 0;
        }
        if (event->type == TIKU_EVENT_POINTER_MOVE) {
            /* Over the toolbar the pointer NAMES the tool it rests on:
             * the tip appears when it arrives and leaves when it does. */
            int tool = (event->y < TOOLBAR_H)
                           ? tiku_ide_tb_tool_at(e, event->x, event->y) : -1;

            if (tool != e->tip) {
                e->tip = tool;
                tiku_ide_edit_paint(st, e);
            }
            /* A drag from a page press extends the selection to the
             * pointer; a move with no such press is not ours. */
            if (e->drag_select &&
                event->x < e->w - PAGE_SB &&
                event->y >= tiku_ide_edit_page_top(e) && event->y < tiku_ide_edit_page_bottom(e)) {
                tiku_ide_edit_place_from_xy(e, event->x, event->y);
                (void)tiku_textview_reveal(&e->tv, tiku_ide_edit_rows(e));
                tiku_ide_edit_paint(st, e);
            }
            return 0;
        }
        if (event->type == TIKU_EVENT_POINTER_UP) {
            int y0, x0, y1, x1;

            e->drag_select = 0;
            /* A press that did not drag leaves no anchor behind, or a
             * later caret move would select from the click point. */
            if (!tiku_textview_selection(&e->tv, &y0, &x0, &y1, &x1)) {
                tiku_textview_select_none(&e->tv);
            }
            return 0;
        }
        if (ch == 0u) {
            ch = event->key;
        }
        if (e->finding) {
            if (event->type != TIKU_EVENT_KEY_DOWN) { return 0; }
            tiku_ide_search_key(st, e, event);
            tiku_ide_edit_paint(st, e);
            tiku_ide_edit_publish(st, e);
            return 0;
        }
        {
            int rows = tiku_ide_edit_rows(e);
            int edited = 0;
            int shift = (event->modifiers & TIKU_MOD_SHIFT) != 0;
            int is_move =
                event->key == TIKU_KEY_LEFT ||
                event->key == TIKU_KEY_RIGHT ||
                event->key == TIKU_KEY_UP ||
                event->key == TIKU_KEY_DOWN ||
                event->key == TIKU_KEY_HOME ||
                event->key == TIKU_KEY_END ||
                event->key == TIKU_KEY_PAGE_UP ||
                event->key == TIKU_KEY_PAGE_DOWN;

            /* Cmd-F opens the find bar, which then has the keys. */
            if ((event->modifiers & TIKU_MOD_CMD) &&
                (event->key == 'f' || event->key == 'h')) {
                tiku_ide_search_open(e, event->key == 'h');
                tiku_ide_edit_paint(st, e);
                return 0;
            }
            /* The file keys: Cmd-S saves, Cmd-W closes THIS window,
             * Cmd-R runs the page's own program, Cmd-. stops a run --
             * each the key its menu row says. */
            if ((event->modifiers & TIKU_MOD_CMD) && event->key == 's') {
                (void)tiku_ide_edit_save(e);
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
            /* The desk's editing keys -- Cmd X/C/V/A, and Cmd-Z /
             * Cmd-Shift-Z for undo and redo -- caught before the
             * switch, so their letters are never typed. */
            if ((event->modifiers & TIKU_MOD_CMD) &&
                (event->key == 'x' || event->key == 'c' ||
                 event->key == 'v' || event->key == 'a' ||
                 event->key == 'z')) {
                int cmd = (event->key == 'x') ? CMD_CUT
                        : (event->key == 'c') ? CMD_COPY
                        : (event->key == 'v') ? CMD_PASTE
                        : (event->key == 'a') ? CMD_SELALL
                        : ((event->modifiers & TIKU_MOD_SHIFT)
                               ? CMD_REDO : CMD_UNDO);
                int did = tiku_ide_edit_clip(st, e, cmd);

                if (did && cmd != CMD_COPY && cmd != CMD_SELALL) {
                    (void)tiku_textview_reveal(&e->tv, rows);
                    tiku_ide_edit_publish(st, e);
                    tiku_ide_project_publish(st);
                }
                tiku_ide_edit_paint(st, e);
                return 0;
            }
            /* A move with Shift GROWS the selection from where it was
             * anchored; a move without lets any selection go.  The
             * editing keys need no such care -- the view removes a
             * selection before it inserts, breaks or rubs out. */
            if (is_move) {
                if (shift) {
                    tiku_textview_select_begin(&e->tv);
                } else {
                    tiku_textview_select_none(&e->tv);
                }
            }

            switch (event->key) {
            case TIKU_KEY_LEFT:
                tiku_textview_move(&e->tv, TIKU_TEXTVIEW_LEFT);
                break;
            case TIKU_KEY_RIGHT:
                tiku_textview_move(&e->tv, TIKU_TEXTVIEW_RIGHT);
                break;
            case TIKU_KEY_UP:
                tiku_textview_move(&e->tv, TIKU_TEXTVIEW_UP);
                break;
            case TIKU_KEY_DOWN:
                tiku_textview_move(&e->tv, TIKU_TEXTVIEW_DOWN);
                break;
            case TIKU_KEY_HOME:
                tiku_textview_move(&e->tv, TIKU_TEXTVIEW_HOME);
                break;
            case TIKU_KEY_END:
                tiku_textview_move(&e->tv, TIKU_TEXTVIEW_END);
                break;
            case TIKU_KEY_PAGE_UP:
            case TIKU_KEY_PAGE_DOWN: {
                /* A screenful, the caret keeping its column where the
                 * line it lands on is long enough to hold it. */
                int cy, cx;

                tiku_textview_caret(&e->tv, &cy, &cx);
                cy += (event->key == TIKU_KEY_PAGE_UP) ? -rows : rows;
                tiku_textview_place(&e->tv, cy, cx);
                break;
            }
            case TIKU_KEY_DELETE:
                tiku_textview_delete(&e->tv);
                edited = 1;
                break;
            case TIKU_KEY_BACKSPACE:
                tiku_textview_backspace(&e->tv);
                edited = 1;
                break;
            case TIKU_KEY_RETURN: {
                int cy, cx, n;

                tiku_textview_caret(&e->tv, &cy, &cx);
                tiku_ide_edit_normalize_word(e);   /* the return ends a word */
                tiku_textview_newline(&e->tv);
                /* The next line's number, already typed: the numbers
                 * are the language's bookkeeping, and bookkeeping is
                 * what a machine is FOR.  A line wearing none offers
                 * none, so prose in a .bas file is not fought over. */
                if (e->lang == TIKU_SYNTAX_BASIC) {
                    n = tiku_ide_next_number(&e->tv, cy);
                    if (n > 0) {
                        tiku_ide_type_number(&e->tv, n);
                    }
                }
                edited = 1;
                break;
            }
            default:
                if (ch >= 32u && ch < 127u) {
                    /* A non-word character ENDS the word before it, and
                     * a keyword there shouts before the character lands. */
                    if (!tiku_ide_norm_is_word((int)ch)) {
                        tiku_ide_edit_normalize_word(e);
                    }
                    tiku_textview_insert(&e->tv, (char)ch);
                    edited = 1;
                } else {
                    return 0;   /* not the editor's key */
                }
                break;
            }
            /* The caret is followed after EVERY move, so typing at the
             * bottom of a long file never runs off the page edge. */
            (void)tiku_textview_reveal(&e->tv, rows);
            tiku_ide_edit_paint(st, e);
            if (edited) {
                e->feedback[0] = '\0';
                tiku_ide_edit_publish(st, e);
                tiku_ide_project_publish(st);   /* the mark follows the document */
            }
            return 0;
        }
    }
        if (window == st->con_id && st->con_surface != NULL) {
        if (event->type == TIKU_EVENT_WHEEL) {
            int rows = (st->con_h - STRIP_H - 2 * MARGIN) /
                       (tiku_font_mono(0)->height + 2);
            int top = (st->con_top >= 0) ? st->con_top : st->ncon - rows;

            top += (event->dy > 0) ? 3 : -3;
            if (top >= st->ncon - rows) {
                st->con_top = -1;   /* back at the tail: it follows */
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
                /* The answer is echoed, as a terminal would. */
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
            /* What FOLLOWING a message means is the application's, the
             * same way opening a row is. */
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

        /* The second click of two on the same row OPENS it, exactly as
         * Return does: a person with a mouse is owed the same door. */
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
        tiku_ide_project_publish(st);    /* Run follows the row it would run */
        return 0;
    }
    if (event->key == TIKU_KEY_RETURN) {
        /* What opening a row MEANS is the application's, which is why
         * the list hands Return back rather than taking it. */
        tiku_ide_open_chosen(st);
        return 0;
    }
    if (tiku_list_key(&st->tv_list, event->key, event->modifiers,
                      tiku_ide_project_list_area(st))) {
        tiku_ide_project_paint(st);
        tiku_ide_project_publish(st);    /* Run follows the row it would run */
    }
    return 0;
}
