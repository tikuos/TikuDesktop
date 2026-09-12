/* Shipped flat-directory apps keep their paths and startup identities.
 * Categorisation changes presentation only, never trust or execution mode. */
static int installed_group(const char *name)
{
    static const char *const demos[] = {
        "hello.so", "clock.so", "counter.so", "sketch.so", "twowin.so",
        "sensors.so", "gallery.so", "demo.so", "sounds.so"
    };
    if (!strcmp(name, "Bench.so") || !strcmp(name, "TikuBench.so") ||
        !strcmp(name, "TikuConsole.so")) return TIKU_TRK_DB_GROUP_DEVICE_TOOLS;
    for (size_t i = 0; i < sizeof demos / sizeof demos[0]; i++)
        if (!strcmp(name, demos[i])) return TIKU_TRK_DB_GROUP_DEMOS;
    return TIKU_TRK_DB_GROUP_APPS;
}
