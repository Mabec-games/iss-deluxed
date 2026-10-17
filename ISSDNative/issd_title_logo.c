#include "issd_title_logo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Top-left of the logo box in the authentic 256-pixel view. */
enum { LOGO_X = 18, LOGO_Y = 26 };

static uint32_t *s_logo, *s_reference;
static int s_w, s_h;

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
    free(s_logo); free(s_reference);
    s_logo = s_reference = NULL;
    s_w = s_h = 0;
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
    printf("[TitleLogo] Loaded %dx%d replacement logo.\n", s_w, s_h);
    return true;
}

bool issd_title_logo_apply(uint32_t *frame, int width, int height, int margin_left,
                           int *box_x, int *box_y, int *box_w, int *box_h) {
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
    if (same * 100 < total * 97) return false;
    for (int y = 0; y < s_h; y++) {
        uint32_t *row = frame + (size_t)(y0 + y) * width + x0;
        const uint32_t *src = s_logo + (size_t)y * s_w;
        for (int x = 0; x < s_w; x++)
            if (src[x] >> 24) row[x] = 0xFF000000u | (src[x] & 0xFFFFFFu);
    }
    if (box_x) *box_x = x0;
    if (box_y) *box_y = y0;
    if (box_w) *box_w = s_w;
    if (box_h) *box_h = s_h;
    return true;
}
