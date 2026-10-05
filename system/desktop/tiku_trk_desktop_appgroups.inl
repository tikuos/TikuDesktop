/* Manifest categories control presentation, never process isolation. */
static int installed_group(const char *category)
{
    if (!strcmp(category, "device-tools"))
        return TIKU_TRK_DB_GROUP_DEVICE_TOOLS;
    if (!strcmp(category, "programming") || !strcmp(category, "development"))
        return TIKU_TRK_DB_GROUP_PROGRAMMING;
    if (!strcmp(category, "documents")) return TIKU_TRK_DB_GROUP_DOCUMENTS;
    if (!strcmp(category, "graphics")) return TIKU_TRK_DB_GROUP_GRAPHICS;
    if (!strcmp(category, "demos")) return TIKU_TRK_DB_GROUP_DEMOS;
    return TIKU_TRK_DB_GROUP_APPS;
}
