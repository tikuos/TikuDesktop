/* Corrected memory-map fixtures use the existing About parser unchanged. */
#include <assert.h>
#include "../../system/shared/ui/windows/tiku_trk_about_window.c"

static void row(about_state_t *st, const char *kind, const char *text)
{
    char line[128];
    snprintf(line, sizeof line, "%s", text);
    about_dev_estate(st, 0, kind, line);
}

int main(void)
{
    about_state_t st = {0};
    row(&st, "nvm", "kind\tFRAM");
    row(&st, "nvm", "total\t262144");
    row(&st, "nvm", "tier\t1024\t64");
    row(&st, "nvm", "store\t9316");
    assert(!strcmp(st.dev[0].lay[1].name, "FRAM"));
    assert(st.dev[0].lay[1].est[0].size == 1024);
    assert(st.dev[0].lay[1].est[0].used == 64);
    assert(st.dev[0].lay[1].est[1].size == 9316);
    assert(!st.dev[0].lay[1].est[1].live);
    row(&st, "nvm", "store\t9316\t4096");
    assert(st.dev[0].lay[1].est[2].live);
    assert(st.dev[0].lay[1].est[2].used == 4096);
    row(&st, "sram", "bank\t524288");
    row(&st, "sram", "static\t65536");
    row(&st, "sram", "stack\t36864\t8192");
    row(&st, "sram", "tier\t421888\t4096\t1");
    assert(st.dev[0].lay[0].est[0].size + st.dev[0].lay[0].est[1].size +
           st.dev[0].lay[0].est[2].size == st.dev[0].lay[0].total);
    assert(!strcmp(st.dev[0].lay[0].est[2].label, "tier"));
    row(&st, "sram", "tier\t65536\t128\t2");
    assert(!strcmp(st.dev[0].lay[0].est[3].label, "tier2"));
    puts("about_memory: map fixtures PASS");
    return 0;
}
