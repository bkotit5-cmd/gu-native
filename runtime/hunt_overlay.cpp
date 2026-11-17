// The hunting aids' drawing (runtime/hunt_aids.h): the damage numbers over the monsters and the monsters' health bars at
// the top (MH4U for Android's, the same look), on the overlay's background list (over the game's picture, under the
// touch controls and the menus), after the FPS plate. The overlay is drawn over each picture presented, the game's and frame generation's made ones alike,
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

// The bars: MH4U for Android's, the same in every measure and colour (its app_native/hunt_bars.cpp; the owner: "identical
// to the 4U's"), of the game's picture's height where MH4U's are of its top screen's: a plate 46% long and 3% tall, one
// under the other, a third of a plate apart; the plate the FPS counter's (wine, a black edge, red and moss rules at the
// top and bottom), a groove in it the health fills in the FPS counter's green (over half), gold (over a quarter) or red,
// the numbers ("1234 / 2546") in ink at its right end; a monster in another zone of the map fainter. No name: MH4U's has
// none
constexpr float kLength = .46f;
constexpr float kShortest = .32f;  // shorter than this beside the hunter's bars, they go under them
constexpr float kHeight = .03f;
constexpr float kMargin = .02f;    // from the top and right edges, and from the HUD's parts they keep clear of
constexpr float kGap = .3f;        // between two plates, of a plate's height
constexpr float kAway = .55f;      // a monster in another zone of the map
constexpr float kText = .64f;      // the numbers, of a plate's height
constexpr float kNumbers = 4.4f;   // the room they take at the plate's right end, of a plate's height
constexpr ImU32 kWine = IM_COL32(50, 25, 24, 255), kRule = IM_COL32(176, 34, 30, 255), kMoss = IM_COL32(80, 142, 54, 255),
                kDeep = IM_COL32(28, 20, 15, 255), kInk = IM_COL32(244, 232, 200, 255),
                kGood = IM_COL32(132, 204, 96, 255), kGold = IM_COL32(222, 186, 96, 255),
                kBad = IM_COL32(236, 84, 64, 255);

ImU32 alpha_of(ImU32 c, float a) {
    return (c & 0x00ffffffu) | ((ImU32)std::clamp(((c >> 24) & 0xff) * a, 0.f, 255.f) << 24);
}

