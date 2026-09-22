/* tiku_reader_navigation.c -- navigation.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_reader_private.h"

static int name_is_safe(const char *name);
static void forward_push(tiku_reader_help_window_t *c, const char *page,
                         tiku_reader_help_position_t at);
static int step(tiku_reader_help_window_t *c, const char *page,
                tiku_reader_help_position_t to, int back);
static int document_unescape(const char *src, size_t len, char *out, size_t cap);
static int document_link(tiku_reader_help_window_t *c, const char *href,
                         char *path, size_t cap, char *anchor, size_t acap);
static int document_image_path(tiku_reader_help_window_t *c, const char *name,
                               char *out, size_t cap);

/** @brief A name with no way out of the folder it is under. */
static int name_is_safe(const char *name)
{
    if (!name || !*name || *name == '/') return 0;
    if (strstr(name, "..")) return 0;
    for (const char *s = name; *s; s++) {
        if ((unsigned char)*s < 32) return 0;
    }
    return 1;
}

/** @brief Read a page of the help folder.  @return 1 when it was there. */
int tiku_reader_page_read(tiku_reader_help_window_t *c, const char *name)
{
    char path[TIKU_PATH_MAX];

    if (name && name[0] == '@' && c->reader) {
        for (int i = 0; i < HELP_REPORTS; i++) {
            if (strcmp(name, c->reader->reports[i].name) ||
                !c->reader->reports[i].html) continue;
            if (!tiku_reader_replace(c, name, c->reader->reports[i].html)) return 0;
            tiku_reader_loaded(c); c->oversize = 0; c->focus_link = TIKU_DOC_NONE;
            snprintf(c->page, sizeof c->page, "%s", name);
            tiku_reader_settle(c); tiku_scroll_to(&c->bar, 0); return 1;
        }
        return 0;
    }

    if (c->document_mode) {
        if (!tiku_reader_document_path(name, path, sizeof path)) return 0;
    } else {
        if (!c->root[0] || !name_is_safe(name)) return 0;
        if (!tiku_reader_path(c, name, path, sizeof path)) return 0;
    }
    const char *why;
    char *text = tiku_reader_source(path, &why);
    if (!text) { tiku_reader_say(c, why); return 0; }
    int ok = (*text || c->document_mode) && tiku_reader_replace(c, name, text);
    free(text);
    if (!ok) return 0;
    if (c->document_mode) {
        snprintf(c->root, sizeof c->root, "%s", path);
        char *slash = strrchr(c->root, '/');
        if (slash == c->root) slash[1] = 0;
        else if (slash) *slash = 0;
    }
    c->oversize = 0;
    tiku_reader_loaded(c);
    c->focus_link = TIKU_DOC_NONE;
    snprintf(c->page, sizeof c->page, "%s", c->document_mode ? path : name);
    if (c->document_mode) tiku_reader_document_location_sync(c);
    tiku_reader_settle(c);
    tiku_scroll_to(&c->bar, 0);
    return 1;
}

/**
 * @brief Put @p page and @p at on the way back.
 *
 * Called only once a read has succeeded: a history committed before the
 * page it names is there drops an entry whenever the read fails, and
 * the oldest one whenever the stack is full.
 */
void tiku_reader_back_push(tiku_reader_help_window_t *c, const char *page,
                      tiku_reader_help_position_t at)
{
    int slot = c->nback;

    if (!page || !page[0]) return;
    if (slot == TIKU_READER_HELP_HISTORY) {
        memmove(c->back, c->back + 1, sizeof c->back[0] * (size_t)(slot - 1));
        slot--;
    }
    snprintf(c->back[slot].page, sizeof c->back[slot].page, "%s", page);
    c->back[slot].at = at;
    c->nback = slot + 1;
}

/** @brief Put @p page and @p at on the way forward. */
static void forward_push(tiku_reader_help_window_t *c, const char *page,
                         tiku_reader_help_position_t at)
{
    int slot = c->nforward;

    if (!page || !page[0]) return;
    if (slot == TIKU_READER_HELP_HISTORY) {
        memmove(c->forward, c->forward + 1,
                sizeof c->forward[0] * (size_t)(slot - 1));
        slot--;
    }
    snprintf(c->forward[slot].page, sizeof c->forward[slot].page, "%s", page);
    c->forward[slot].at = at;
    c->nforward = slot + 1;
}

/** @brief What the window tells the reader, in its own line. */
void tiku_reader_say(tiku_reader_help_window_t *c, const char *what)
{
    snprintf(c->status, sizeof c->status, "%s", what ? what : "");
}

