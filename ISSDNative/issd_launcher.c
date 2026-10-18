/* Pre-game launcher, after the ISS 2000 recomp's: its own SDL 2D-renderer
 * window, embedded logo/ball/font, ROM selection by file dialog or drag and
 * drop. The layout is a console-style vertical menu instead of a button
 * row, so it can be driven entirely by a controller. */
#include "issd_launcher.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "launcher_picker.h"
#include "sha256.h"
#include "issd_password.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "generated/launcher_assets.h"

#define LAUNCHER_TITLE "ISS DELUXE'd"
enum { VIEW_W = 1280, VIEW_H = 720, ATLAS = 1024 };

/* ---- text (stb_truetype baked atlas) ---------------------------------- */

typedef struct {
    SDL_Texture *texture;
    stbtt_bakedchar chars[96];
    float size;
} Font;

static Font bake_font(SDL_Renderer *renderer, float pixel_height) {
    Font font = {0};
    font.size = pixel_height;
    uint8_t *alpha = (uint8_t *)calloc(ATLAS * ATLAS, 1);
    uint32_t *rgba = (uint32_t *)malloc(ATLAS * ATLAS * sizeof *rgba);
    if (!alpha || !rgba) { free(alpha); free(rgba); return font; }
    stbtt_BakeFontBitmap(launcher_font_ttf, 0, pixel_height, alpha, ATLAS, ATLAS, 32, 96, font.chars);
    for (int i = 0; i < ATLAS * ATLAS; i++) rgba[i] = 0x00FFFFFFu | ((uint32_t)alpha[i] << 24);
    font.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, ATLAS, ATLAS);
    if (font.texture) {
        SDL_UpdateTexture(font.texture, NULL, rgba, ATLAS * 4);
        SDL_SetTextureBlendMode(font.texture, SDL_BLENDMODE_BLEND);
    }
    free(alpha); free(rgba);
    return font;
}

static float text_width(const Font *font, const char *text) {
    float x = 0, y = 0;
    for (const unsigned char *c = (const unsigned char *)text; *c; c++) {
        if (*c < 32 || *c >= 128) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(font->chars, ATLAS, ATLAS, *c - 32, &x, &y, &q, 1);
    }
    return x;
}

