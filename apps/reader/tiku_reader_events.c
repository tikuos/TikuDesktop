/* tiku_reader_events.c -- events.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_reader_private.h"

static void invoke(tiku_reader_help_window_t *c, int command);

/** @brief What a command does, whether it was clicked or typed. */
static void invoke(tiku_reader_help_window_t *c, int command)
{
    if (c->document_mode && tiku_reader_document_action(c, command)) return;
    if (tiku_reader_action(c, command)) return;
    switch (command) {
    case HELP_BACK:    (void)tiku_reader_help_window_back(c); break;
    case HELP_FORWARD: (void)tiku_reader_help_window_forward(c); break;
    case HELP_HOME:    (void)tiku_reader_help_window_home(c); break;
    case HELP_DONE:
        if (c->document_mode) (void)tiku_reader_document_window_reload(c);
        else tiku_reader_help_window_close(c);
        break;
    default: break;
    }
}

/** @brief Published native menus use the very same toolbar commands. */
void tiku_reader_document_menu_pick(tiku_window_t *w, int command, void *ctx)
{
    tiku_reader_help_window_t *c = ctx;
    (void)w;
    if (command == 100) tiku_reader_help_window_close(c);
    else if (command == HELP_FIND) tiku_reader_document_find(c, 1);
    else invoke(c, command);
    if (c->dirty) *c->dirty = 1;
}

/** @brief Reflect history and reload availability in the desktop menu. */
void tiku_reader_document_menus(tiku_reader_help_window_t *c)
{
    if (!c->window) return;
    tiku_menuset_t set = { .nmenu = 3 };
    const char *titles[] = {"File", "Edit", "View"};
    const char *labels[][6] = {
        {"reader.open", "reader.reload", "reader.close"},
        {"reader.find", "help.copy", "help.copy_code", "help.copy_link",
         "reader.search_folder"},
        {"button.back", "button.forward", "help.outline", "help.bookmarks",
         "reader.bookmark", "reader.location_focus"}
    };
    const int ids[][6] = {{HELP_HOME, HELP_DONE, 100},
        {HELP_FIND, HELP_COPY, HELP_CODE, HELP_LINK, HELP_SEARCH},
        {HELP_BACK, HELP_FORWARD, HELP_OUTLINE, HELP_BOOKMARKS,
         HELP_BOOKMARK, HELP_LOCATION}};
    const char keys[][6] = {{'o','r','w'}, {'f','c',0,0,0}, {0,0,0,0,'d','l'}};
    const int counts[] = {3, 5, 6};
    for (int m = 0; m < 3; m++) {
        tiku_menu_list_t *menu = &set.menu[m];
        snprintf(menu->title, sizeof menu->title, "%s", titles[m]);
        menu->nitem = counts[m];
        for (int i = 0; i < menu->nitem; i++) {
            tiku_menu_entry_t *item = &menu->item[i];
            snprintf(item->label, sizeof item->label, "%s",
                     tiku_str(labels[m][i]));
            item->command = ids[m][i]; item->enabled = 1;
            item->sc = keys[m][i]; item->mods = item->sc ? TIKU_MOD_CMD : 0;
            if (item->command == HELP_BACK) item->enabled = c->nback > 0;
            if (item->command == HELP_FORWARD) item->enabled = c->nforward > 0;
            if (item->command == HELP_DONE) item->enabled = c->page[0] == '/';
            if (item->command == HELP_HOME)
                item->enabled = c->choose_file != NULL;
            if (c->reader && item->command == HELP_OUTLINE)
                item->marked = c->reader->side == 1;
            if (c->reader && item->command == HELP_BOOKMARKS)
                item->marked = c->reader->side == 3;
        }
    }
    tiku_window_publish_menus(c->window, &set, tiku_reader_document_menu_pick, c);
}

