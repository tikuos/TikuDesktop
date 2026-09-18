/* The Mac's window behind the seam: opened, shown by its first frame,
 * counted, retitled, asked, closed -- with a screen; without one it says
 * so and passes, since a shell over ssh has no window to test. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tiku_host.h"
#include "tiku_gfx.h"

int main(void)
{
    tiku_host_t *host = tiku_host_open(320, 240, "Host test");
    tiku_surface_t *s;
    tiku_event_t ev;
    uint64_t frames, bytes, rects;
    int scale, x = -2, y = -2, i;
    unsigned mods = 99u;

    if (host == NULL) {
        puts("SKIPPED: no window server session (no screen)");
        return 0;
    }
    scale = tiku_host_scale(host);
    assert(scale >= 1 && scale <= 4);
    tiku_host_present_stats(host, &frames, &bytes, &rects);
    assert(frames == 0 && bytes == 0 && rects == 0);

    /* The first frame is whole, and is what reveals the window. */
    s = tiku_surface_new(320, 240, TIKU_RGB(0x33, 0x66, 0x99));
    assert(s != NULL);
    if (scale > 1) {
        assert(tiku_surface_rescale(s, scale, TIKU_RGB(0x33, 0x66, 0x99)) == 0);
    }
    assert(tiku_host_present(host, s) == 0);
    tiku_host_present_stats(host, &frames, &bytes, &rects);
    assert(frames == 1 && rects == 1);
    assert(bytes == (uint64_t)320 * 240 * scale * scale * 4);

    /* An unchanged frame uploads nothing; a small change uploads a tile. */
    assert(tiku_host_present(host, s) == 0);
    tiku_host_present_stats(host, &frames, &bytes, &rects);
    assert(frames == 2 && rects == 1);
    tiku_fill(s, (tiku_rect_t){ 10, 10, 4, 4 }, TIKU_RGB(0xff, 0, 0));
    assert(tiku_host_present(host, s) == 0);
    tiku_host_present_stats(host, &frames, &bytes, &rects);
    assert(frames == 3 && rects == 2);
    assert(bytes < (uint64_t)320 * 240 * scale * scale * 4 * 2);

    /* A surface of another size is refused, not stretched. */
    {
        tiku_surface_t *other = tiku_surface_new(100, 100, 0);
        assert(tiku_host_present(host, other) == -1);
        tiku_surface_free(other);
    }

    tiku_host_set_title(host, "Host test, retitled");
    tiku_host_set_resize_cursor(host, 1);
    tiku_host_set_resize_cursor(host, 0);
    assert(tiku_host_pointer(host, &x, &y, &mods) == 1);
    assert(x >= -1 && y >= -1 && mods < 16u);

    /* Pumping for a moment delivers what the window was told on the way
     * up -- at most an activation, a resize -- and nothing untoward. */
    for (i = 0; i < 20; i++) {
        tiku_host_wait(host, 10000);
        while (tiku_host_poll(host, &ev)) {
            assert(ev.type == TIKU_EVENT_ACTIVATED ||
                   ev.type == TIKU_EVENT_DEACTIVATED ||
                   ev.type == TIKU_EVENT_RESIZE ||
                   ev.type == TIKU_EVENT_CLIPBOARD ||
                   ev.type == TIKU_EVENT_POINTER_MOVE ||
                   ev.type == TIKU_EVENT_KEY_UP ||
                   ev.type == TIKU_EVENT_KEY_DOWN);
            if (ev.type == TIKU_EVENT_RESIZE) {
                assert(ev.width == 320 && ev.height == 240);
            }
        }
    }
    tiku_host_cancel_composition(host);
    tiku_host_close(host);
    tiku_surface_free(s);
    puts("PASS: the Mac's window opens, shows, counts, answers and closes");
    return 0;
}