// The game's picture in the window (touch.cpp's picture(): as wide as it fits in its own shape, centred)
struct Pic {
    float x0, y0, w, h;
};
Pic picture(float w, float h) {
    float a = g_render_aspect > 0 ? g_render_aspect : w / h, pw = w, ph = w / a;
    if (ph > h) ph = h, pw = h * a;
    return {(w - pw) * .5f, (h - ph) * .5f, pw, ph};
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

// Where the bars were drawn last (display pixels: their plates' block) and when: the editors' frame for them
// (hunt_bars_rect)
std::atomic<float> g_rect[4];
std::atomic<uint64_t> g_rect_ns{0};

// A part of the game's HUD where it was drawn last (display pixels: touch_hud_zone, its art and a margin), else where the
// HUD puts it (hud_parts.h hud_box, with the player's moves); false while it is hidden
bool part_box(int part, const Pic &p, float w, float h, float r[4]) {
    if (touch_hud_zone(part, w, h, r)) return true;
    int prof = g_hud_prof_now;
    if (prof < 0 || prof >= HUD_PROFILES) prof = g_touch_hud ? HUD_TOUCH : HUD_PLAIN;
    if (g_hud_hidden[prof][part]) return false;
    float b[4];
    hud_box(part, prof, g_hud_arts_top, b);
    r[0] = p.x0 + b[0] * p.w / 1280.0f, r[1] = p.y0 + b[1] * p.h / 720.0f;
    r[2] = p.x0 + b[2] * p.w / 1280.0f, r[3] = p.y0 + b[3] * p.h / 720.0f;
    return true;
}

// Where the HUD leaves room for the bars at their own place (MH4U's hunt_bars_room, for MHGU's HUD): they end left of the
// target camera, in the top right corner (MH4U's: under the parts in the corner; here the minimap sits under the camera),
// and start after the hunter's health and stamina, or go under them
struct Room {
    float right, left, under;
};
Room room_of(const Pic &p, float w, float h) {
    const float m = kMargin * p.h;
    Room r{p.x0 + p.w - m, p.x0, p.y0};
    float z[4];
    if (part_box(HP_TARGET, p, w, h, z) && z[1] < p.y0 + .34f * p.h && z[0] > p.x0 + .5f * p.w)
        r.right = std::min(r.right, z[0] - m);
    if (part_box(HP_BARS, p, w, h, z) && z[1] < p.y0 + .34f * p.h) r.left = z[2], r.under = z[3];
    return r;
}

// The bars (MH4U's hunt_bars_layout and hunt_bars_draw): at their own place, from the top, the right end left of the
// target camera, as long as they fit after the hunter's bars (under them at full length when that leaves less than
// kShortest), under the FPS plate if it is over them; placed in an editor (touch_bars_place: their top middle, the
// screen's fractions), there at full length, kept on the picture; times the editors' size
void bars(ImDrawList *dl, const HuntView &v, const Pic &p, float w, float h, uint64_t now) {
    float px, py, k;
    touch_bars_place(&px, &py, &k);
    k = std::clamp(k, .5f, 2.f);
    const float bh = std::max(5.f, std::floor(kHeight * k * p.h + .5f)), m = kMargin * p.h;
    const float gap = std::max(2.f, std::floor(kGap * bh + .5f)), length = kLength * k * p.h;
    const float n = (float)v.nbar, block = n * bh + std::max(0.f, n - 1) * gap;
    float x, y, bw;
    if (px < 0 || py < 0) {
        const Room r = room_of(p, w, h);
        bw = std::min(length, r.right - std::max(r.left + m, p.x0 + m));
        y = p.y0 + m;
        if (bw < kShortest * k * p.h) {  // (no room beside the hunter's bars: under them)
            bw = std::min(length, r.right - (p.x0 + m));
            y = std::max(y, r.under + m);
        }
        x = r.right - bw;
        float f[4];  // (the FPS plate at the top over them: under it)
        if (perfstats_rect(f) && f[0] < x + bw && f[2] > x && f[1] < p.y0 + .2f * p.h && f[3] > y) y = f[3] + m * .5f;
    } else {
        bw = std::min(length, p.w);
        x = std::clamp(px * w - bw * .5f, p.x0, std::max(p.x0, p.x0 + p.w - bw));
        y = std::clamp(py * h, p.y0, std::max(p.y0, p.y0 + p.h - block));
    }
    y = std::floor(y);
    g_rect[0] = x, g_rect[1] = y, g_rect[2] = x + bw, g_rect[3] = y + block, g_rect_ns = now;
    const float l = std::max(1.f, std::floor(bh * .09f + .5f)), pad = std::max(2.f, std::floor(bh * .25f));
    const float text = std::max(6.f, std::floor(kText * bh)), groove = std::max(2.f, std::floor(bh * .36f));
    const float numbers = std::min(kNumbers * bh, bw * .55f);
    ImFont *font = ImGui::GetFont();
    dl->PushClipRect(ImVec2(p.x0, p.y0), ImVec2(p.x0 + p.w, p.y0 + p.h), true);
    for (int i = 0; i < v.nbar; i++, y += bh + gap) {
        const HuntBar &b = v.bar[i];
        const float a = b.here ? 1.f : kAway, fill = b.max > 0 ? std::clamp((float)b.hp / (float)b.max, 0.f, 1.f) : 0.f;
        const ImU32 colour = fill > .5f ? kGood : fill > .25f ? kGold : kBad;
        const ImVec2 p0(x, y), p1(x + bw, y + bh);
        // the plate (the FPS counter's, perfstats.cpp: wine, a black edge, red and moss rules at the top and bottom)
        dl->AddRectFilled(p0, p1, alpha_of(kWine, a * .9f));
        dl->AddRect(p0, p1, alpha_of(IM_COL32(0, 0, 0, 255), a), 0.f, 0, l);
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + l), alpha_of(kRule, a));
        dl->AddRectFilled(ImVec2(p0.x, p0.y + l), ImVec2(p1.x, p0.y + l * 1.7f), alpha_of(kMoss, a));
        dl->AddRectFilled(ImVec2(p0.x, p1.y - l * 1.7f), ImVec2(p1.x, p1.y - l), alpha_of(kMoss, a));
        dl->AddRectFilled(ImVec2(p0.x, p1.y - l), p1, alpha_of(kRule, a));
        // the groove and the health in it
        const float tw = std::max(4.f, bw - numbers - 2 * pad);
        const ImVec2 t0(x + pad, y + std::floor((bh - groove) * .5f)), t1(t0.x + tw, t0.y + groove);
        dl->AddRectFilled(ImVec2(t0.x - l, t0.y - l), ImVec2(t1.x + l, t1.y + l), alpha_of(IM_COL32(0, 0, 0, 255), a * .8f));
        dl->AddRectFilled(t0, t1, alpha_of(kDeep, a));
        if (fill > 0) {
            const float fx = t0.x + tw * fill;
            dl->AddRectFilled(t0, ImVec2(fx, t1.y), alpha_of(colour, a));
            dl->AddRectFilled(t0, ImVec2(fx, t0.y + std::max(1.f, groove * .34f)), alpha_of(IM_COL32(255, 255, 255, 70), a));
        }
        // the numbers: ink on the plate, a shadow as the port's other texts
        char t[32];
        std::snprintf(t, sizeof t, "%d / %d", b.hp, b.max);
        const ImVec2 size = font->CalcTextSizeA(text, 1e9f, 0, t);
        const ImVec2 at(x + bw - pad - size.x, y + bh * .5f - size.y * .5f);
        dl->AddText(font, text, ImVec2(at.x + text * .05f, at.y + text * .06f), alpha_of(IM_COL32(0, 0, 0, 200), a), t);
        dl->AddText(font, text, at, alpha_of(kInk, a), t);
    }
    dl->PopClipRect();
}

// What there is to draw now: the game side's view, and in an editor (the touch layout editor, the HUD editor) with the
// bars on, a stand-in bar while no monster has one (MH4U's: 1900 / 2546), so they can be placed outside a quest
bool what(HuntView *v, bool *editing) {
    bool any = huntaids_view(v);
    *editing = config_live_monster_bars() && touch_editor_open();
    if (!*editing) return any;
    if (!any) v->nnum = 0, v->nbar = 0;
    if (!v->nbar) v->bar[0].hp = 1900, v->bar[0].max = 2546, v->bar[0].here = true, v->nbar = 1;
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
    for (int i = 0; i < v.nbar; i++) mix(&v.bar[i].hp, sizeof(int)), mix(&v.bar[i].max, sizeof(int)), mix(&v.bar[i].here, 1);
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
