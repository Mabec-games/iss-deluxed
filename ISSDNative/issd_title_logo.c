#include "issd_title_logo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Top-left of the logo box and the bottom credit line in the authentic
 * 256-pixel view (tools/build_title_logo.py, tools/build_title_credit.py). */
enum { LOGO_X = 18, LOGO_Y = 26, CREDIT_X = 8, CREDIT_Y = 213 };

static uint32_t *s_logo, *s_reference;
static int s_w, s_h;
/* The "LICENSED BY NINTENDO" line under the photos: the new banner, the
 * original line, and what lies behind it. */
static uint32_t *s_credit, *s_credit_reference, *s_credit_behind;
static int s_credit_w, s_credit_h;

/* The lettering flies in as a scaled sprite over a still brushstroke, so
 * it is found by colour: none of its colours occur in the brushstroke. */
static uint32_t *s_brush, *s_text, *s_old;
static int s_text_w, s_text_h;
/* Where the lettering ends up, in box coordinates (from old_text.bmp). */
static int s_land_x0, s_land_y0, s_land_x1, s_land_y1;
static uint32_t s_text_colours[64];
static int s_text_colour_count;
/* Every colour of the lettering, including ones it shares with the
 * brushstroke or background (its white faces are the background white). */
static uint32_t s_old_colours[64];
static int s_old_colour_count;

static bool in_list(const uint32_t *list, int count, uint32_t c) {
    c &= 0xFFFFFFu;
    for (int i = 0; i < count; i++)
        if (list[i] == c) return true;
    return false;
}
static bool is_text_colour(uint32_t c) { return in_list(s_text_colours, s_text_colour_count, c); }

/* Which pixels of the working box this frame changed. */
static uint8_t *s_changed;
static size_t s_changed_size;
static int s_cx0, s_cy0, s_cw;
static bool changed_reset(int x0, int y0, int w, int h) {
    const size_t need = (size_t)w * h;
    if (need > s_changed_size) {
        uint8_t *grown = (uint8_t *)realloc(s_changed, need);
        if (!grown) return false;
        s_changed = grown; s_changed_size = need;
    }
    memset(s_changed, 0, need);
    s_cx0 = x0; s_cy0 = y0; s_cw = w;
    return true;
}
/* Write frame pixel (fx, fy) and remember that it changed. */
static void put(uint32_t *frame, int width, int fx, int fy, uint32_t c) {
    uint32_t *p = frame + (size_t)fy * width + fx;
    c = 0xFF000000u | (c & 0xFFFFFFu);
    if (*p == c) return;
    *p = c;
    s_changed[(size_t)(fy - s_cy0) * s_cw + (fx - s_cx0)] = 1;
}

/* 32-bit BMP, top-down or bottom-up, uncompressed or bitfields. */
static uint32_t *load_bmp(const char *path, int *w, int *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t head[54];
    uint32_t *px = NULL;
    if (fread(head, 1, sizeof head, f) != sizeof head || head[0] != 'B' || head[1] != 'M')
        goto done;
    uint32_t offset = head[10] | head[11] << 8 | head[12] << 16 | (uint32_t)head[13] << 24;
    int32_t width = (int32_t)(head[18] | head[19] << 8 | head[20] << 16 | (uint32_t)head[21] << 24);
    int32_t height = (int32_t)(head[22] | head[23] << 8 | head[24] << 16 | (uint32_t)head[25] << 24);
    unsigned bits = head[28] | head[29] << 8, compression = head[30];
    int rows = height < 0 ? -height : height;
    if (bits != 32 || compression > 3 || width <= 0 || width > 256 || rows <= 0 || rows > 224)
        goto done;
    px = (uint32_t *)malloc((size_t)width * rows * sizeof *px);
    if (!px || fseek(f, (long)offset, SEEK_SET) != 0 ||
        fread(px, sizeof *px, (size_t)width * rows, f) != (size_t)width * rows) {
        free(px); px = NULL; goto done;
    }
    if (height > 0)                            /* bottom-up: flip into place */
        for (int y = 0; y < rows / 2; y++)
            for (int x = 0; x < width; x++) {
                uint32_t t = px[y * width + x];
                px[y * width + x] = px[(rows - 1 - y) * width + x];
                px[(rows - 1 - y) * width + x] = t;
            }
    *w = width; *h = rows;
done:
    fclose(f);
    return px;
}