/** @brief Keys the page answers first, then the panel's own controls. */
int tiku_reader_window_event(tiku_window_t *window, const tiku_event_t *ev, void *ctx)
{
    tiku_reader_help_window_t *c = ctx;
    help_layout_t l = tiku_reader_layout(c, tiku_window_content(window));
    tiku_dialog_result_t r;
    int taken = 0;

    c->hover_damage = (tiku_rect_t){0};
    if (tiku_reader_document_hover(c, ev, l)) return 1;
    tiku_reader_refit(c, l);
    tiku_reader_controls(c, l);
    if (c->document_mode && tiku_reader_document_event(c, ev, l)) {
        if (c->dirty) *c->dirty = 1;
        return 1;
    }
    if (c->document_mode && ev->type == TIKU_EVENT_KEY_DOWN) {
        int cmd = (ev->modifiers & TIKU_MOD_CMD) != 0;
        if (cmd && (ev->key == 'o' || ev->key == 'O')) {
            invoke(c, HELP_HOME); return 1;
        }
        if (cmd && (ev->key == 'r' || ev->key == 'R')) {
            invoke(c, HELP_DONE); return 1;
        }
        if ((ev->modifiers & TIKU_MOD_OPTION) &&
            (ev->key == TIKU_KEY_LEFT || ev->key == TIKU_KEY_RIGHT)) {
            invoke(c, ev->key == TIKU_KEY_LEFT ? HELP_BACK : HELP_FORWARD);
            return 1;
        }
    }
    if (tiku_reader_tools_event(c, ev, l)) {
        if (c->dirty) *c->dirty = 1;
        return 1;
    }
    if (ev->type == TIKU_EVENT_KEY_DOWN && c->doc) {
        int page = tiku_scroll_page(&c->bar), line = tiku_scroll_line(&c->bar);

        switch (ev->key) {
        case TIKU_KEY_DOWN:  tiku_scroll_to(&c->bar, c->bar.value + line);
                             taken = 1; break;
        case TIKU_KEY_UP:    tiku_scroll_to(&c->bar, c->bar.value - line);
                             taken = 1; break;
        case TIKU_KEY_PAGE_DOWN:
                             tiku_scroll_to(&c->bar, c->bar.value + page);
                             taken = 1; break;
        case TIKU_KEY_PAGE_UP:
                             tiku_scroll_to(&c->bar, c->bar.value - page);
                             taken = 1; break;
        case TIKU_KEY_HOME:  tiku_scroll_to(&c->bar, 0); taken = 1; break;
        case TIKU_KEY_END:   tiku_scroll_to(&c->bar, tiku_scroll_max(&c->bar));
                             taken = 1; break;
        case TIKU_KEY_BACKSPACE:
                             (void)tiku_reader_help_window_back(c);
                             taken = 1; break;
        case TIKU_KEY_TAB:
            if (tiku_dialog_focused(&c->dialog, HELP_PAGE) ||
                c->dialog.focus == HELP_PAGE) {
                int back = (ev->modifiers & TIKU_MOD_SHIFT) != 0;
                int next = tiku_doc_link_next(c->doc, c->focus_link, back);

                /* The walk hands the Tab on when it comes round, so the
                 * buttons are still reachable from the keyboard. */
                if (next != TIKU_DOC_NONE &&
                    (c->focus_link == TIKU_DOC_NONE ||
                     (back ? next < c->focus_link : next > c->focus_link))) {
                    c->focus_link = next;
                    tiku_reader_reveal(c, l);
                    taken = 1;
                } else {
                    c->focus_link = TIKU_DOC_NONE;
                }
            }
            break;
        case TIKU_KEY_RETURN:
            if (c->focus_link != TIKU_DOC_NONE &&
                c->dialog.focus == HELP_PAGE) {
                (void)tiku_reader_help_window_go(c,
                    tiku_doc_link_href(c->doc, c->focus_link));
                taken = 1;
            }
            break;
        default: break;
        }
    }
    if (!taken) {
        r = tiku_dialog_event(&c->dialog, ev);
        if (r.activated != TIKU_DIALOG_NONE) {
            invoke(c, r.activated);
            if (c->dirty) *c->dirty = 1;
            return 1;
        }
        taken = r.taken;
    }
    if (ev->type == TIKU_EVENT_WHEEL) {
        tiku_scroll_to(&c->bar, c->bar.value +
                       ev->dy * tiku_scroll_line(&c->bar));
        taken = 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 && c->doc) {
        if (ev->x >= l.bar.x && ev->x < l.bar.x + l.bar.w &&
            ev->y >= l.bar.y && ev->y < l.bar.y + l.bar.h) {
            /* The bar is drawn with a square arrow at each end and the
             * thumb in what is left, so that is the track a press is
             * measured against. */
            int arrow = l.bar.w;
            int track = l.bar.h - 2 * arrow;

            if (ev->y < l.bar.y + arrow) {
                tiku_scroll_to(&c->bar,
                               c->bar.value - tiku_scroll_line(&c->bar));
            } else if (ev->y >= l.bar.y + l.bar.h - arrow) {
                tiku_scroll_to(&c->bar,
                               c->bar.value + tiku_scroll_line(&c->bar));
            } else if (track > 0) {
                (void)tiku_scroll_press(&c->bar, track,
                                        ev->y - (l.bar.y + arrow));
            }
            taken = 1;
        } else if (ev->x >= l.page.x && ev->x < l.page.x + l.page.w &&
                   ev->y >= l.page.y && ev->y < l.page.y + l.page.h) {
            tiku_doc_hit_t h = tiku_doc_hit(c->doc, ev->x - l.page.x,
                                            ev->y - l.page.y + c->bar.value);

            if (h.kind == TIKU_DOC_HIT_LINK) {
                c->focus_link = h.link;
                (void)tiku_reader_help_window_go(c,
                    tiku_doc_link_href(c->doc, h.link));
            }
            taken = 1;
        }
    }
    if (ev->type == TIKU_EVENT_POINTER_MOVE && c->bar.tracking) {
        int arrow = l.bar.w;
        int track = l.bar.h - 2 * arrow;

        if (track > 0) {
            (void)tiku_scroll_drag(&c->bar, track,
                                   ev->y - (l.bar.y + arrow));
        }
        taken = 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_UP) tiku_scroll_release(&c->bar);
    if (c->dirty) *c->dirty = 1;
    return taken;
}
