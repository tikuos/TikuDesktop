/* A nested render must not overwrite the outer glyph's outline. */
#define tiku_path_new audit_path_new
#include <tiku_ttf.c>
#undef tiku_path_new
#include <assert.h>
tiku_path_t *tiku_path_new(void);
static tiku_ttf_t *face;
static int nesting;
tiku_path_t *audit_path_new(void)
{
    if (nesting == 1) {
        tiku_ttf_glyph_t other;
        nesting = 2;
        assert(tiku_ttf_render(face, 'W', 28, &other));
        tiku_ttf_free_glyph(&other);
    }
    return tiku_path_new();
}
int main(void)
{
    face = tiku_ttf_open("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    if (!face) face = tiku_ttf_open("/System/Library/Fonts/Supplemental/Arial.ttf");
    assert(face);
    tiku_ttf_glyph_t reference, nested;
    assert(tiku_ttf_render(face, 'A', 28, &reference));
    nesting = 1;
    assert(tiku_ttf_render(face, 'A', 28, &nested));
    int ok = reference.w == nested.w && reference.h == nested.h &&
        reference.ox == nested.ox && reference.oy == nested.oy &&
        !memcmp(reference.cover, nested.cover, (size_t)reference.w * reference.h);
    tiku_ttf_free_glyph(&reference); tiku_ttf_free_glyph(&nested);
    tiku_ttf_close(face);
    puts(ok ? "PASS D-124" : "FAIL D-124");
    return !ok;
}