static void load_credit(const char *mods_dir) {
    char path[1024];
    int cw = 0, ch = 0, rw = 0, rh = 0, bw = 0, bh = 0;
    free(s_credit); free(s_credit_reference); free(s_credit_behind);
    s_credit = s_credit_reference = s_credit_behind = NULL;
    snprintf(path, sizeof path, "%s/title_logo/credit.bmp", mods_dir);
    uint32_t *credit = load_bmp(path, &cw, &ch);
    snprintf(path, sizeof path, "%s/title_logo/credit_reference.bmp", mods_dir);
    uint32_t *reference = load_bmp(path, &rw, &rh);
    snprintf(path, sizeof path, "%s/title_logo/credit_behind.bmp", mods_dir);
    uint32_t *behind = load_bmp(path, &bw, &bh);
    if (credit && reference && behind && cw == rw && ch == rh && cw == bw && ch == bh &&
        CREDIT_X + cw <= 256 && CREDIT_Y + ch <= 224) {
        s_credit = credit; s_credit_reference = reference; s_credit_behind = behind;
        s_credit_w = cw; s_credit_h = ch;
        return;
    }
    free(credit); free(reference); free(behind);
}

bool issd_title_logo_load(const char *mods_dir) {
    free(s_logo); free(s_reference); free(s_brush); free(s_text); free(s_old);
    s_logo = s_reference = s_brush = s_text = s_old = NULL;
    s_w = s_h = s_text_w = s_text_h = s_text_colour_count = s_old_colour_count = 0;
    if (!mods_dir || !mods_dir[0]) return false;
    load_credit(mods_dir);
    char path[1024];
    int lw = 0, lh = 0, rw = 0, rh = 0;
    snprintf(path, sizeof path, "%s/title_logo/logo.bmp", mods_dir);
    uint32_t *logo = load_bmp(path, &lw, &lh);
    snprintf(path, sizeof path, "%s/title_logo/reference.bmp", mods_dir);
    uint32_t *reference = load_bmp(path, &rw, &rh);
    if (!logo || !reference || lw != rw || lh != rh ||
        LOGO_X + lw > 256 || LOGO_Y + lh > 224) {
        free(logo); free(reference);
        return false;
    }
    s_logo = logo; s_reference = reference; s_w = lw; s_h = lh;

    /* Optional fly-in layers; without them only the finished logo swaps. */
    int bw = 0, bh = 0, ow = 0, oh = 0, tw = 0, th = 0;
    snprintf(path, sizeof path, "%s/title_logo/brush.bmp", mods_dir);
    uint32_t *brush = load_bmp(path, &bw, &bh);
    snprintf(path, sizeof path, "%s/title_logo/old_text.bmp", mods_dir);
    uint32_t *old = load_bmp(path, &ow, &oh);
    snprintf(path, sizeof path, "%s/title_logo/text.bmp", mods_dir);
    uint32_t *text = load_bmp(path, &tw, &th);
    if (brush && old && text && bw == lw && bh == lh && ow == lw && oh == lh &&
        tw <= lw && th <= lh) {
        /* old_text.bmp holds the lettering's own pixels with their true
         * colours; those absent from the brushstroke identify it. */
        for (int i = 0; i < ow * oh; i++) {
            if (!(old[i] >> 24)) continue;
            const uint32_t c = old[i] & 0xFFFFFFu;
            if (!in_list(s_old_colours, s_old_colour_count, c) && s_old_colour_count < 64)
                s_old_colours[s_old_colour_count++] = c;
            bool in_brush = false;
            for (int j = 0; j < bw * bh && !in_brush; j++) in_brush = (brush[j] & 0xFFFFFFu) == c;
            if (!in_brush && !is_text_colour(c) && s_text_colour_count < 64)
                s_text_colours[s_text_colour_count++] = c;
        }
        s_land_x0 = ow; s_land_y0 = oh; s_land_x1 = s_land_y1 = -1;
        for (int y = 0; y < oh; y++)
            for (int x = 0; x < ow; x++)
                if ((old[y * ow + x] >> 24) && is_text_colour(old[y * ow + x])) {
                    if (x < s_land_x0) s_land_x0 = x;
                    if (x > s_land_x1) s_land_x1 = x;
                    if (y < s_land_y0) s_land_y0 = y;
                    if (y > s_land_y1) s_land_y1 = y;
                }
        s_brush = brush; s_text = text; s_old = old; s_text_w = tw; s_text_h = th;
        brush = text = old = NULL;
    }
    free(brush); free(old); free(text);
    printf("[TitleLogo] Loaded %dx%d replacement logo%s.\n", s_w, s_h,
           s_text ? " with fly-in lettering" : "");
    return true;
}

/* What lies behind the lettering at box coordinates (x, y): the stroke
 * inside the logo box, plain background outside it, where the lettering
 * passes while it is still large and far to the right. */