/* Text with its top-left at (x, y). */
static void draw_text(SDL_Renderer *renderer, const Font *font, const char *text,
                      float x, float y, SDL_Color color) {
    if (!font->texture) return;
    SDL_SetTextureColorMod(font->texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(font->texture, color.a);
    float baseline = y + font->size * 0.8f;
    for (const unsigned char *c = (const unsigned char *)text; *c; c++) {
        if (*c < 32 || *c >= 128) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(font->chars, ATLAS, ATLAS, *c - 32, &x, &baseline, &q, 1);
        SDL_Rect src = { (int)(q.s0 * ATLAS), (int)(q.t0 * ATLAS),
                         (int)((q.s1 - q.s0) * ATLAS), (int)((q.t1 - q.t0) * ATLAS) };
        SDL_FRect dst = { q.x0, q.y0, q.x1 - q.x0, q.y1 - q.y0 };
        SDL_RenderCopyF(renderer, font->texture, &src, &dst);
    }
}

static void draw_text_centered(SDL_Renderer *renderer, const Font *font, const char *text,
                               float cx, float y, SDL_Color color) {
    draw_text(renderer, font, text, cx - text_width(font, text) / 2, y, color);
}

static void fill_rect(SDL_Renderer *renderer, SDL_FRect r, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRectF(renderer, &r);
}

static SDL_Texture *load_png(SDL_Renderer *renderer, const uint8_t *png, size_t size, int *w, int *h) {
    int channels = 0;
    uint8_t *pixels = stbi_load_from_memory(png, (int)size, w, h, &channels, 4);
    if (!pixels) return NULL;
    SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, *w, *h);
    if (tex) {
        SDL_UpdateTexture(tex, NULL, pixels, *w * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    stbi_image_free(pixels);
    return tex;
}

/* ---- ROM check --------------------------------------------------------- */

bool issd_launcher_rom_ok(const char *path, char *why, size_t why_size) {
    enum { IMAGE = 2 * 1024 * 1024 };
    if (!path || !path[0]) { snprintf(why, why_size, "No ROM selected."); return false; }
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(why, why_size, "Could not open that file."); return false; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    /* Copier dumps carry a 512-byte header before the cartridge image. */
    long base = size % 1024 == 512 ? 512 : 0;
    uint8_t *image = size - base == IMAGE ? (uint8_t *)malloc(IMAGE) : NULL;
    bool read = image && fseek(f, base, SEEK_SET) == 0 && fread(image, 1, IMAGE, f) == IMAGE;
    fclose(f);
    if (!read) {
        free(image);
        snprintf(why, why_size, "That is not the International Superstar Soccer Deluxe (USA) ROM.");
        return false;
    }
    /* Only the exact USA cartridge: other versions or modified dumps would
     * play differently, and online both players must run the same game. */
    uint8_t digest[32];
    sha256_compute(image, IMAGE, digest);
    bool same_title = memcmp(image + 0x7FC0, "SUPERSTAR SOCCER 2", 18) == 0;
    free(image);
    if (memcmp(digest, issd_supported_rom_sha256, sizeof digest) != 0) {
        snprintf(why, why_size, same_title
                 ? "That is a different version or a modified dump. Only the original USA ROM works."
                 : "That is not the International Superstar Soccer Deluxe (USA) ROM.");
        return false;
    }
    return true;
}

static const char *file_name(const char *path) {
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *back = strrchr(path, '\\');
    if (back && (!slash || back > slash)) slash = back;
#endif
    return slash ? slash + 1 : path;
}

/* ---- menu ---------------------------------------------------------------- */

typedef enum { PAGE_WELCOME, PAGE_MAIN, PAGE_ONLINE, PAGE_OPTIONS, PAGE_CONTROLS } Page;
enum { WELCOME_ROM, WELCOME_QUIT, WELCOME_COUNT };
enum { MAIN_PLAY, MAIN_ONLINE, MAIN_OPTIONS, MAIN_CONTROLS, MAIN_QUIT, MAIN_COUNT };
enum { OPT_FULLSCREEN, OPT_WINDOW, OPT_VOLUME, OPT_BALL_SHADOW, OPT_ROM, OPT_BACK, OPT_COUNT };

typedef struct {
    Page page;
    int focus;
    bool rom_ok;
    char status[160];
    bool status_error;
    char *rom_path;
    size_t rom_path_size;
    IssdConfig *cfg;
    bool play, running;
    SDL_FRect items[8];
    int item_count;
} Launcher;

static int page_count(const Launcher *l) {
    return l->page == PAGE_MAIN ? MAIN_COUNT : l->page == PAGE_OPTIONS ? OPT_COUNT :
           l->page == PAGE_WELCOME ? WELCOME_COUNT : 1;
}

static void use_rom(Launcher *l, const char *chosen) {
    char why[160], path[1024];
    snprintf(path, sizeof path, "%s", chosen);   /* chosen may be l->rom_path itself */
    if (issd_launcher_rom_ok(path, why, sizeof why)) {
        snprintf(l->rom_path, l->rom_path_size, "%s", path);
        l->rom_ok = true;
        snprintf(l->status, sizeof l->status, "ROM: %s", file_name(path));
        l->status_error = false;
        if (l->page == PAGE_WELCOME) { l->page = PAGE_MAIN; l->focus = MAIN_PLAY; }
    } else {
        snprintf(l->status, sizeof l->status, "%s", why);
        l->status_error = true;
    }
}

static void pick_rom(Launcher *l) {
    char path[1024] = {0};
    if (snesrecomp_pick_rom_file(path, sizeof path) && path[0]) use_rom(l, path);
}

static void open_page(Launcher *l, Page page, int focus) { l->page = page; l->focus = focus; }

static void change_option(Launcher *l, int item, int dir) {
    IssdConfig *cfg = l->cfg;
    switch (item) {
    case OPT_FULLSCREEN: cfg->fullscreen = !cfg->fullscreen; break;
    case OPT_WINDOW: cfg->output_resolution = (cfg->output_resolution + dir + 5) % 5; break;
    case OPT_VOLUME:
        cfg->master_volume += dir * 10;
        if (cfg->master_volume < 0) cfg->master_volume = 0;
        if (cfg->master_volume > 100) cfg->master_volume = 100;
        break;
    case OPT_BALL_SHADOW: cfg->ball_shadow = !cfg->ball_shadow; break;
    default: break;
    }
}

static void activate(Launcher *l) {
    switch (l->page) {
    case PAGE_WELCOME:
        if (l->focus == WELCOME_ROM) pick_rom(l);
        else l->running = false;
        break;
    case PAGE_MAIN:
        switch (l->focus) {
        case MAIN_PLAY:
            if (l->rom_ok) { l->play = true; l->running = false; }
            else pick_rom(l);
            break;
        case MAIN_ONLINE: open_page(l, PAGE_ONLINE, 0); break;
        case MAIN_OPTIONS: open_page(l, PAGE_OPTIONS, 0); break;
        case MAIN_CONTROLS: open_page(l, PAGE_CONTROLS, 0); break;
        case MAIN_QUIT: l->running = false; break;
        }
        break;
    case PAGE_OPTIONS:
        if (l->focus == OPT_ROM) pick_rom(l);
        else if (l->focus == OPT_BACK) open_page(l, PAGE_MAIN, MAIN_OPTIONS);
        else change_option(l, l->focus, 1);
        break;
    case PAGE_ONLINE: open_page(l, PAGE_MAIN, MAIN_ONLINE); break;
    case PAGE_CONTROLS: open_page(l, PAGE_MAIN, MAIN_CONTROLS); break;
    }
}

static void back(Launcher *l) {
    if (l->page == PAGE_MAIN || l->page == PAGE_WELCOME) l->running = false;
    else open_page(l, PAGE_MAIN, l->page == PAGE_OPTIONS ? MAIN_OPTIONS :
                                 l->page == PAGE_ONLINE ? MAIN_ONLINE : MAIN_CONTROLS);
}

static void move(Launcher *l, int dir) {
    int n = page_count(l);
    l->focus = (l->focus + dir + n) % n;
}

static void sideways(Launcher *l, int dir) {
    if (l->page == PAGE_OPTIONS && l->focus <= OPT_BALL_SHADOW) change_option(l, l->focus, dir);
}

static const char *main_label(int item, bool rom_ok) {
    static const char *labels[MAIN_COUNT] = { "PLAY", "ONLINE", "OPTIONS", "CONTROLS", "QUIT" };
    return item == MAIN_PLAY && !rom_ok ? "SELECT ROM" : labels[item];
}

static void option_label(const IssdConfig *cfg, int item, char *out, size_t size) {
    static const char *windows[] = { "AUTO", "1280 x 720", "1920 x 1080", "2560 x 1440", "3840 x 2160" };
    switch (item) {
    case OPT_FULLSCREEN: snprintf(out, size, "FULLSCREEN   %s", cfg->fullscreen ? "ON" : "OFF"); break;
    case OPT_WINDOW: snprintf(out, size, "WINDOW   %s", windows[cfg->output_resolution % 5]); break;
    case OPT_VOLUME: snprintf(out, size, "VOLUME   %d%%", cfg->master_volume); break;
    case OPT_BALL_SHADOW: snprintf(out, size, "BALL SHADOW   %s", cfg->ball_shadow ? "ON" : "OFF"); break;
    case OPT_ROM: snprintf(out, size, "CHANGE ROM..."); break;
    default: snprintf(out, size, "BACK"); break;
    }
}

/* ---- main loop ------------------------------------------------------------- */

bool issd_launcher_run(IssdConfig *cfg, char *rom_path, size_t rom_path_size) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "[Launcher] SDL init failed: %s\n", SDL_GetError());
        char why[160];
        return issd_launcher_rom_ok(rom_path, why, sizeof why);
    }
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                   (cfg->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_Window *window = SDL_CreateWindow(LAUNCHER_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          VIEW_W, VIEW_H, flags);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED |
                                                         SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (window && !renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "[Launcher] No window: %s\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
        char why[160];
        return issd_launcher_rom_ok(rom_path, why, sizeof why);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    int logo_w = 0, logo_h = 0, ball_w = 0, ball_h = 0;
    SDL_Texture *logo = load_png(renderer, launcher_logo_png, sizeof launcher_logo_png, &logo_w, &logo_h);
    SDL_Texture *ball = load_png(renderer, launcher_ball_png, sizeof launcher_ball_png, &ball_w, &ball_h);
    Font big = bake_font(renderer, 44), normal = bake_font(renderer, 28), small = bake_font(renderer, 20);

    Launcher l = {0};
    l.cfg = cfg;
    l.rom_path = rom_path;
    l.rom_path_size = rom_path_size;
    l.running = true;
    l.page = PAGE_MAIN;
    if (rom_path[0]) use_rom(&l, rom_path);
    if (!l.rom_ok) {
        /* First run, or the saved ROM moved: ask for it before anything else. */
        l.page = PAGE_WELCOME;
        l.focus = WELCOME_ROM;
        l.status[0] = '\0';
        l.status_error = false;
    }
    SDL_GameController *pad = NULL;
    Sint16 last_stick = 0;

    while (l.running) {
        int out_w = 0, out_h = 0, win_w = 0, win_h = 0;
        SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
        SDL_GetWindowSize(window, &win_w, &win_h);
        const float dpi = win_w > 0 ? (float)out_w / win_w : 1.0f;
        /* Lay out in a 1280x720 virtual space scaled to the window. */
        float scale = (float)out_w / VIEW_W < (float)out_h / VIEW_H ? (float)out_w / VIEW_W : (float)out_h / VIEW_H;
        if (scale <= 0) scale = 1;
        SDL_RenderSetScale(renderer, scale, scale);
        const float vw = out_w / scale, vh = out_h / scale;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: l.running = false; break;
            case SDL_DROPFILE: use_rom(&l, e.drop.file); SDL_free(e.drop.file); break;
            case SDL_MOUSEMOTION:
                for (int i = 0; i < l.item_count; i++) {
                    SDL_FRect r = l.items[i];
                    float x = e.motion.x * dpi / scale, y = e.motion.y * dpi / scale;
                    if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) l.focus = i;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    float x = e.button.x * dpi / scale, y = e.button.y * dpi / scale;
                    for (int i = 0; i < l.item_count; i++) {
                        SDL_FRect r = l.items[i];
                        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) { l.focus = i; activate(&l); break; }
                    }
                } else if (e.button.button == SDL_BUTTON_RIGHT) back(&l);
                break;
            case SDL_KEYDOWN:
                switch (e.key.keysym.sym) {
                case SDLK_UP: case SDLK_w: move(&l, -1); break;
                case SDLK_DOWN: case SDLK_s: case SDLK_TAB: move(&l, 1); break;
                case SDLK_LEFT: case SDLK_a: sideways(&l, -1); break;
                case SDLK_RIGHT: case SDLK_d: sideways(&l, 1); break;
                case SDLK_RETURN: case SDLK_SPACE: case SDLK_KP_ENTER: activate(&l); break;
                case SDLK_ESCAPE: case SDLK_BACKSPACE: back(&l); break;
                case SDLK_F11: cfg->fullscreen = !cfg->fullscreen; break;
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!pad) pad = SDL_GameControllerOpen(e.cdevice.which);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: move(&l, -1); break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: move(&l, 1); break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: sideways(&l, -1); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: sideways(&l, 1); break;
                case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: activate(&l); break;
                case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_BACK: back(&l); break;
                }
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    Sint16 v = e.caxis.value;
                    int was = last_stick < -16000 ? -1 : last_stick > 16000 ? 1 : 0;
                    int now = v < -16000 ? -1 : v > 16000 ? 1 : 0;
                    if (now && now != was) move(&l, now);
                    last_stick = v;
                }
                break;
            }
        }
        if (!l.running) break;
        /* Fullscreen follows the option immediately, so it can be judged here. */
        bool is_full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
        if (is_full != cfg->fullscreen) SDL_SetWindowFullscreen(window, cfg->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);

        /* Background: the logo across the top, slightly darkened. */
        SDL_SetRenderDrawColor(renderer, 6, 8, 16, 255);
        SDL_RenderClear(renderer);
        if (logo) {
            float lw = vw * 0.9f, lh = lw * logo_h / logo_w;
            if (lh > vh * 0.42f) { lh = vh * 0.42f; lw = lh * logo_w / logo_h; }
            SDL_FRect dst = { (vw - lw) / 2, vh * 0.04f, lw, lh };
            SDL_SetTextureColorMod(logo, 175, 175, 175);
            SDL_RenderCopyF(renderer, logo, NULL, &dst);
        }

        /* Menu panel. */
        const float panel_w = 560, top = vh * 0.50f;
        const float row = l.page == PAGE_OPTIONS ? 46 : 54;
        fill_rect(renderer, (SDL_FRect){ (vw - panel_w) / 2, top - 18, panel_w, vh * 0.88f - top + 18 },
                  (SDL_Color){ 0, 0, 0, 150 });
        const SDL_Color gold = { 255, 214, 64, 255 }, plain = { 225, 230, 245, 255 },
                        dim = { 140, 146, 165, 255 };
        l.item_count = 0;
        const float t = SDL_GetTicks() / 1000.0f;

        if (l.page == PAGE_WELCOME) {
            draw_text_centered(renderer, &normal, "WELCOME TO ISS DELUXE'd", vw / 2, top - 4, gold);
            draw_text_centered(renderer, &small, "To play, select your International Superstar Soccer Deluxe (USA)",
                               vw / 2, top + 40, plain);
            draw_text_centered(renderer, &small, "ROM file. You can also drop the file onto this window.",
                               vw / 2, top + 66, plain);
        }
        if (l.page == PAGE_MAIN || l.page == PAGE_OPTIONS || l.page == PAGE_WELCOME) {
            const Font *font = l.page == PAGE_OPTIONS ? &normal : &big;
            const float first = l.page == PAGE_WELCOME ? top + 112 : top;
            int n = page_count(&l);
            for (int i = 0; i < n; i++) {
                char label[64];
                if (l.page == PAGE_MAIN) snprintf(label, sizeof label, "%s", main_label(i, l.rom_ok));
                else if (l.page == PAGE_WELCOME) snprintf(label, sizeof label, "%s", i == WELCOME_ROM ? "SELECT ROM" : "QUIT");
                else option_label(cfg, i, label, sizeof label);
                float y = first + i * row;
                float w = text_width(font, label);
                l.items[l.item_count++] = (SDL_FRect){ (vw - panel_w) / 2, y - 6, panel_w, row };
                bool focused = i == l.focus;
                draw_text_centered(renderer, font, label, vw / 2, y, focused ? gold : plain);
                if (focused && ball) {
                    /* The golden ball bobs beside the selected item. */
                    float size = font->size * 1.05f, bob = sinf(t * 5.0f) * 3.0f;
                    SDL_FRect b = { vw / 2 - w / 2 - size - 18 + bob, y + (font->size - size) / 2 + 2, size, size };
                    SDL_RenderCopyF(renderer, ball, NULL, &b);
                }
            }
            if (l.page == PAGE_OPTIONS)
                draw_text_centered(renderer, &small, "Left / Right to change", vw / 2, top + n * row + 4, dim);
        } else if (l.page == PAGE_ONLINE) {
            draw_text_centered(renderer, &big, "ONLINE", vw / 2, top, gold);
            draw_text_centered(renderer, &normal, "Coming soon: host a match and share an invite", vw / 2, top + 70, plain);
            draw_text_centered(renderer, &normal, "code, or join a friend's match with theirs.", vw / 2, top + 106, plain);
            draw_text_centered(renderer, &small, "Press A / Enter to go back", vw / 2, top + 170, dim);
            l.items[l.item_count++] = (SDL_FRect){ (vw - panel_w) / 2, top, panel_w, 200 };
        } else {
            static const char *keys[][2] = {
                { "Move", "Arrows / WASD" }, { "Short pass (B)", "Z / J" }, { "Shoot (A)", "X / K" },
                { "Dash (Y)", "C / U" }, { "Through ball (X)", "V / I" }, { "L / R", "Q / E" },
                { "Start", "Enter" }, { "Select", "Space" },
            };
            draw_text_centered(renderer, &normal, "KEYBOARD  (controllers work automatically)", vw / 2, top - 6, gold);
            for (int i = 0; i < 8; i++) {
                float y = top + 34 + i * 27;
                draw_text(renderer, &small, keys[i][0], vw / 2 - 230, y, plain);
                draw_text(renderer, &small, keys[i][1], vw / 2 + 40, y, plain);
            }
            l.items[l.item_count++] = (SDL_FRect){ (vw - panel_w) / 2, top, panel_w, 260 };
        }

        /* ROM status along the bottom. */
        draw_text_centered(renderer, &small, l.status, vw / 2, vh * 0.92f,
                           l.status_error ? (SDL_Color){ 255, 120, 110, 255 } :
                           l.rom_ok ? (SDL_Color){ 150, 230, 150, 255 } : dim);
        SDL_RenderPresent(renderer);
    }

    if (pad) SDL_GameControllerClose(pad);
    SDL_DestroyTexture(big.texture);
    SDL_DestroyTexture(normal.texture);
    SDL_DestroyTexture(small.texture);
    if (logo) SDL_DestroyTexture(logo);
    if (ball) SDL_DestroyTexture(ball);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
    return l.play;
}
