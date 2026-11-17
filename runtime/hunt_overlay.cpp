// The hunting aids' drawing (runtime/hunt_aids.h): the damage numbers over the monsters and the monsters' health bars at
// the top, on the overlay's background list (over the game's picture, under the touch controls and the menus), after
// the FPS plate. The overlay is drawn over each picture presented, the game's and frame generation's made ones alike,
// after the upscaler: none of them ever samples it.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hud_parts.h"
#include "config.h"
#include "hunt_aids.h"
#include "imgui.h"
#include "logwriter.h"
#include "perfstats.h"
#include "touch.h"

extern "C" uint64_t plat_now_ns(void);

namespace {

// The numbers: one size for all (Dear ImGui bakes the font at each size it is asked for: a size that changed every frame
// would bake glyphs every frame), of the screen's height by [Hunting Aids] DamageNumberSize (0 Medium, the default, 1
// Small, 2 Large: MH4U for Android's three), and a colour by DamageNumberColour, always with a dark outline
constexpr float SIZES[] = {.075f, .04f, .10f};
constexpr ImU32 COLOURS[] = {IM_COL32(255, 255, 255, 0), IM_COL32(255, 214, 40, 0), IM_COL32(255, 80, 64, 0),
                             IM_COL32(110, 200, 255, 0), IM_COL32(110, 230, 110, 0)};
constexpr float RISE = .08f;     // how far a number rises while it shows, of the screen's height (eased: fast at first)
constexpr float FADE = .6f;      // it fades from 60% of its time on
constexpr float JITTER = .006f;  // numbers at one place: up to 4 steps of this (of the width) apart, by their id

// The bars: the port's own look, as the FPS counter's plate and the touch controls' (perfstats.cpp, touch.cpp plate): a
// wine-dark plate with a black edge and the game's red and moss rules above and below, its ink with a dark drop shadow;
// the health fills the plate's inside from the left in the light red of the plate's warnings. Thin by default: the name
// and the health over a plate a fiftieth of the screen's height. Sizes in the HUD's canvas units (720 of the picture's
// height), times the player's size from the editors
const ImU32 INK = IM_COL32(244, 232, 200, 255), WINE = IM_COL32(50, 25, 24, 230), RED = IM_COL32(176, 34, 30, 255),
            MOSS = IM_COL32(80, 142, 54, 255), LIFE_TOP = IM_COL32(236, 84, 64, 255), LIFE_LOW = IM_COL32(176, 40, 32, 255),
            GLINT = IM_COL32(255, 214, 160, 90);
constexpr float BAR_W = 360.0f, PLATE_H = 14.0f, RULE = 2.1f, NAME_H = 15.0f, HP_H = 13.0f, BLOCK_GAP = 5.0f, TOP = 46.0f;

// The game's picture in the window (touch.cpp's picture(): as wide as it fits in its own shape, centred)
struct Pic {
    float x0, y0, w, h;
};
Pic picture(float w, float h) {
    float a = g_render_aspect > 0 ? g_render_aspect : w / h, pw = w, ph = w / a;
    if (ph > h) ph = h, pw = h * a;
    return {(w - pw) * .5f, (h - ph) * .5f, pw, ph};
}

// Text with the port's drop shadow (touch.cpp text_center's)
void shadowed(ImDrawList *dl, ImFont *font, float size, ImVec2 at, ImU32 colour, const char *text) {
    dl->AddText(font, size, ImVec2(at.x + size * .05f, at.y + size * .06f), IM_COL32(0, 0, 0, 200), text);
    dl->AddText(font, size, at, colour, text);
}

void outlined(ImDrawList *dl, ImFont *font, float size, ImVec2 at, ImU32 colour, int alpha, const char *text, float o) {
    static const float DX[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, DY[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    for (int k = 0; k < 8; ++k)
        dl->AddText(font, size, ImVec2(at.x + DX[k] * o, at.y + DY[k] * o), IM_COL32(0, 0, 0, alpha * 3 / 4), text);
    dl->AddText(font, size, at, (colour & 0x00ffffffu) | IM_COL32(0, 0, 0, alpha), text);
}

void numbers(ImDrawList *dl, const HuntView &v, const Pic &p, float h, uint64_t now) {
    ImFont *font = ImGui::GetFont();
    const float size = std::max(8.0f, std::floor(SIZES[std::clamp(v.number_size, 0, 2)] * h + .5f));
    const ImU32 colour = COLOURS[std::clamp(v.number_colour, 0, 4)];
    dl->PushClipRect(ImVec2(p.x0, p.y0), ImVec2(p.x0 + p.w, p.y0 + p.h), true);
    for (int i = 0; i < v.nnum; i++) {
        const HuntNumber &n = v.num[i];
        float t = (float)((double)(now - n.born_ns) / (double)HUNT_NUMBER_NS);
        if (!n.shown || t > 1 || now < n.born_ns) continue;
        const float rise = RISE * h * (1 - (1 - t) * (1 - t));
        const float jitter = (float)((int)(n.id * 37u % 9u) - 4) * JITTER * p.w;
        char text[16];
        std::snprintf(text, sizeof text, "%d", n.value);
        const ImVec2 sz = font->CalcTextSizeA(size, 1e9f, 0, text);
        const ImVec2 at(p.x0 + n.x * p.w + jitter - sz.x * .5f, p.y0 + n.y * p.h - rise - sz.y * .5f);
        const int a = (int)(255 * std::clamp(t < FADE ? 1 : 1 - (t - FADE) / (1 - FADE), 0.f, 1.f));
        outlined(dl, font, size, at, colour, a, text, std::max(1.f, size * .07f));
    }
    dl->PopClipRect();
}

// Below what is at the top of the screen where the bars go: the hunter's health and stamina (wherever they were moved),
// the target camera, the FPS plate
float bars_top(float x0, float x1, float w, float h, float top) {
    float r[4];
    for (int part : {HP_BARS, HP_TARGET, HP_CLOCK})
        if (touch_hud_zone(part, w, h, r) && r[0] < x1 && r[2] > x0 && r[1] < .3f * h) top = std::max(top, r[3] + .004f * h);
    if (perfstats_rect(r) && r[0] < x1 && r[2] > x0 && r[1] < .2f * h) top = std::max(top, r[3] + .008f * h);
    return top;
}

// Where the bars were drawn last (display pixels) and when: the editors' frame for them (hunt_bars_rect)
std::atomic<float> g_rect[4];
std::atomic<uint64_t> g_rect_ns{0};

// The bars, a block each (the name and the health over the bar), at the player's place and size (the editors': Layout's
// monster_bars, touch_bars_place) or at their own: the top middle of the picture, under what is there
void bars(ImDrawList *dl, const HuntView &v, const Pic &p, float w, float h, uint64_t now) {
    ImFont *font = ImGui::GetFont();
    float px, py, pk;
    touch_bars_place(&px, &py, &pk);
    const float k = p.h / 720.0f * std::clamp(pk, .5f, 2.0f), bw = std::min(BAR_W * k, .9f * w);
    const float name = std::max(8.0f, std::floor(NAME_H * k + .5f)), small = std::max(8.0f, std::floor(HP_H * k + .5f));
    const float bh = std::max(5.0f, PLATE_H * k), line = std::max(1.0f, RULE * k * .5f), block = name + k + bh;
    const float all = v.nbar * block + (v.nbar - 1) * BLOCK_GAP * k;
    float x0, y;
    if (px >= 0 && py >= 0) {
        x0 = std::clamp(px * w - bw * .5f, 2 * k, std::max(2 * k, w - bw - 2 * k));
        y = std::clamp(py * h, 0.0f, std::max(0.0f, h - all - 2 * k));
    } else {
        x0 = p.x0 + p.w * .5f - bw * .5f;
        y = bars_top(x0, x0 + bw, w, h, p.y0 + TOP * p.h / 720.0f);
    }
    g_rect[0] = x0, g_rect[1] = y, g_rect[2] = x0 + bw, g_rect[3] = y + all, g_rect_ns = now;
    for (int i = 0; i < v.nbar; i++) {
        const HuntBar &b = v.bar[i];
        char hp[32];
        std::snprintf(hp, sizeof hp, "%d / %d", b.hp, b.max);
        const ImVec2 hs = font->CalcTextSizeA(small, 1e9f, 0, hp);
        if (b.name[0]) shadowed(dl, font, name, ImVec2(x0 + 3 * k, y), INK, b.name);
        shadowed(dl, font, small, ImVec2(x0 + bw - hs.x - 3 * k, y + name - hs.y), INK, hp);
        const float by = y + name + k, f = b.max > 0 ? std::clamp((float)b.hp / (float)b.max, 0.f, 1.f) : 0.f;
        const ImVec2 p0(x0, by), p1(x0 + bw, by + bh);
        dl->AddRectFilled(p0, p1, WINE);  // (the plate: perfstats.cpp's)
        const float in0 = p0.y + line * 1.7f, in1 = p1.y - line * 1.7f, ix0 = p0.x + line, ix1 = p1.x - line;
        if (f > 0 && in1 > in0) {  // (the health, inside the rules)
            const float fx = ix0 + (ix1 - ix0) * f;
            dl->AddRectFilledMultiColor(ImVec2(ix0, in0), ImVec2(fx, in1), LIFE_TOP, LIFE_TOP, LIFE_LOW, LIFE_LOW);
            dl->AddRectFilled(ImVec2(ix0, in0), ImVec2(fx, in0 + std::max(1.0f, (in1 - in0) * .25f)), GLINT);
        }
        dl->AddRect(p0, p1, IM_COL32(0, 0, 0, 255), 0, 0, line);
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + line), RED);
        dl->AddRectFilled(ImVec2(p0.x, p0.y + line), ImVec2(p1.x, p0.y + line * 1.7f), MOSS);
        dl->AddRectFilled(ImVec2(p0.x, p1.y - line * 1.7f), ImVec2(p1.x, p1.y - line), MOSS);
        dl->AddRectFilled(ImVec2(p0.x, p1.y - line), p1, RED);
        y = by + bh + BLOCK_GAP * k;
    }
}

// What there is to draw now: the game side's view, and in an editor (the touch layout editor, the HUD editor) with the
// bars on, a stand-in bar while no monster has one, so they can be placed outside a quest
bool what(HuntView *v, bool *editing) {
    bool any = huntaids_view(v);
    *editing = config_live_monster_bars() && touch_editor_open();
    if (!*editing) return any;
    if (!any) v->nnum = 0, v->nbar = 0;
    if (!v->nbar) {
        std::snprintf(v->bar[0].name, sizeof v->bar[0].name, "%s", config_language() == 1 ? "Monstruo" : "Monster");
        v->bar[0].hp = v->bar[0].max = 100, v->nbar = 1;
    }
    return true;
}

// What a frame draws, for MHGU_PERF_OVERLAY_REUSE: 0 nothing, ~0 numbers (they move every frame) or an editor, else a
// hash of the bars and their place
uint64_t g_drawn;
uint64_t key_of(bool any, bool editing, const HuntView &v, float w, float h) {
    if (!any || w <= 0 || h <= 0 || (!editing && touch_game_menu())) return 0;
    if (v.nnum || editing) return ~0ull;
    uint64_t k = 1469598103934665603ull;  // (FNV-1a)
    auto mix = [&k](const void *p, size_t n) {
        for (size_t i = 0; i < n; i++) k = (k ^ ((const unsigned char *)p)[i]) * 1099511628211ull;
    };
    float place[3];
    touch_bars_place(&place[0], &place[1], &place[2]);
    mix(&w, sizeof w), mix(&h, sizeof h), mix(place, sizeof place), mix(&v.nbar, sizeof v.nbar);
    for (int i = 0; i < v.nbar; i++) mix(v.bar[i].name, strlen(v.bar[i].name)), mix(&v.bar[i].hp, sizeof(int) * 2);
    return k == 0 || k == ~0ull ? 1 : k;
}

}  // namespace