static uint32_t behind(int x, int y) {
    if (x >= 0 && x < s_w && y >= 0 && y < s_h) return s_brush[y * s_w + x] & 0xFFFFFFu;
    return s_brush[0] & 0xFFFFFFu;
}

/* While the lettering flies in or waits for the ball, swap it for the new
 * lettering at the same place and size. The search area runs from the top
 * of the screen to the logo box's bottom and across to the frame's right
 * edge, where the lettering enters. Returns true when done. */
static bool replace_lettering(uint32_t *frame, int width, int x0, int y0, int right) {
    if (!s_text || !s_text_colour_count) return false;
    const int ax0 = 0, ay0 = -y0, ax1 = right - x0, ay1 = s_h;   /* box coordinates */
    int brush = 0, text = 0, bx0 = ax1, by0 = ay1, bx1 = ax0 - 1, by1 = ay0 - 1;
    for (int y = ay0; y < ay1; y++) {
        const uint32_t *row = frame + (size_t)(y0 + y) * width + x0;
        for (int x = ax0; x < ax1; x++) {
            const uint32_t c = row[x] & 0xFFFFFFu;
            if (c == behind(x, y)) { if (x < s_w && y >= 0) brush++; }
            else if (is_text_colour(c)) {
                text++;
                if (x < bx0) bx0 = x;
                if (x > bx1) bx1 = x;
                if (y < by0) by0 = y;
                if (y > by1) by1 = y;
            }
        }
    }
    /* Mostly brushstroke plus lettering: the title screen between the
     * stroke being painted and the ball landing. */
    if (text < 16 || (size_t)(brush + text) * 100 < (size_t)s_w * s_h * 75) return false;
    /* Once landed the lettering never moves again, but the ball fading in
     * above it briefly blends into lettering colours. Its left and bottom
     * edges are unaffected, so when they sit where it lands, use the
     * landing box rather than letting those pixels stretch it. */
    if (s_land_x1 >= 0 && abs(bx0 - s_land_x0) <= 1 && abs(by1 - s_land_y1) <= 1) {
        bx0 = s_land_x0; by0 = s_land_y0; bx1 = s_land_x1; by1 = s_land_y1;
    }
    /* The old lettering's tiles still sit in the background beneath; keep
     * HD replacement off its whole area, not just the pixels that differ. */
    for (int y = by0; y <= by1; y++)
        memset(s_changed + (size_t)(y0 + y) * s_cw + x0 + bx0, 1, (size_t)(bx1 - bx0 + 1));
    for (int y = by0; y <= by1; y++) {
        uint32_t *row = frame + (size_t)(y0 + y) * width + x0;
        for (int x = bx0; x <= bx1; x++)
            if ((row[x] & 0xFFFFFFu) != behind(x, y) &&
                in_list(s_old_colours, s_old_colour_count, row[x]))
                put(frame, width, x0 + x, y0 + y, behind(x, y));
    }
    /* The game scales the lettering uniformly and it can be cut off at the
     * right edge as it enters, so its height gives the scale. */
    int th = by1 - by0 + 1;
    if (th > s_text_h) th = s_text_h;
    int tw = (s_text_w * th + s_text_h / 2) / s_text_h;
    for (int y = 0; y < th; y++) {
        const int fy = by0 + y;
        if (fy >= ay1) break;
        const uint32_t *src = s_text + (size_t)(y * s_text_h / th) * s_text_w;
        uint32_t *row = frame + (size_t)(y0 + fy) * width + x0;
        for (int x = 0; x < tw; x++) {
            const int fx = bx0 + x;
            if (fx >= ax1) break;
            const uint32_t c = src[x * s_text_w / tw];
            /* Only over the brushstroke or background: the ball stays in front. */
            if ((c >> 24) && (row[fx] & 0xFFFFFFu) == behind(fx, fy))
                put(frame, width, x0 + fx, y0 + fy, c);
        }
    }
    return true;
}

/* Frames that do not match a reference near-exactly are the animation, a
 * fade or another screen, and are left alone. */
static bool matches(const uint32_t *frame, int width, int x0, int y0,
                    const uint32_t *ref, int w, int h) {
    size_t same = 0, total = (size_t)w * h;
    for (int y = 0; y < h; y++) {
        const uint32_t *row = frame + (size_t)(y0 + y) * width + x0;
        for (int x = 0; x < w; x++) same += ((row[x] ^ ref[(size_t)y * w + x]) & 0xFFFFFFu) == 0;
    }
    return same * 100 >= total * 97;
}

