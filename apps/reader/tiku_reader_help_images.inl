/* Local images only. Decode after bounded dimensions and file-size checks. */

/** @brief Read image dimensions before allocating decoded pixels. */
static int reader_image_allowed(const char *path)
{
    struct stat st; unsigned char h[32]; unsigned w = 0, height = 0;
    if (stat(path, &st) || st.st_size < 24 || st.st_size > 4 * 1024 * 1024) return 0;
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    size_t n = fread(h, 1, sizeof h, f);
    if (n >= 24 && !memcmp(h, "\211PNG\r\n\032\n", 8)) {
        for (int i = 16; i < 20; i++) w = (w << 8) | h[i];
        for (int i = 20; i < 24; i++) height = (height << 8) | h[i];
    } else if (n >= 26 && h[0] == 'B' && h[1] == 'M') {
        for (int i = 21; i >= 18; i--) w = (w << 8) | h[i];
        for (int i = 25; i >= 22; i--) height = (height << 8) | h[i];
        if (height & 0x80000000u) height = 0u - height;
    } else if (n >= 2 && h[0] == 255 && h[1] == 216) {
        fseek(f, 2, SEEK_SET);
        for (int i = 0; i < 256; i++) {
            int mark = fgetc(f), tag = fgetc(f);
            if (mark != 255 || tag < 0 || tag == 218 || tag == 217) break;
            while (tag == 255) tag = fgetc(f);
            int a = fgetc(f), b = fgetc(f), len = a * 256 + b;
            if (a < 0 || b < 0 || len < 2) break;
            if (tag == 192 && len >= 7 && fread(h, 1, 5, f) == 5) {
                height = h[1] * 256u + h[2]; w = h[3] * 256u + h[4]; break;
            }
            if (fseek(f, len - 2, SEEK_CUR)) break;
        }
    }
    fclose(f);
    return w > 0 && height > 0 && w <= 2048 && height <= 2048 &&
           (unsigned long)w * height <= 1024 * 1024;
}

/** @brief Resolve local image paths inside the selected help bundle. */
static int reader_image_index(tiku_reader_help_window_t *c, const char *src)
{
    struct help_reader *r = c->reader; if (!r) return -1;
    for (int i = 0; i < r->nimages; i++) if (!strcmp(r->images[i].src, src)) return i;
    if (r->nimages >= 8 || strlen(src) >= sizeof r->images[0].src) return -1;
    int i = r->nimages++; strcpy(r->images[i].src, src);
    char path[TIKU_PATH_MAX];
    if (reader_path(c, src, path, sizeof path) && reader_image_allowed(path))
        (void)tiku_image_load(path, &r->images[i].image);
    return i;
}

/** @brief Keep aspect ratio and fit the paper; never enlarge small images. */
static int reader_image_size(void *ctx, const char *src, int *w, int *h)
{
    tiku_reader_help_window_t *c = ctx;
    int i = reader_image_index(c, src); if (i < 0) return 0;
    tiku_image_t *im = &c->reader->images[i].image; if (!im->px) return 0;
    *w = im->w; *h = im->h;
    int max = c->laid_for > 40 ? c->laid_for - 24 : 480;
    if (*w > max) { *h = (int)((long)*h * max / *w); *w = max; }
    if (*h < 1) *h = 1;
    return 1;
}

/** @brief Draw bounded image pixels through the toolkit and preserve clipping. */
static void reader_image_draw(void *ctx, const char *src, tiku_rect_t where)
{
    tiku_reader_help_window_t *c = ctx; struct help_reader *r = c->reader;
    int i = reader_image_index(c, src); if (i < 0 || !r->surface) return;
    tiku_image_t *im = &r->images[i].image; if (!im->px) return;
    tiku_rect_t clip = r->surface->clip;
    tiku_gfx_rec_miss(r->surface);
    for (int y = 0; y < where.h; y++) {
        if (where.y + y < clip.y || where.y + y >= clip.y + clip.h) continue;
        for (int x = 0; x < where.w; x++) {
            if (where.x + x < clip.x || where.x + x >= clip.x + clip.w) continue;
            uint32_t px = im->px[(long)y * im->h / where.h * im->w + (long)x * im->w / where.w];
            unsigned a = px >> 24;
            if (a) {
                uint32_t bg = TIKU_C_DOC;
                unsigned red = (((px >> 16) & 255) * a + ((bg >> 16) & 255) * (255-a)) / 255;
                unsigned green = (((px >> 8) & 255) * a + ((bg >> 8) & 255) * (255-a)) / 255;
                unsigned blue = ((px & 255) * a + (bg & 255) * (255-a)) / 255;
                tiku_pixel(r->surface, where.x+x, where.y+y, TIKU_RGB(red, green, blue));
            }
        }
    }
}
