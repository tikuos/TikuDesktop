/* Partial thumbnail alpha blends on both sides of the picture size limit. */
#include "tiku_iconpaint.h"
#include "tiku_dl.h"
#include <stdio.h>
int main(void)
{
    uint32_t pixel = 0x80ff0000;
    tiku_image_t img = {.w = 1, .h = 1, .px = &pixel};
    int fails = 0;
    for (int size = 8; size <= 520; size *= 8) {
        tiku_surface_t *s = tiku_surface_new(size, size, TIKU_RGB(0, 0, 255));
        tiku_thumb_paint(s, &img, 0, 0, size);
        if (tiku_peek(s, 0, 0) != TIKU_RGB(128, 0, 127)) {
            printf("FAIL D-102 size=%d\n", size); fails++;
        }
        tiku_surface_free(s);
    }
    return fails != 0;
}
