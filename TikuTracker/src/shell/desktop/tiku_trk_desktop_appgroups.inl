/* Manifest categories control presentation, never process isolation. */
static int installed_group(const char *category)
{
    if (!strcmp(category, "device-tools"))
        return TIKU_TRK_DB_GROUP_DEVICE_TOOLS;
    if (!strcmp(category, "programming"))
        return TIKU_TRK_DB_GROUP_PROGRAMMING;
    if (!strcmp(category, "demos")) return TIKU_TRK_DB_GROUP_DEMOS;
    return TIKU_TRK_DB_GROUP_APPS;
}