tiku_reader_help_where_t tiku_reader_help_window_go(tiku_reader_help_window_t *c,
                                              const char *href)
{
    if (!href || !*href) return TIKU_READER_HELP_NOWHERE;
    if (href[0] == '#') {
        char decoded[128];
        const char *anchor = href + 1;
        if (c->document_mode) {
            if (!document_unescape(anchor, strlen(anchor), decoded,
                                   sizeof decoded))
                return TIKU_READER_HELP_NOWHERE;
            anchor = decoded;
        }
        int y = c->doc ? tiku_doc_anchor_y(c->doc, anchor) : TIKU_DOC_NONE;

        if (y == TIKU_DOC_NONE) {
            tiku_reader_say(c, tiku_str("help.no_such_place"));
            return TIKU_READER_HELP_NOWHERE;
        }
        tiku_reader_back_push(c, c->page, tiku_reader_position(c));
        c->nforward = 0;
        tiku_scroll_to(&c->bar, y);
        tiku_reader_say(c, "");
        return TIKU_READER_HELP_ANCHOR;
    }
    if (c->document_mode) {
        char path[TIKU_PATH_MAX], anchor[128];
        if (!document_link(c, href, path, sizeof path, anchor, sizeof anchor)) {
            tiku_reader_say(c, "Cannot open link. Only existing local Markdown and HTML "
                   "files are supported.");
            return TIKU_READER_HELP_NOWHERE;
        }
        if (!tiku_reader_document_window_file(c, path))
            return TIKU_READER_HELP_NOWHERE;
        snprintf(c->anchor, sizeof c->anchor, "%s", anchor);
        return TIKU_READER_HELP_PAGE;
    }
    if (href[0] == '/' || !strncmp(href, "tiku:", 5)) {
        const char *path = href[0] == '/' ? href : href + 5;

        if (path[0] != '/') {
            /* The desk opens a place in the namespace, which starts at
             * its root; anything else it would drop without a word. */
            tiku_reader_say(c, tiku_str("help.not_a_place"));
            return TIKU_READER_HELP_NOWHERE;
        }
        snprintf(c->wanted, sizeof c->wanted, "%s", path);
        if (c->opener && !c->opener(c->opener_ctx, path)) {
            tiku_reader_say(c, tiku_str("help.not_a_place"));
            return TIKU_READER_HELP_NOWHERE;
        }
        tiku_reader_say(c, "");
        return TIKU_READER_HELP_NAMESPACE;
    }
    if (strstr(href, "://") || !strncmp(href, "mailto:", 7)) {
        /* There is no browser here, and a help page that pretended
         * otherwise would leave a reader waiting for a window. */
        tiku_reader_say(c, tiku_str("help.not_followed"));
        return TIKU_READER_HELP_NOWHERE;
    }
    {
        char name[TIKU_PATH_MAX], place[128], from[TIKU_PATH_MAX];
        const char *hash = strchr(href, '#');
        size_t n = hash ? (size_t)(hash - href) : strlen(href);
        tiku_reader_help_position_t was = tiku_reader_position(c);

        if (n >= sizeof name) return TIKU_READER_HELP_NOWHERE;
        memcpy(name, href, n);
        name[n] = 0;
        /*
         * Both halves are copied out before the read.  An href handed
         * in by a link points into the page's own words, and reading
         * the next page writes over them.
         */
        snprintf(place, sizeof place, "%s", hash && hash[1] ? hash + 1 : "");
        snprintf(from, sizeof from, "%s", c->page);
        if (!tiku_reader_page_read(c, name)) {
            tiku_reader_say(c, tiku_str("help.no_such_page"));
            return TIKU_READER_HELP_NOWHERE;
        }
        tiku_reader_back_push(c, from, was);
        c->nforward = 0;
        tiku_reader_say(c, "");
        snprintf(c->anchor, sizeof c->anchor, "%s", place);
        return TIKU_READER_HELP_PAGE;
    }
}

int tiku_reader_help_window_home(tiku_reader_help_window_t *c)
{
    if (c->document_mode) {
        if (c->choose_file) c->choose_file(c->choose_ctx);
        return c->choose_file != NULL;
    }
    char from[TIKU_PATH_MAX];
    tiku_reader_help_position_t was = tiku_reader_position(c);

    snprintf(from, sizeof from, "%s", c->page);
    if (!tiku_reader_page_read(c, "index.html")) {
        tiku_reader_say(c, tiku_str("help.no_such_page"));
        return 0;
    }
    tiku_reader_back_push(c, from, was);
    /* A move forward from here would be to a page the reader never came
     * back through. */
    c->nforward = 0;
    tiku_reader_say(c, "");
    return 1;
}

