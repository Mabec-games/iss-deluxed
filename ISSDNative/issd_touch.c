#include "issd_touch.h"
#include <string.h>

#define MAX_POINTS 10

static struct {
    bool          enabled;
    bool          pad_visible;
    int           vw, vh;
    IssdTouchRect r[ISSD_TOUCH_COUNT];
    int           px[MAX_POINTS], py[MAX_POINTS], pn;
    bool          thumb_held;
    int64_t       thumb_device, thumb_finger;
    int           thumb_x, thumb_y;
    uint16_t      mask;
    bool          menu_down, menu_fired, hide_down;
    bool          screen_touch_down, screen_tap_fired;
    int           screen_tap_x, screen_tap_y;
    int           layout_x[ISSD_TOUCH_COUNT], layout_y[ISSD_TOUCH_COUNT];
    int           layout_size[ISSD_TOUCH_COUNT];
} T = {
    .enabled = true, .pad_visible = true,
    .layout_x = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    .layout_y = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    .layout_size = {100,100,100,100,100,100,100,100,100,100,100}
};

static bool hit(const IssdTouchRect *r, int x, int y) {
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* Fingers slide off small targets constantly, so face buttons take a generous
 * margin. The d-pad does not: it is already large and a margin there would
 * overlap its neighbours. */
static bool hit_padded(const IssdTouchRect *r, int x, int y, int pad) {
    return (int64_t)x >= (int64_t)r->x - pad &&
           (int64_t)x < (int64_t)r->x + r->w + pad &&
           (int64_t)y >= (int64_t)r->y - pad &&
           (int64_t)y < (int64_t)r->y + r->h + pad;
}

static void place(IssdTouchControl c, int x, int y, int w, int h,
                  bool round, const char *label, const char *sublabel,
                  uint8_t cr, uint8_t cg, uint8_t cb) {
    T.r[c].x = x; T.r[c].y = y; T.r[c].w = w; T.r[c].h = h;
    T.r[c].round = round; T.r[c].label = label; T.r[c].sublabel = sublabel;
    T.r[c].color_r = cr; T.r[c].color_g = cg; T.r[c].color_b = cb;
    T.r[c].pressed = false;
}

static void compute_layout(void) {
    const int width = T.vw, height = T.vh;
    /* Everything scales off the short edge so the layout holds at any aspect
     * and any density, from a phone to a 1080p handheld. */
    const int s     = height < width ? height : width;
    const int unit  = s / 8;              /* face button diameter */
    const int gap   = s / 40;
    const int edge  = s / 16;
    const int dpad  = (int)(unit * 2.4f); /* whole d-pad, not one direction */
    const int small = (int)(unit * 0.62f);

    /* D-pad: bottom left. Face cluster: bottom right, diamond. */
    place(ISSD_TOUCH_DPAD, edge, height - edge - dpad, dpad, dpad, false, NULL, NULL, 50, 55, 65);

    const int fx = width - edge - unit * 3 - gap * 2;
    const int fy = height - edge - unit * 3 - gap * 2;
    const int cx = fx + unit + gap;
    const int cy = fy + unit + gap;
    place(ISSD_TOUCH_Y, fx,              cy,              unit, unit, true, "Y", NULL, 46, 204, 113);
    place(ISSD_TOUCH_X, cx,              fy,              unit, unit, true, "X", NULL, 52, 152, 219);
    place(ISSD_TOUCH_A, cx + unit + gap, cy,              unit, unit, true, "A", NULL, 231, 76, 60);
    place(ISSD_TOUCH_B, cx,              cy + unit + gap, unit, unit, true, "B", NULL, 241, 196, 15);

    /* Shoulders along the top, clear of the face cluster. */
    place(ISSD_TOUCH_L, edge,                    edge, unit * 2, small, false, "L", NULL, 150, 155, 165);
    place(ISSD_TOUCH_R, width - edge - unit * 2, edge, unit * 2, small, false, "R", NULL, 150, 155, 165);

    /* Start/Select centred low, where a thumb will not rest by accident. */
    const int sw = (int)(unit * 1.5f);
    place(ISSD_TOUCH_SELECT, width / 2 - sw - gap / 2, height - edge - small, sw, small, false, "SELECT", NULL, 120, 125, 135);
    place(ISSD_TOUCH_START,  width / 2 + gap / 2,      height - edge - small, sw, small, false, "START",  NULL, 120, 125, 135);

    /* Hide toggle sits top-centre-left; the menu button top-centre-right.
     * The menu button is deliberately separated from the hide toggle so that
     * hiding the pad cannot be confused with losing access to the menu. */
    place(ISSD_TOUCH_HIDE, width / 2 - small * 2 - gap, edge, small * 2, small, false, "HIDE", NULL, 70, 130, 180);
    place(ISSD_TOUCH_MENU, width / 2 + gap,             edge, small * 2, small, false, "MENU", NULL, 230, 126, 34);

    for (int i = 0; i < ISSD_TOUCH_COUNT; ++i) {
        IssdTouchRect *r = &T.r[i];
        /* Use the original centres for automatic axes. Width/height rounding
         * preserves every default rectangle exactly on ordinary viewports. */
        int64_t cx = T.layout_x[i] < 0 ? r->x + r->w / 2 :
                     (int64_t)width * T.layout_x[i] / 1000;
        int64_t cy = T.layout_y[i] < 0 ? r->y + r->h / 2 :
                     (int64_t)height * T.layout_y[i] / 1000;
        int64_t w = (int64_t)r->w * T.layout_size[i] / 100;
        int64_t h = (int64_t)r->h * T.layout_size[i] / 100;
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        if (w > width) w = width;
        if (h > height) h = height;
        int64_t x = cx - w / 2, y = cy - h / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > width - w) x = width - w;
        if (y > height - h) y = height - h;
        r->x = (int)x; r->y = (int)y; r->w = (int)w; r->h = (int)h;
    }
}

