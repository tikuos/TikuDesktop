/* Pixel parity and reproducible software-compositor benchmark.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tiku_window.h"
#include "tiku_ui.h"
#include "tiku_present.h"

/** @brief Elapsed CPU-path timing, not X-server/display refresh timing. */
static int64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}

/** @brief Small controls and text resemble a populated utility window. */
static void content(tiku_window_t *w, tiku_surface_t *s,
                    tiku_rect_t r, void *ctx)
{
    int value = *(int *)ctx;
    (void)w;
    tiku_fill(s, r, 0xf0f0f0u+(unsigned)value);
    for (int y = 8; y < r.h-20; y += 25) {
        tiku_rect_t b = {r.x+10, r.y+y, 100, 20};
        tiku_ui_button(s, b, "Device", 0);
        tiku_text(s, tiku_font_plain(), r.x+120, r.y+y+15,
            "Memory: 64 KiB / 128 KiB", TIKU_C_TEXT);
    }
    /* Resetting a nested clip must not escape the compositor's damage. */
    tiku_clip_reset(s);
}

/** @brief Nested clipping, empty blits, native scale, and resize reset. */
static void clipping(int scale)
{
    tiku_surface_t *s = tiku_surface_new(80, 60, 0);
    tiku_surface_t *p = tiku_surface_new(10, 10, 123);
    assert(s && p);
    assert(!tiku_surface_rescale(s, scale, 0));
    assert(!tiku_surface_rescale(p, scale, 123));
    tiku_rect_t damage = {20, 20, 5, 5};
    tiku_clip_damage(s, &damage);
    tiku_blit(s, 60, 40, p); /* Disjoint: no negative-sized memcpy. */
    tiku_clip_reset(s);
    tiku_fill(s, (tiku_rect_t){0,0,80,60}, 1);
    for (int y = 0; y < 60*scale; y++)
        for (int x = 0; x < 80*scale; x++)
            assert(s->px[y*80*scale+x] ==
                (unsigned)(x/scale >= 20 && x/scale < 25 &&
                           y/scale >= 20 && y/scale < 25));
    damage = (tiku_rect_t){100,100,5,5};
    tiku_clip_damage(s, &damage);
    tiku_blit(s, 0, 0, p);
    assert(!tiku_surface_resize(s, 90, 70, 0));
    assert(!s->damage_active && s->clip.w == 90 && s->clip.h == 70);
    tiku_surface_free(p); tiku_surface_free(s);
}

/** @brief Tiles never drop changes, including overflow and HiDPI rounding. */
static void presentation(int scale, int rounded)
{
    tiku_surface_t *s = tiku_surface_new(201, 101, 0);
    assert(s && !tiku_surface_rescale(s, scale, 0));
    int w = s->w*scale-rounded, h = s->h*scale-rounded, stride = w+5;
    tiku_rgb_t *out = calloc((size_t)stride*h, sizeof *out);
    assert(out);
    for (int y = 0; y < s->h*scale; y++)
        for (int x = 0; x < s->w*scale; x++)
            s->px[y*s->w*scale+x] = (unsigned)(1+x+12345*y);
    tiku_rect_t r[64];
    assert(tiku_present_update(out,w,h,stride,s,1,r,64) == 1);
    assert(r[0].w == w && r[0].h == h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++)
            assert(out[y*stride+x] == s->px[y*s->w*scale+x]);
        assert(!out[y*stride+w]); /* Do not overwrite destination padding. */
    }
    assert(!tiku_present_update(out,w,h,stride,s,0,r,64));
    s->px[0]++;
    assert(tiku_present_update(out,w,h,stride,s,0,r,64) == 1);
    assert(r[0].w*r[0].h < w*h/8);
    assert(out[0] == s->px[0]);
    s->px[0]++;
    s->px[(size_t)(h-1)*s->w*scale+w-1]++;
    assert(tiku_present_update(out,w,h,stride,s,0,r,1) == 1);
    assert(r[0].x == 0 && r[0].y == 0 && r[0].w == w && r[0].h == h);
    assert(!tiku_present_update(out,w,h,stride,s,0,r,64));
    assert(tiku_present_update(out,w,h,w-1,s,0,r,64) == -1);
    free(out); tiku_surface_free(s);
}

/** @brief Legacy logical scaling still works when host and source differ. */
static void scaled_presentation(void)
{
    tiku_surface_t *s = tiku_surface_new(3, 2, 0);
    assert(s);
    for (int i = 0; i < 6; i++) s->px[i] = (unsigned)i;
    tiku_rgb_t out[77] = {0};
    tiku_rect_t r[4];
    assert(tiku_present_update(out,11,7,11,s,1,r,4) == 1);
    for (int y = 0; y < 7; y++)
        for (int x = 0; x < 11; x++)
            assert(out[y*11+x] == s->px[(y*2/7)*3+x*3/11]);
    assert(!tiku_present_update(out,11,7,11,s,0,r,4));
    tiku_surface_free(s);
}