/** @brief Step to @p page at @p to, banking where it came from. */
static int step(tiku_reader_help_window_t *c, const char *page,
                tiku_reader_help_position_t to, int back)
{
    char from[TIKU_PATH_MAX];
    tiku_reader_help_position_t was = tiku_reader_position(c);

    snprintf(from, sizeof from, "%s", c->page);
    if (strcmp(page, c->page) && !tiku_reader_page_read(c, page)) {
        /* Nothing has been taken off either stack yet, so a page that
         * has gone missing since it was read costs the reader nothing
         * but the step. */
        return 0;
    }
    if (back) forward_push(c, from, was);
    else tiku_reader_back_push(c, from, was);
    tiku_scroll_to(&c->bar, c->doc && (c->doc->laid_width != to.width ||
        c->doc->laid_height != to.height) ?
        tiku_doc_place_y(c->doc, to.place) : to.pixel);
    tiku_reader_say(c, "");
    return 1;
}

int tiku_reader_help_window_back(tiku_reader_help_window_t *c)
{
    int at = c->nback - 1;

    if (at < 0) return 0;
    if (!step(c, c->back[at].page, c->back[at].at, 1)) return 0;
    c->nback = at;
    return 1;
}

int tiku_reader_help_window_forward(tiku_reader_help_window_t *c)
{
    int at = c->nforward - 1;

    if (at < 0) return 0;
    if (!step(c, c->forward[at].page, c->forward[at].at, 0)) return 0;
    c->nforward = at;
    return 1;
}

/* Local-document policy: explicit file navigation, confined automatic images. */

/** @brief Formats accepted by Open and by document links, case-insensitively. */
int tiku_reader_document_file_supported(const char *path)
{
    if (!path) return 0;
    const char *ext = strrchr(path, '.');
    return tiku_reader_markdown(path) || (ext &&
        (!strcasecmp(ext, ".html") || !strcasecmp(ext, ".htm")));
}

/** @brief Only regular local markup files can replace the current document. */
int tiku_reader_document_path(const char *name, char *out, size_t cap)
{
    struct stat st;
    if (!name || *name != '/' || !tiku_reader_document_file_supported(name))
        return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (*p < 32 || *p == 127) return 0;
    return tiku_reader_realpath(name, out, cap) &&
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
    return n < (int)sizeof candidate && tiku_reader_document_path(candidate, path, cap);
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
    if (n >= (int)sizeof candidate || !tiku_reader_realpath(candidate, out, cap))
        return 0;
    size_t len = strlen(c->root);
    return !strncmp(c->root, out, len) &&
        (len == 1 || out[len] == '/') && !stat(out, &st) && S_ISREG(st.st_mode);
}
/** @brief Canonicalize without assuming the OS PATH_MAX equals the kit's cap. */
int tiku_reader_realpath(const char *path, char *out, size_t cap)
{
    char *resolved = realpath(path, NULL); if (!resolved) return 0;
    int ok = strlen(resolved) < cap;
    if (ok) strcpy(out, resolved);
    free(resolved); return ok;
}

/** @brief Localized pages fall back individually; symlinks cannot escape root. */
int tiku_reader_path(tiku_reader_help_window_t *c, const char *name,
                       char *out, size_t cap)
{
    if (c->document_mode) return document_image_path(c, name, out, cap);
    char root[TIKU_PATH_MAX], candidate[TIKU_PATH_MAX], resolved[TIKU_PATH_MAX];
    char locale[16] = ""; struct stat st;
    const char *lang = getenv("TIKU_LANG"); if (!lang) lang = getenv("LANG");
    if (!name || !*name || *name == '/' || strstr(name, "..") ||
        strchr(name, ':') || strchr(name, '\\') || !tiku_reader_realpath(c->root, root, sizeof root)) return 0;
    if (lang) for (int i = 0; i < 15 && isalpha((unsigned char)lang[i]); i++)
        locale[i] = (char)tolower((unsigned char)lang[i]);
    for (int localized = locale[0] != 0; localized >= 0; localized--) {
        int n = localized ? snprintf(candidate, sizeof candidate, "%s/%s/%s", root, locale, name) :
            snprintf(candidate, sizeof candidate, "%s/%s", root, name);
        if (n >= (int)sizeof candidate || !tiku_reader_realpath(candidate, resolved, sizeof resolved)) continue;
        size_t len = strlen(root);
        if (strncmp(root, resolved, len) || resolved[len] != '/' ||
            stat(resolved, &st) || !S_ISREG(st.st_mode)) continue;
        if (strlen(resolved) >= cap) return 0;
        strcpy(out, resolved); return 1;
    }
    return 0;
}
