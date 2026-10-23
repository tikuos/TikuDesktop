/* Broad CMap ranges remain candidates beneath many narrow ranges. */
#include <tiku_pdf_cmap.c>
int main(void)
{
    pdf_cmap_t m = {0};
    cm_run_t runs[24] = {0};
    m.run = runs; m.nrun = 24;
    runs[0] = (cm_run_t){.lo = 0x100000, .hi = 0x10ffff, .cid = 0, .len = 3};
    for (int i = 1; i < 24; i++) {
        runs[i] = (cm_run_t){.lo = 0x100000 + i, .hi = 0x100000 + i,
                             .cid = 100 + i, .len = 3, .seq = i};
    }
    int ok = cm_run_cid(&m, 0x100100, 3) == 256 &&
             cm_run_cid(&m, 0x100017, 3) == 123;
    puts(ok ? "PASS D-092" : "FAIL D-092"); return !ok;
}