void issd_touch_reset_points(void) {
    T.pn = 0; T.mask = 0;
    T.thumb_held = false;
    memset(T.px, 0, sizeof(T.px)); memset(T.py, 0, sizeof(T.py));
    T.menu_down = T.menu_fired = T.hide_down = false;
    T.screen_touch_down = T.screen_tap_fired = false;
    T.screen_tap_x = T.screen_tap_y = 0;
    for (int i = 0; i < ISSD_TOUCH_COUNT; ++i) T.r[i].pressed = false;
}

void issd_touch_set_layout(const int *xs, const int *ys, const int *sizes) {
    bool changed = false;
    for (int i = 0; i < ISSD_TOUCH_COUNT; ++i) {
        int x = xs && xs[i] >= 0 && xs[i] <= 1000 ? xs[i] : -1;
        int y = ys && ys[i] >= 0 && ys[i] <= 1000 ? ys[i] : -1;
        int size = sizes && sizes[i] >= 50 && sizes[i] <= 200 ? sizes[i] : 100;
        if (x != T.layout_x[i] || y != T.layout_y[i] || size != T.layout_size[i]) {
            T.layout_x[i] = x; T.layout_y[i] = y; T.layout_size[i] = size;
            changed = true;
        }
    }
    if (!changed) return;
    issd_touch_reset_points();
    if (T.vw > 0 && T.vh > 0) compute_layout();
}

void issd_touch_set_viewport(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == T.vw && height == T.vh) return;
    T.vw = width; T.vh = height;
    issd_touch_reset_points();
    compute_layout();
}

bool issd_touch_center(int control, int *x, int *y) {
    if (control < 0 || control >= ISSD_TOUCH_COUNT || T.vw <= 0 || T.vh <= 0)
        return false;
    const IssdTouchRect *r = &T.r[control];
    /* Twice the geometric centre retains half-pixels in odd-sized controls.
     * Round to the nearest normalized coordinate without floating point. */
    int nx = (int)(((2*(int64_t)r->x+r->w)*1000+T.vw) / (2*(int64_t)T.vw));
    int ny = (int)(((2*(int64_t)r->y+r->h)*1000+T.vh) / (2*(int64_t)T.vh));
    if (x) *x = nx;
    if (y) *y = ny;
    return true;
}