/** @brief Sort a bounded set of measurements for median/p95 reporting. */
static int order_time(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y)-(x < y);
}

/** @brief Every cached drag frame must equal a from-scratch reference. */
static void dragging(int width, int height, int scale)
{
    tiku_surface_t *s[2];
    tiku_workspace_t *ws[2];
    tiku_window_t *wins[2][4];
    int value = 0;
    for (int k = 0; k < 2; k++) {
        s[k] = tiku_surface_new(width, height, 0);
        assert(s[k] && !tiku_surface_rescale(s[k], scale, 0));
        ws[k] = tiku_workspace_new(s[k]); assert(ws[k]);
        for (int i = 0; i < 4; i++) {
            tiku_rect_t f = {30+i*110, 35+i*45, 420, 290};
            wins[k][i] = tiku_workspace_open(ws[k], "Device Monitor",
                                              f, content, &value, NULL);
            assert(wins[k][i]);
        }
    }
    tiku_workspace_cache_mode(ws[1], TIKU_CACHE_REFRESH);
    tiku_workspace_draw(ws[1]);
    int64_t times[2][120];
    uint64_t area = 0;
    for (int i = 0; i < 120; i++) {
        tiku_rect_t before = wins[1][3]->frame;
        wins[0][3]->frame.x = wins[1][3]->frame.x = 250+(i%30)*8;
        wins[0][3]->frame.y = wins[1][3]->frame.y = 120+(i%15)*5;
        tiku_rect_t damage = tiku_rect_union(before, wins[1][3]->frame);
        /* What a window dirties is its frame and the shadow past it. */
        damage.x -= TIKU_WINDOW_SHADOW; damage.y -= TIKU_WINDOW_SHADOW;
        damage.w += 2*TIKU_WINDOW_SHADOW; damage.h += 2*TIKU_WINDOW_SHADOW;
        for (int k = 0; k < 2; k++) {
            int64_t start = now_us();
            if (k) {
                tiku_clip_damage(s[k], &damage);
                tiku_workspace_cache_mode(ws[k], TIKU_CACHE_REUSE);
            }
            tiku_workspace_draw(ws[k]);
            times[k][i] = now_us()-start;
        }
        area += (uint64_t)damage.w*damage.h;
        assert(!memcmp(s[0]->px, s[1]->px,
            (size_t)width*height*scale*scale*sizeof(tiku_rgb_t)));
        tiku_clip_damage(s[1], NULL);
    }
    uint64_t draws, hits;
    size_t bytes;
    tiku_workspace_cache_stats(ws[1], &draws, &hits, &bytes);
    assert(draws == 4 && hits >= 120 && bytes <= 64u*1024u*1024u);
    assert(area < (uint64_t)width*height*120/2);
    for (int k = 0; k < 2; k++)
        qsort(times[k], 120, sizeof times[k][0], order_time);
    printf("drag %dx%d @%dx: full median/p95 %lld/%lld us; "
        "cached %lld/%lld us; pixels %.1f%%; callbacks %llu; hits %llu\n",
        width,height,scale, (long long)times[0][60],
        (long long)times[0][113], (long long)times[1][60],
        (long long)times[1][113],
        100.0*area/((double)width*height*120),
        (unsigned long long)draws, (unsigned long long)hits);
    /* Content update, resize, focus, offscreen move and slot reuse. */
    for (int step = 0; step < 5; step++) {
        value++;
        for (int k = 0; k < 2; k++) {
            if (step == 1) wins[k][3]->frame.w += 30;
            if (step == 2) tiku_workspace_activate(ws[k], wins[k][0]);
            if (step == 3) wins[k][0]->frame.x = -120;
            if (step == 4) tiku_workspace_close(ws[k], wins[k][0]);
            if (k) tiku_workspace_cache_mode(ws[k], TIKU_CACHE_REFRESH);
            tiku_workspace_draw(ws[k]);
        }
        assert(!memcmp(s[0]->px, s[1]->px,
            (size_t)width*height*scale*scale*sizeof(tiku_rgb_t)));
    }
    tiku_workspace_cache_mode(ws[1], TIKU_CACHE_OFF);
    tiku_workspace_cache_stats(ws[1], NULL, NULL, &bytes); assert(!bytes);
    for (int k = 0; k < 2; k++) {
        tiku_workspace_free(ws[k]); tiku_surface_free(s[k]);
    }
}

int main(void)
{
    clipping(1); clipping(2);
    presentation(1,0); presentation(2,0); presentation(2,1);
    scaled_presentation();
    dragging(1000,680,1); dragging(1920,1080,1); dragging(1000,680,2);
    puts("PASS: damage, native presentation, cache lifecycle and drag parity");
    return 0;
}
