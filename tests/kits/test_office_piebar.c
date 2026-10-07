/* The secondary stacked bar stays inside its requested rectangle. */
#define ch_rect audit_rect
#include "../../kits/translation/tiku_office_chart_plot.c"
#undef ch_rect
static float low = 1e9, high = -1e9;
void audit_rect(ch_chart_t *k, const ch_box_t *b, const dml_fill_t *f,
                const dml_line_t *l)
{
    (void)k; (void)f; (void)l;
    low = fminf(low, b->y); high = fmaxf(high, b->y + b->h);
}
int main(void)
{
    ch_chart_t chart = {0}; ch_group_t group = {0}; ch_series_t series = {0};
    double values[] = {1, 1};
    series.n = 2; series.v = values; series.has_fill = 1;
    group.split_at = 2;
    pl_t plot = {.k = &chart, .g = &group};
    float at[] = {10, 100, 20, 60};
    second_bar(&plot, &series, 2, 2, at);
    int ok = fabsf(low - 100) < .01f && fabsf(high - 160) < .01f;
    puts(ok ? "PASS D-087" : "FAIL D-087"); return !ok;
}
