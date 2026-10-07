/* Merged cells continue on the next page with bounded background geometry. */
#define pg_next_column audit_next_column
#include "../../kits/translation/tiku_office_table.c"
#undef pg_next_column
#include <assert.h>
#include <stdio.h>
static int split_at = -1;
int audit_next_column(of_pager_t *pg)
{ split_at = pg->frags->n; pg->y = 0; pg->content = 0; return 0; }
int main(void)
{
    of_row_t rows[2] = {0}; of_table_t table = {.row = rows, .nrow = 2};
    of_cell_t cell = {.fill = 0xffff0000};
    tb_cell_t placed = {.c = &cell, .row = 0, .rows = 2, .span = 1,
                        .nfrag = 1};
    float xs[] = {0, 100}, heights[] = {80, 80};
    tb_t tb = {.t = &table, .xs = xs, .ncol = 1, .cell = &placed,
               .ncell = 1, .rh = heights};
    of_vec_t output; of_vec_init(&output, sizeof(of_frag_t));
    of_vec_init(&tb.frags, sizeof(of_frag_t));
    of_frag_t *source = of_vec_push(&tb.frags);
    assert(source);
    *source = (of_frag_t){.kind = OF_FRAG_RECT, .w = 20, .h = 160,
                         .color = 0xff00ff00};
    of_layout_t layout = {0};
    of_pager_t pg = {.lay = &layout, .frags = &output, .bottom = 100};
    assert(place_rows(&pg, &tb) == 0);
    int ok = split_at > 0 && output.n > split_at, contents = 0;
    for (int i = 0; i < output.n; i++) {
        of_frag_t *f = (of_frag_t *)output.p + i;
        if (f->kind == OF_FRAG_RECT && f->color == cell.fill)
            ok &= f->h <= 100;
        if (f->color == source->color) {
            ok &= f->clipped && f->y + f->clip_top == 0 &&
                  f->y + f->clip_bottom == 80;
            ok &= contents ? f->y == -80 : f->y == 0;
            contents++;
        }
    }
    ok &= contents == 2;
    of_vec_free(&output); of_vec_free(&tb.frags);
    puts(ok ? "PASS D-095" : "FAIL D-095"); return !ok;
}