extern "C" bool hunt_overlay_unchanged(float w, float h) {
    HuntView v;
    bool editing, any = what(&v, &editing);
    uint64_t k = key_of(any, editing, v, w, h);
    return k != ~0ull && k == g_drawn;
}

extern "C" void hunt_overlay_draw(float w, float h) {
    HuntView v;
    bool editing, any = what(&v, &editing);
    g_drawn = key_of(any, editing, v, w, h);
    if (!g_drawn) return;
    const Pic p = picture(w, h);
    const uint64_t now = plat_now_ns();
    ImDrawList *dl = ImGui::GetBackgroundDrawList();  // (under the touch controls and the menus, as the FPS plate)
    if (v.nbar) bars(dl, v, p, w, h, now);
    if (v.nnum) numbers(dl, v, p, h, now);
    static int cost = -1;  // ((debug) MHGU_HUNT_COST=1: this drawing's time too, every 10 s, as huntaids.c's step)
    if (cost < 0) cost = getenv("MHGU_HUNT_COST") && atoi(getenv("MHGU_HUNT_COST")) > 0;
    if (cost) {
        static uint64_t sum, most, since, frames;
        uint64_t t = plat_now_ns() - now;
        sum += t, frames++, most = std::max(most, t);
        if (!since) since = now;
        if (now - since >= 10000000000ull) {
            rt_logf("[hunt] drawing: %.2f us a frame drawn on average, %.1f at most (%llu frames, %d numbers, %d bars now)\n",
                    sum / 1e3 / (double)frames, most / 1e3, (unsigned long long)frames, v.nnum, v.nbar);
            sum = most = frames = 0, since = now;
        }
    }
}

extern "C" bool hunt_bars_rect(float r[4]) {
    uint64_t at = g_rect_ns.load();
    if (!at || plat_now_ns() - at > 400000000ull) return false;
    for (int i = 0; i < 4; i++) r[i] = g_rect[i].load();
    return true;
}
