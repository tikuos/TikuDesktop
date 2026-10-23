/* Explicit Adobe transforms determine JPEG component interpretation. */
#include <tiku_jpeg.c>
static int fails;
static void pixel(int components, int transform, unsigned a, unsigned b,
                  unsigned c, unsigned k, uint32_t expected, const char *id)
{
    jdec_t d = {0};
    unsigned char samples[] = {a, b, c, k};
    d.w = d.h = d.hmax = d.vmax = 1;
    d.ncomp = components; d.transform = transform;
    for (int i = 0; i < components; i++) {
        d.comp[i].h = d.comp[i].v = d.comp[i].bw = 1;
        d.comp[i].pix = &samples[i];
    }
    uint32_t *out = to_rgb(&d);
    if (!out || *out != expected) { printf("FAIL %s\n", id); fails++; }
    free(out);
}
int main(void)
{
    pixel(3, 0, 10, 100, 240, 0, 0xff0a64f0, "D-089");
    pixel(3, -1, 100, 128, 128, 0, 0xff646464, "YCbCr");
    pixel(4, 0, 255, 128, 0, 255, 0xffff8000, "D-098 CMYK");
    pixel(4, 2, 100, 128, 128, 128, 0xff4d4d4d, "D-098 YCCK");
    pixel(4, -1, 0, 127, 255, 0, 0xffff8000, "unmarked CMYK");
    return fails != 0;
}
