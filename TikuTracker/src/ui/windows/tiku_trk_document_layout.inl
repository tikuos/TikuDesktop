/* Compact browser chrome belongs to TikuReader, not bundled Help. */

/** @brief Compact icons and address share a row, wrapping on narrow windows. */
static help_layout_t document_layout(tiku_trk_help_window_t *c, tiku_rect_t box)
{
    help_layout_t l = {0};
    int bh = tiku_ui_button_h(), fh = tiku_font_plain()->height;
    int x = box.x + 6, y = box.y + 5;
    int bw = bh > 30 ? bh : 30;
    if (bw > (box.w-26)/5) bw = (box.w-26)/5;
    l.back = (tiku_rect_t){x, y, bw, bw};
    l.forward = (tiku_rect_t){x + bw + 2, y, bw, bw};
    l.done = (tiku_rect_t){x + 2 * (bw + 2), y, bw, bw};
    l.home = (tiku_rect_t){x + 3 * (bw + 2) + 4, y, bw, bw};
    l.find_toggle = (tiku_rect_t){box.x + box.w - bw - 6, y, bw, bw};
    int address_x = l.home.x + bw + 6;
    int address_right = l.find_toggle.x - 4;
    if (address_right - address_x < 210) {
        y += bw + 4;
        address_x = x; address_right = box.x + box.w - 6;
    }
    int row_h = bw > bh ? bw : bh;
    l.location = (tiku_rect_t){address_x, y + (row_h-bh)/2,
        address_right - address_x - bw - 2, bh};
    l.go = (tiku_rect_t){address_right - bw, y + (row_h-bw)/2, bw, bw};
    y += row_h + 5;
    l.status = (tiku_rect_t){x, box.y + box.h - fh - 8, box.w - 16, fh + 4};
    l.status.x += 22; l.status.w -= 22;
    int bottom = l.status.y - 5;
    if (c->reader && c->reader->find_visible) {
        int prev = tiku_ui_button_w(tiku_str("help.prev"), 48);
        int next = tiku_ui_button_w(tiku_str("help.next"), 48);
        int search = tiku_ui_button_w(tiku_str("reader.search_folder"), 100);
        bottom -= bh + 8;
        l.find_close = (tiku_rect_t){x, bottom, bh, bh};
        l.find = (tiku_rect_t){x + bh + 4, bottom,
            box.w - bh - prev - next - search - 32, bh};
        /* Folder search stays in Edit on narrow windows. */
        if (l.find.w < 100) { l.find.w += search + 4; search = 0; }
        if (l.find.w < 70) l.find.w += prev + next + 8;
        else {
            l.prev = (tiku_rect_t){l.find.x + l.find.w + 4, bottom, prev, bh};
            l.next = (tiku_rect_t){l.prev.x + prev + 4, bottom, next, bh};
        }
        if (search)
            l.search = (tiku_rect_t){l.next.x + next + 4, bottom, search, bh};
        bottom -= 4;
    }
    l.paper = (tiku_rect_t){box.x + 4, y, box.w - 8, bottom - y};
    if (c->reader && c->reader->side) {
        if (box.w >= 640) {
            l.side = (tiku_rect_t){l.paper.x, y, 188, l.paper.h};
            l.paper.x += 192; l.paper.w -= 192;
        } else {
            l.side = (tiku_rect_t){l.paper.x, y, l.paper.w, 96};
            l.paper.y += 100; l.paper.h -= 100;
        }
    }
    if (l.paper.w < 40) l.paper.w = 40;
    if (l.paper.h < 24) l.paper.h = 24;
    l.page = (tiku_rect_t){l.paper.x + 10, l.paper.y + 8,
        l.paper.w - HELP_BAR - 20, l.paper.h - 16};
    l.bar = (tiku_rect_t){l.paper.x + l.paper.w - HELP_BAR - 1,
        l.paper.y + 1, HELP_BAR, l.paper.h - 2};
    return l;
}