static bool apply_logo(uint32_t *frame, int width, int height, int margin_left) {
    if (!s_logo) return false;
    const int x0 = margin_left + LOGO_X, y0 = LOGO_Y;
    if (x0 < 0 || x0 + s_w > width || y0 + s_h > height) return false;
    if (!matches(frame, width, x0, y0, s_reference, s_w, s_h))
        return replace_lettering(frame, width, x0, y0, width);  /* fly-in reaches the margin */
    for (int y = 0; y < s_h; y++) {
        const uint32_t *src = s_logo + (size_t)y * s_w;
        for (int x = 0; x < s_w; x++)
            if (src[x] >> 24) put(frame, width, x0 + x, y0 + y, src[x]);
    }
    if (s_old)                      /* the old lettering's tiles, as above */
        for (int i = 0; i < s_w * s_h; i++)
            if (s_old[i] >> 24) s_changed[(size_t)(y0 + i / s_w) * s_cw + x0 + i % s_w] = 1;
    return true;
}

static uint32_t scale_colour(uint32_t c, const int k[3]) {
    int r = (int)(c >> 16 & 255) * k[0] / 256, g = (int)(c >> 8 & 255) * k[1] / 256,
        b = (int)(c & 255) * k[2] / 256;
    return (uint32_t)(r > 255 ? 255 : r) << 16 | (uint32_t)(g > 255 ? 255 : g) << 8 |
           (uint32_t)(b > 255 ? 255 : b);
}

/* The line fades in with the title, so it is recognised by its own letter
 * pixels at the brightness of the plain background beside it, and the
 * banner is drawn at that same brightness. */
static bool apply_credit(uint32_t *frame, int width, int height, int margin_left) {
    if (!s_credit) return false;
    const int x0 = margin_left + CREDIT_X, y0 = CREDIT_Y;
    if (x0 < 0 || x0 + s_credit_w > width || y0 + s_credit_h > height) return false;
    const uint32_t ref_bg = s_credit_reference[0] & 0xFFFFFFu;    /* plain background */
    const uint32_t cur_bg = frame[(size_t)y0 * width + x0] & 0xFFFFFFu;
    int k[3];
    for (int i = 0; i < 3; i++) {
        int ref = (int)(ref_bg >> (16 - 8 * i) & 255), cur = (int)(cur_bg >> (16 - 8 * i) & 255);
        if (ref < 32 || cur > ref + 8) return false;
        k[i] = cur * 256 / ref;
    }
    if (k[0] < 32) return false;                                   /* almost black */
    size_t letters = 0, present = 0;
    for (int y = 0; y < s_credit_h; y++)
        for (int x = 0; x < s_credit_w; x++) {
            const size_t i = (size_t)y * s_credit_w + x;
            if (((s_credit_reference[i] ^ s_credit_behind[i]) & 0xFFFFFFu) == 0) continue;
            letters++;
            uint32_t want = scale_colour(s_credit_reference[i], k);
            uint32_t got = frame[(size_t)(y0 + y) * width + x0 + x] & 0xFFFFFFu;
            int d = abs((int)(want >> 16 & 255) - (int)(got >> 16 & 255)) +
                    abs((int)(want >> 8 & 255) - (int)(got >> 8 & 255)) +
                    abs((int)(want & 255) - (int)(got & 255));
            present += d <= 24;
        }
    if (!letters || present * 100 < letters * 90) return false;
    for (int y = 0; y < s_credit_h; y++)
        for (int x = 0; x < s_credit_w; x++) {
            const size_t i = (size_t)y * s_credit_w + x;
            const bool old = ((s_credit_reference[i] ^ s_credit_behind[i]) & 0xFFFFFFu) != 0;
            if (s_credit[i] >> 24)
                put(frame, width, x0 + x, y0 + y, scale_colour(s_credit[i], k));
            else if (old)
                put(frame, width, x0 + x, y0 + y, scale_colour(s_credit_behind[i], k));
        }
    return true;
}

bool issd_title_logo_apply(uint32_t *frame, int width, int height, int margin_left,
                           int *box_x, int *box_y, int *box_w, int *box_h,
                           const uint8_t **changed) {
    if (!frame || (!s_logo && !s_credit)) return false;
    if (!changed_reset(0, 0, width, height)) return false;
    bool drawn = apply_logo(frame, width, height, margin_left);
    drawn |= apply_credit(frame, width, height, margin_left);
    if (!drawn) return false;
    if (box_x) *box_x = 0;
    if (box_y) *box_y = 0;
    if (box_w) *box_w = width;
    if (box_h) *box_h = height;
    if (changed) *changed = s_changed;
    return true;
}