void issd_touch_set_enabled(bool enabled) {
    if (T.enabled != enabled) issd_touch_reset_points();
    T.enabled = enabled;
}
bool issd_touch_enabled(void) { return T.enabled; }
bool issd_touch_pad_visible(void) { return T.pad_visible; }
void issd_touch_set_pad_visible(bool visible) {
    if (T.pad_visible != visible) issd_touch_reset_points();
    T.pad_visible = visible;
}

void issd_touch_set_points(const int *xs, const int *ys, int count) {
    IssdTouchPoint points[MAX_POINTS];
    if (!xs || !ys || count < 0) count = 0;
    if (count > MAX_POINTS) count = MAX_POINTS;
    for (int i = 0; i < count; ++i)
        points[i] = (IssdTouchPoint){0, i, xs[i], ys[i]};
    issd_touch_set_fingers(points, count);
}

bool issd_touch_thumb(int *x, int *y) {
    if (!T.enabled || !T.pad_visible || !T.thumb_held) return false;
    if (x) *x = T.thumb_x;
    if (y) *y = T.thumb_y;
    return true;
}

static uint16_t thumb_direction(const IssdTouchRect *d, int x, int y) {
    /* Normalize both axes so resized/non-square pads retain 45-degree sectors.
     * Eight equal angular sectors; the centre radius is 8% of the diameter.
     * 414/1000 approximates tan(22.5 degrees) without libm or squared overflow. */
    int64_t dx = ((int64_t)x*2 - (int64_t)d->x*2 - d->w)*1000/d->w;
    int64_t dy = ((int64_t)y*2 - (int64_t)d->y*2 - d->h)*1000/d->h;
    int64_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax <= 160 && ay <= 160 && ax*ax+ay*ay <= 160*160) return 0;
    uint16_t mask = 0;
    if (ax*1000 >= ay*414) mask |= dx < 0 ? ISSD_PAD_LEFT : ISSD_PAD_RIGHT;
    if (ay*1000 >= ax*414) mask |= dy < 0 ? ISSD_PAD_UP : ISSD_PAD_DOWN;
    return mask;
}

