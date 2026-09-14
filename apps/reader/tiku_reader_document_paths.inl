/* Local-document policy: explicit file navigation, confined automatic images. */

/** @brief Formats accepted by Open and by document links, case-insensitively. */
int tiku_reader_document_file_supported(const char *path)
{
    if (!path) return 0;
    const char *ext = strrchr(path, '.');
    return reader_markdown(path) || (ext &&
        (!strcasecmp(ext, ".html") || !strcasecmp(ext, ".htm")));
}

/** @brief Only regular local markup files can replace the current document. */
static int document_path(const char *name, char *out, size_t cap)
{
    struct stat st;
    if (!name || *name != '/' || !tiku_reader_document_file_supported(name))
        return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (*p < 32 || *p == 127) return 0;
    return reader_realpath(name, out, cap) &&
        tiku_reader_document_file_supported(out) &&
        !stat(out, &st) && S_ISREG(st.st_mode);
}

/** @brief Decode local URL escapes, refusing malformed escapes and controls. */
static int document_unescape(const char *src, size_t len, char *out, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)src[i];
        if (ch == '%') {
            if (i + 2 >= len || !isxdigit((unsigned char)src[i+1]) ||
                !isxdigit((unsigned char)src[i+2])) return 0;
            unsigned value = 0;
            for (int k = 0; k < 2; k++) {
                ch = (unsigned char)tolower((unsigned char)src[++i]);
                value = value * 16 + (ch <= '9' ? ch-'0' : ch-'a'+10);
            }
            ch = (unsigned char)value;
        }
        if (ch < 32 || ch == 127 || n + 1 >= cap) return 0;
        out[n++] = (char)ch;
    }
    out[n] = 0;
    return 1;
}

/** @brief Relative links use the current page's directory, never the help root. */
static int document_link(tiku_reader_help_window_t *c, const char *href,
                         char *path, size_t cap, char *anchor, size_t acap)
{
    char name[TIKU_PATH_MAX], candidate[TIKU_PATH_MAX];
    const char *hash = strchr(href, '#');
    size_t len = hash ? (size_t)(hash-href) : strlen(href);
    if (strchr(href, ':') || !strncmp(href, "//", 2) ||
        !document_unescape(href, len, name, sizeof name) ||
        strchr(name, ':') || strchr(name, '\\') ||
        !strncmp(name, "//", 2)) return 0;
    if (!document_unescape(hash ? hash+1 : "", hash ? strlen(hash+1) : 0,
                           anchor, acap)) return 0;
    int n = name[0] == '/' ? snprintf(candidate, sizeof candidate, "%s", name) :
        snprintf(candidate, sizeof candidate, "%s/%s", c->root, name);
    return n < (int)sizeof candidate && document_path(candidate, path, cap);
}

/** @brief Images may resolve only beneath the displayed document's directory. */
static int document_image_path(tiku_reader_help_window_t *c, const char *name,
                               char *out, size_t cap)
{
    char decoded[TIKU_PATH_MAX], candidate[TIKU_PATH_MAX];
    struct stat st;
    if (!c->root[0] || !name ||
        !document_unescape(name, strlen(name), decoded, sizeof decoded) ||
        decoded[0] == '/' || strchr(decoded, ':') || strchr(decoded, '\\'))
        return 0;
    int n = snprintf(candidate, sizeof candidate, "%s/%s", c->root, decoded);
    if (n >= (int)sizeof candidate || !reader_realpath(candidate, out, cap))
        return 0;
    size_t len = strlen(c->root);
    return !strncmp(c->root, out, len) &&
        (len == 1 || out[len] == '/') && !stat(out, &st) && S_ISREG(st.st_mode);
}
