/* tiku_sdr_navigation.c -- Plot navigation and retained spectrum history.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include <string.h>

#include "tiku_sdr.h"

void sdr_navigate(sdr_t *st, double start, double stop, int remember)
{
    if (st->play.file || !isfinite(start) || !isfinite(stop)) return;
    sdr_range_fit(&start, &stop);
    if (start == st->model.start && stop == st->model.stop) return;
    if (remember) {
        if (st->previous_count == SDR_VIEWS) {
            memmove(st->previous, st->previous + 1,
                    sizeof st->previous[0] * (SDR_VIEWS - 1));
            st->previous_count--;
        }
        st->previous[st->previous_count][0] = st->model.start;
        st->previous[st->previous_count++][1] = st->model.stop;
    }
    st->shown[0] = st->hover[0] = '\0';
    st->nav_drag = 0;
    sdr_model_range(&st->model, start, stop);
    double *kept = sdr_model_is_live(&st->model) ? st->live_view : st->wide_view;
    kept[0] = start;
    kept[1] = stop;
    sdr_rec_view(st);
    st->dirty = 1;
}

void sdr_zoom(sdr_t *st, double fraction, double factor)
{
    double span = st->model.stop - st->model.start;
    if (st->play.file || st->shown[0] || !isfinite(fraction) ||
        !isfinite(factor) || factor <= 0) return;
    if (fraction < 0) fraction = 0;
    if (fraction > 1) fraction = 1;
    double next = span * factor;
    double maximum = st->model.start >= 3800.0 ? 1000.0 : 500.0;
    if (next < SDR_SPAN_MIN) next = SDR_SPAN_MIN;
    if (next > maximum) next = maximum;
    double start = st->model.start + (span - next) * fraction;
    sdr_navigate(st, start, start + next, 1);
}

void sdr_view_back(sdr_t *st)
{
    if (st->play.file || st->shown[0] || st->previous_count <= 0) return;
    int i = --st->previous_count;
    sdr_navigate(st, st->previous[i][0], st->previous[i][1], 0);
    st->dirty = 1;
}

/** @brief Whether a pointer lies inside a positive-sized rectangle. */
static int nav_inside(tiku_rect_t r, const tiku_event_t *ev)
{
    return ev->x >= r.x && ev->x < r.x + r.w &&
           ev->y >= r.y && ev->y < r.y + r.h;
}

/** @brief Clamp a horizontal pointer coordinate to the plot edges. */
static int nav_column(tiku_rect_t r, int x)
{
    return x < r.x ? r.x : x > r.x + r.w ? r.x + r.w : x;
}

int sdr_navigation_event(sdr_t *st, const tiku_event_t *ev)
{
    tiku_rect_t plot, fall;
    sdr_model_t *m = &st->model;
    int editable = !st->play.file && !st->shown[0];
    sdr_layout(st, &plot, &fall);
    if (ev->type == TIKU_EVENT_DEACTIVATED ||
        ev->type == TIKU_EVENT_RESIZE ||
        (ev->type == TIKU_EVENT_KEY_DOWN && ev->key == TIKU_KEY_ESCAPE)) {
        int took = st->nav_drag != 0;
        st->nav_drag = 0;
        return took;
    }
    if (st->nav_drag && (ev->type == TIKU_EVENT_POINTER_MOVE ||
                         ev->type == TIKU_EVENT_POINTER_UP)) {
        st->nav_x = nav_column(plot, ev->x);
        if (ev->type == TIKU_EVENT_POINTER_UP) {
            int kind = st->nav_drag, delta = st->nav_x - st->nav_anchor;
            double span = st->nav_stop - st->nav_start;
            st->nav_drag = 0;
            if (editable && (delta >= 4 || delta <= -4)) {
                double start, stop;
                if (kind == 2) {
                    start = st->nav_start - delta * span / plot.w;
                    stop = start + span;
                } else {
                    start = st->nav_start +
                        (st->nav_anchor - plot.x) * span / plot.w;
                    stop = st->nav_start + (st->nav_x - plot.x) * span / plot.w;
                }
                sdr_navigate(st, start, stop, 1);
            }
        }
        st->dirty = 1;
        return 1;
    }
    int on_fall = nav_inside(fall, ev), on_plot = nav_inside(plot, ev);
    if (on_fall && st->occupancy.visible) return 1;
    if (!on_fall && !on_plot) return 0;
    if (ev->type == TIKU_EVENT_WHEEL && ev->dy) {
        if (on_fall && (ev->modifiers & TIKU_MOD_OPTION))
            sdr_history_scroll(m, ev->dy > 0 ? 20 : -20);
        else if (editable && (ev->modifiers & TIKU_MOD_SHIFT)) {
            double delta = (m->stop - m->start) * (ev->dy > 0 ? 0.1 : -0.1);
            sdr_navigate(st, m->start + delta, m->stop + delta, 1);
        } else if (editable)
            sdr_zoom(st, (double)(ev->x - plot.x) / plot.w,
                     ev->dy > 0 ? 1.25 : 0.8);
        st->dirty = 1;
        return 1;
    }
    if (ev->type != TIKU_EVENT_POINTER_DOWN) return 0;
    if (on_fall && ev->button == 1 && (ev->modifiers & TIKU_MOD_OPTION)) {
        int age = m->wf_offset + ev->y - fall.y;
        if (sdr_history_row(m, age) >= 0) m->wf_selected = age;
        st->dirty = 1;
        return 1;
    }
    if (editable && ((ev->button == 1 && (ev->modifiers & TIKU_MOD_SHIFT)) ||
                     ev->button == 2)) {
        st->nav_drag = ev->button == 2 ? 2 : 1;
        m->mark_held = -1;
        st->nav_anchor = st->nav_x = nav_column(plot, ev->x);
        st->nav_start = m->start;
        st->nav_stop = m->stop;
        st->dirty = 1;
        return 1;
    }
    return 0;
}