void issd_touch_set_fingers(const IssdTouchPoint *points, int count) {
    if (!points) count = 0;
    if (count < 0) count = 0;
    if (count > MAX_POINTS) count = MAX_POINTS;
    T.pn = count;
    for (int i = 0; i < count; i++) { T.px[i] = points[i].x; T.py[i] = points[i].y; }

    if (T.thumb_held) {
        bool present = false;
        for (int i = 0; i < count; ++i)
            if (points[i].device == T.thumb_device && points[i].finger == T.thumb_finger)
                present = true;
        if (!present || !T.pad_visible || !T.enabled) T.thumb_held = false;
    }

    for (int i = 0; i < ISSD_TOUCH_COUNT; i++) T.r[i].pressed = false;
    T.mask = 0;

    if (!T.enabled || T.vw == 0) { T.menu_down = false; return; }

    const int pad_margin = T.vh / 60;
    bool menu_now = false, hide_now = false;
    int unhandled_x = -1, unhandled_y = -1;

    for (int i = 0; i < count; i++) {
        const int x = T.px[i], y = T.py[i];
        bool control_hit = false;

        /* The captured thumb is reserved for movement. Sliding onto Menu or a
         * face button must not accidentally activate it. */
        if (T.thumb_held && points[i].device == T.thumb_device &&
            points[i].finger == T.thumb_finger) {
            T.thumb_x = x; T.thumb_y = y;
            uint16_t direction = thumb_direction(&T.r[ISSD_TOUCH_DPAD], x, y);
            T.mask |= direction;
            T.r[ISSD_TOUCH_DPAD].pressed = direction != 0;
            continue;
        }

        /* The menu and hide/show buttons are live whether or not the pad is hidden. */
        if (hit_padded(&T.r[ISSD_TOUCH_MENU], x, y, pad_margin)) {
            menu_now = true;
            T.r[ISSD_TOUCH_MENU].pressed = true;
            control_hit = true;
            continue;
        }
        if (hit_padded(&T.r[ISSD_TOUCH_HIDE], x, y, pad_margin)) {
            hide_now = true;
            T.r[ISSD_TOUCH_HIDE].pressed = true;
            control_hit = true;
            continue;
        }

        if (T.pad_visible) {
            const IssdTouchRect *d = &T.r[ISSD_TOUCH_DPAD];
            if (hit(d, x, y)) {
                if (!T.thumb_held) {
                    T.thumb_held = true;
                    T.thumb_device = points[i].device;
                    T.thumb_finger = points[i].finger;
                    T.thumb_x = x; T.thumb_y = y;
                    uint16_t direction = thumb_direction(d, x, y);
                    T.mask |= direction;
                    T.r[ISSD_TOUCH_DPAD].pressed = direction != 0;
                }
                control_hit = true;
                continue;
            }

            static const struct { IssdTouchControl c; uint16_t bit; } buttons[] = {
                { ISSD_TOUCH_A, ISSD_PAD_A }, { ISSD_TOUCH_B, ISSD_PAD_B },
                { ISSD_TOUCH_X, ISSD_PAD_X }, { ISSD_TOUCH_Y, ISSD_PAD_Y },
                { ISSD_TOUCH_L, ISSD_PAD_L }, { ISSD_TOUCH_R, ISSD_PAD_R },
                { ISSD_TOUCH_START, ISSD_PAD_START },
                { ISSD_TOUCH_SELECT, ISSD_PAD_SELECT },
            };
            for (unsigned b = 0; b < sizeof(buttons) / sizeof(buttons[0]); b++) {
                if (hit_padded(&T.r[buttons[b].c], x, y, pad_margin)) {
                    T.mask |= buttons[b].bit;
                    T.r[buttons[b].c].pressed = true;
                    control_hit = true;
                    break;
                }
            }
        }

        if (!control_hit && unhandled_x < 0) {
            unhandled_x = x;
            unhandled_y = y;
        }
    }

    if (unhandled_x >= 0) {
        if (!T.screen_touch_down) {
            T.screen_tap_fired = true;
            T.screen_tap_x = unhandled_x;
            T.screen_tap_y = unhandled_y;
            T.screen_touch_down = true;
        }
    } else {
        T.screen_touch_down = false;
    }

    /* Both toggles fire on release, so a finger held down does not repeat. */
    if (!menu_now && T.menu_down) T.menu_fired = true;
    if (!hide_now && T.hide_down) {
        T.pad_visible = !T.pad_visible;
        if (!T.pad_visible) {
            T.thumb_held = false;
            T.mask = 0;
            for (int i = 0; i < ISSD_TOUCH_COUNT; ++i) T.r[i].pressed = false;
        }
    }
    T.menu_down = menu_now;
    T.hide_down = hide_now;
}

uint16_t issd_touch_pad_mask(void) { return T.enabled ? T.mask : 0; }

bool issd_touch_take_menu_press(void) {
    bool fired = T.menu_fired;
    T.menu_fired = false;
    return fired;
}

bool issd_touch_take_screen_tap(int *out_x, int *out_y) {
    if (!T.screen_tap_fired) return false;
    T.screen_tap_fired = false;
    if (out_x) *out_x = T.screen_tap_x;
    if (out_y) *out_y = T.screen_tap_y;
    return true;
}

int issd_touch_rects(const IssdTouchRect **out) {
    T.r[ISSD_TOUCH_HIDE].label = T.pad_visible ? "HIDE" : "SHOW";
    if (T.pad_visible) {
        T.r[ISSD_TOUCH_HIDE].color_r = 70;
        T.r[ISSD_TOUCH_HIDE].color_g = 130;
        T.r[ISSD_TOUCH_HIDE].color_b = 180;
    } else {
        T.r[ISSD_TOUCH_HIDE].color_r = 46;
        T.r[ISSD_TOUCH_HIDE].color_g = 160;
        T.r[ISSD_TOUCH_HIDE].color_b = 100;
    }

    for (int i = 0; i < ISSD_TOUCH_COUNT; i++) {
        /* Menu and Hide/Show buttons are never hidden. Everything else follows the pad. */
        T.r[i].visible = T.enabled &&
                         (i == ISSD_TOUCH_MENU || i == ISSD_TOUCH_HIDE || T.pad_visible);
    }
    if (out) *out = T.r;
    return ISSD_TOUCH_COUNT;
}
