#include "issd_title_logo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Top-left of the logo box in the authentic 256-pixel view. */
enum { LOGO_X = 18, LOGO_Y = 26 };

static uint32_t *s_logo, *s_reference;
static int s_w, s_h;

/* The lettering flies in as a scaled sprite over a still brushstroke, so
 * it is found by colour: none of its colours occur in the brushstroke. */
static uint32_t *s_brush, *s_text, *s_old;
static int s_text_w, s_text_h;
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

bool issd_title_logo_load(const char *mods_dir) {
    free(s_logo); free(s_reference); free(s_brush); free(s_text); free(s_old);
    s_logo = s_reference = s_brush = s_text = s_old = NULL;
    s_w = s_h = s_text_w = s_text_h = s_text_colour_count = s_old_colour_count = 0;
    if (!mods_dir || !mods_dir[0]) return false;
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
    if (!changed_reset(x0, 0, right - x0, y0 + s_h)) return false;
    /* The old lettering's tiles still sit in the background beneath; keep
     * HD replacement off its whole area, not just the pixels that differ. */
    for (int y = by0; y <= by1; y++)
        memset(s_changed + (size_t)(y0 + y) * s_cw + bx0, 1, (size_t)(bx1 - bx0 + 1));
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

bool issd_title_logo_apply(uint32_t *frame, int width, int height, int margin_left,
                           int *box_x, int *box_y, int *box_w, int *box_h,
                           const uint8_t **changed) {
    if (!s_logo || !frame) return false;
    const int x0 = margin_left + LOGO_X, y0 = LOGO_Y;
    if (x0 < 0 || x0 + s_w > width || y0 + s_h > height) return false;
    /* Only the finished logo, at full brightness: anything less than a
     * near-exact match is the animation, a fade or another screen. */
    size_t same = 0, total = (size_t)s_w * s_h;
    for (int y = 0; y < s_h; y++) {
        const uint32_t *row = frame + (size_t)(y0 + y) * width + x0;
        const uint32_t *ref = s_reference + (size_t)y * s_w;
        for (int x = 0; x < s_w; x++)
            same += ((row[x] ^ ref[x]) & 0xFFFFFFu) == 0;
    }
    if (same * 100 < total * 97) {
        const int right = width;   /* widescreen shows the lettering entering beyond 256 */
        if (!replace_lettering(frame, width, x0, y0, right)) return false;
        if (box_x) *box_x = x0;
        if (box_y) *box_y = 0;
        if (box_w) *box_w = right - x0;
        if (box_h) *box_h = y0 + s_h;
        if (changed) *changed = s_changed;
        return true;
    }
    if (!changed_reset(x0, y0, s_w, s_h)) return false;
    for (int y = 0; y < s_h; y++) {
        const uint32_t *src = s_logo + (size_t)y * s_w;
        for (int x = 0; x < s_w; x++)
            if (src[x] >> 24) put(frame, width, x0 + x, y0 + y, src[x]);
    }
    if (s_old)                      /* the old lettering's tiles, as above */
        for (int i = 0; i < s_w * s_h; i++)
            if (s_old[i] >> 24) s_changed[i] = 1;
    if (box_x) *box_x = x0;
    if (box_y) *box_y = y0;
    if (box_w) *box_w = s_w;
    if (box_h) *box_h = s_h;
    if (changed) *changed = s_changed;
    return true;
}
