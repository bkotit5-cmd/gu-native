// Hunting aids (1.0.1, the owner's: "everything off by default, so whoever wants it turns it on"), the game's side: once a
// frame, on the game's update thread after its update (runtime/overlay.cpp overlay_frame), from the game's memory alone.
// No hook of the game's code: off, nothing here reads or writes anything. runtime/hunt_aids.h says what each aid is;
// runtime/hunt_overlay.cpp draws the numbers and the bars.
//
// The monsters (main v1.4.0, runtime addresses): sEnemy (*0x1a51fe0) +0x1c, ten slots: the quest's large monsters
// (whatever zone they are in) and the small ones of the hunter's zone. A monster: +0xb5f7 0 a large one (the Target Cam's
// own test, main 0x22194c), +0xb5f9 not 0 while it takes part (the map's test below), +0xb5f4 its em number, +0xb5f5 its
// variant (the monster id, small << 12 | variant << 8 | em number, as runtime/hle/presence.c reads quests), +0x1428 its
// status block (inside it: +0x1440), +0x73e0 / +0x73e1 its state (dead 0xb or 0xe, captured 0xc / 0xff: main 0x27ffb8).
// The status block: +0x40 the place (the feet), +0x370 the health (s32), +0x374 the most (the HP setters 0x29a7b8,
// 0x29a764: health + n, clamped to [0, most]), +0x1c4 the mark's timer below, +0x4f0 where the hit the monster reacts to
// landed (a vec3 the hunters' hits write, 0x371350 .. 0x376ad0, as they register: read by 0x29a828, the damage
// step's end, for the flinch's place). A model's skeleton (mousecam.c): +0x490 the joints, +0x494 their array, 0xa0 bytes
// each, the world place at +0x40, +0x91 the parent (0xff the root, the pelvis: the body's middle).
//
// Monsters always on the map ([Hunting Aids] MonstersOnMap). The quest maps (the minimap, cUIOQuestMap's update 0x819614,
// and the full map) show a monster when 0x81a86c says so: past the map's own checks (+0xb5f9, 0x270824, 0x26ff44, the
// zone's), a large monster shows while 0x2c0940 is true, its status block's +0x1c4 above 0 (or by the Autotracker's
// kind of skill, the map's +0xd8 +4 bits). That float is the mark's timer, in 60 fps frames: the game's mark (0x2c0908,
// the monster's vtable +0x3c0: a paintball's hit, a skill) sets it to 36000 (10 minutes) unless +0x15e9 bit 2 (a monster
// never marked), and the monster's status step counts it down (0x2c8ddc -> 0x272128). Found from the map: the drawing
// (0x81a064) shows the monsters of the map's +0xdc bit mask, which 0x819614 fills from 0x81a86c. Here, in a quest hunted
// alone, each large monster that takes part and is not marked gets the timer kept between KEEP_LOW and KEEP_HIGH: the
// game then draws its own icon, from the quest's first frame (seen: written in the camp, the monster's pink mark in its
// zone of the minimap at once). A mark of the game's own (36000) is left to the game until it runs down to our range.
// Turned off in a quest, our timers go back to 0 at once (a monster the game had marked is left as it is).
// Multiplayer: the mark is shared with the party by the game's own message, not by the timer: 0x2c0908 also sets bit 0
// of the monster's +0x7429, the status events a hunter's game sends (vtable +0x43c, 0x2cbdc0: message 0x3e with that
// byte), which a receiving game turns into its own mark (0x2ccc88: bit 0 -> vtable +0x3c0) before it clears +0x7429.
// Writing the timer alone sets none of that. But whether nothing else ever sends a status block whole could not be
// settled, so with another hunter in the quest (sPlayer's slots, the quest's join count) nothing is written: the map is
// the game's own there, and the menu's help says so.
//
// Damage numbers ([Hunting Aids] DamageNumbers): each frame, each large monster's health against the last frame's; a
// drop is a number of that much (any damage: the hunter's, the Palicos', bombs, poison, the other hunters' in a party).
// Its place: the reacting hit's (+0x4f0) when it changed since the monster's last number (and is near it), else the body's
// middle (the root joint, or 100 units above the feet). Only for a monster in the hunter's zone of the map (+0x1054 of
// both: each zone has its own coordinates). Placed on the game's picture every frame with the camera the game
// drew this frame with: sCamera (*0x2326504, sFestaCamera) +0x54 the camera, +0xe0 its view matrix and +0x120 its
// projection (the port's aspect and field of view in: tools/gen.py's patch of 0x21c358), row vectors (clip = p * V * P),
// +y up in clip space. Found by matching the monsters' places with a screenshot.
//
// Monster health bars ([Hunting Aids] MonsterHealthBars): each large monster taking part, alive, with its health and the
// most, and whether it is in the hunter's zone of the map (a bar fainter when not, as MH4U's). No name: MH4U's bars have
// none (the owner wants them the same).
//   MHGU_HUNT_LOG=1   (debug) a line per number (the damage, from where, the hit's place or the body's) and per mark
//   MHGU_HUNT_COST=1  (debug) every 10 s: the average and the longest time of this step a frame
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../config.h"
#include "../hle.h"
#include "../hunt_aids.h"

#define G_SENEMY      0x1a51fe0u  // sEnemy *: +0x1c the ten monsters
#define G_SCAMERA     0x2326504u  // sCamera * (sFestaCamera)
#define G_GAMECONTROL 0x1a8ca48u  // sGameControl *: +0x4c the town (0xff in a quest), +0x30 our player slot
#define G_SPLAYER     0x1a85b58u  // sPlayer *: +0x1c the four players
#define G_SQUEST      0x1a8ad90u  // sQuest *: +0xa4 the quest being played
#define G_SELECTOR    0x1a8ae38u  // sQuestSelector *: +0xd750 the quest's join count
#define CAM_VIEW      0xe0u
#define CAM_PROJ      0x120u
#define EN_INFO       0x1428u
#define EN_JOINTS     0x490u
#define EN_ZONE       0x1054u     // (u8: the zone of the map it is in, a hunter's too)
#define EN_NO_MARK    0x15e9u     // bit 2: never marked
#define EN_STATE      0x73e0u
#define EN_EM         0xb5f4u
#define EN_SMALL      0xb5f7u
#define EN_TAKES_PART 0xb5f9u
#define ST_PLACE      0x40u
#define ST_MARK       0x1c4u
#define ST_HP         0x370u
#define ST_HP_MAX     0x374u
#define ST_HIT        0x4f0u
#define KEEP_LOW      600.0f      // the mark's timer kept between these (10 and 20 s at 60 fps): low, so that turned
#define KEEP_HIGH     1200.0f     // off it can be told from a mark of the game's own (36000, counting down)
#define SLOTS         10

static bool sane(uint32_t a) { return a >= 0x08000000u && a < 0xF0000000u && !(a & 3); }

static struct {
    uint32_t en;       // the monster this slot followed last frame (0: none)
    int hp;            // its health then
    float hit[3];      // its +0x4f0 when it last made a number
    bool marked;       // its mark's timer is ours
} g_slot[SLOTS];
static struct { float p[3]; int value; uint32_t id; uint64_t born; } g_num[HUNT_NUMBERS];
static int g_nnum;
static uint32_t g_next_id = 1;
static HuntView g_view;  // this frame's (huntaids_view)
static bool g_view_any, g_touched;  // (g_touched: something of a quest is remembered, or a timer is ours)
static int g_log = -1;

// ---- the quest and the monsters
static bool in_quest(void) {
    uint32_t gc = LD32(G_GAMECONTROL), sq = LD32(G_SQUEST);
    return sane(gc) && LD8(gc + 0x4c) == 0xff && sane(sq) && sane(LD32(sq + 0xa4));
}

static bool hunted_alone(void) {
    uint32_t sp = LD32(G_SPLAYER), sel = LD32(G_SELECTOR);
    int n = 0;
    for (int i = 0; sane(sp) && i < 4; i++) n += sane(LD32(sp + 0x1c + 4u * (uint32_t)i));
    uint32_t joined = sane(sel) ? LD32(sel + 0xd750) : 0;
    return n <= 1 && joined <= 1;
}

static bool large(uint32_t en) { return LD8(en + EN_SMALL) == 0 && LD8(en + EN_TAKES_PART) != 0; }

// The zone of the map the local hunter is in (+0x1054, a monster's too: each zone has its own coordinates, so a number
// is only placed for a monster in the hunter's zone); -1 none
static int hunter_zone(void) {
    uint32_t gc = LD32(G_GAMECONTROL), sp = LD32(G_SPLAYER);
    uint32_t slot = sane(gc) ? LD32(gc + 0x30) : 9, pl = sane(sp) && slot < 4 ? LD32(sp + 0x1c + 4u * slot) : 0;
    return sane(pl) ? LD8(pl + EN_ZONE) : -1;
}

static bool down(uint32_t en, uint32_t info) {
    uint32_t st = LD8(en + EN_STATE);
    return (int32_t)LD32(info + ST_HP) <= 0 || st == 0xb || st == 0xe || (st == 0xc && LD8(en + EN_STATE + 1) == 0xff);
}

// The body's middle: the root joint (the pelvis) when the skeleton reads as measured, else 100 units above the feet
static void body_middle(uint32_t en, uint32_t info, float out[3]) {
    out[0] = LDF(info + ST_PLACE), out[1] = LDF(info + ST_PLACE + 4) + 100.0f, out[2] = LDF(info + ST_PLACE + 8);
    uint32_t n = LD32(en + EN_JOINTS), arr = LD32(en + EN_JOINTS + 4);
    if (n < 3 || n > 512 || !sane(arr)) return;
    for (uint32_t i = 0; i < n && i < 8; i++) {
        uint32_t j = arr + i * 0xa0u;
        if (LD32(j) < 0x00200000u || LD32(j) >= 0x02347000u) return;  // (not a joint)
        if (LD8(j + 0x91) != 0xff) continue;
        float p[3] = {LDF(j + 0x40), LDF(j + 0x44), LDF(j + 0x48)};
        if (isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]) && LDF(j + 0x4c) == 1.0f &&
            fabsf(p[0] - out[0]) + fabsf(p[2] - out[2]) < 3000.0f)
            out[0] = p[0], out[1] = p[1], out[2] = p[2];
        return;
    }
}

static void add_number(int value, const float p[3], uint64_t now) {
    if (g_nnum == HUNT_NUMBERS) memmove(g_num, g_num + 1, sizeof g_num[0] * (HUNT_NUMBERS - 1)), g_nnum--;
    g_num[g_nnum].p[0] = p[0], g_num[g_nnum].p[1] = p[1], g_num[g_nnum].p[2] = p[2];
    g_num[g_nnum].value = value, g_num[g_nnum].id = g_next_id++, g_num[g_nnum].born = now;
    g_nnum++;
}

// The mark's timer: ours kept (on), or ours taken back (off)
static void mark(int k, uint32_t en, uint32_t info, bool on) {
    float t = LDF(info + ST_MARK);
    if (on) {
        if (!(t < KEEP_LOW) || (LD8(en + EN_NO_MARK) & 4) || down(en, info)) return;
        STF(info + ST_MARK, KEEP_HIGH);
        if (!g_slot[k].marked && g_log) LOG("[hunt] monster %08x marked on the map (its timer was %.0f)\n", en, t);
        g_slot[k].marked = true;
    } else if (g_slot[k].marked) {
        if (t > 0 && t <= KEEP_HIGH) STF(info + ST_MARK, 0.0f);
        g_slot[k].marked = false;
        if (g_log) LOG("[hunt] monster %08x: our mark taken back (its timer was %.0f)\n", en, t);
    }
}

// Every mark of ours taken back (the option turned off, a party): the slots' monsters, if they are still there
static void unmark_all(void) {
    uint32_t sen = LD32(G_SENEMY);
    for (int k = 0; k < SLOTS; k++) {
        if (!g_slot[k].marked) continue;
        uint32_t en = sane(sen) ? LD32(sen + 0x1c + 4u * (uint32_t)k) : 0, info = sane(en) ? LD32(en + EN_INFO) : 0;
        if (en == g_slot[k].en && sane(info)) mark(k, en, info, false);
        g_slot[k].marked = false;
    }
}

static void forget(void) {
    memset(g_slot, 0, sizeof g_slot);
    g_nnum = 0;
    g_touched = false;
}

// p (world) on the game's picture this frame: x, y 0..1 from its left and top; false behind the camera
static bool project(const float v[16], const float pr[16], const float p[3], float *x, float *y) {
    float e[4], c[4];
    for (int j = 0; j < 4; j++) e[j] = p[0] * v[j] + p[1] * v[4 + j] + p[2] * v[8 + j] + v[12 + j];
    for (int j = 0; j < 4; j++) c[j] = e[0] * pr[j] + e[1] * pr[4 + j] + e[2] * pr[8 + j] + e[3] * pr[12 + j];
    if (!(c[3] > 1.0f)) return false;
    *x = (c[0] / c[3] + 1.0f) * 0.5f, *y = (1.0f - c[1] / c[3]) * 0.5f;
    return isfinite(*x) && isfinite(*y);
}

static void step(uint64_t now) {
    bool map = config_live_monsters_on_map(), numbers = config_live_damage_numbers(), bars = config_live_monster_bars();
    g_view_any = false;
    if (!map && !numbers && !bars) {
        if (g_touched) {  // (all turned off: our marks back, the rest forgotten, once)
            unmark_all();
            forget();
        }
        return;
    }
    if (!in_quest()) {
        if (g_touched) forget();
        return;
    }
    g_touched = true;
    if (g_log < 0) g_log = getenv("MHGU_HUNT_LOG") && atoi(getenv("MHGU_HUNT_LOG")) > 0;
    bool alone = map && hunted_alone();
    int zone = numbers || bars ? hunter_zone() : -1;
    uint32_t sen = LD32(G_SENEMY);
    HuntView *v = &g_view;
    v->nnum = v->nbar = 0;
    for (int k = 0; sane(sen) && k < SLOTS; k++) {
        uint32_t en = LD32(sen + 0x1c + 4u * (uint32_t)k), info = sane(en) ? LD32(en + EN_INFO) : 0;
        if (!sane(info) || !large(en)) {
            g_slot[k].en = 0, g_slot[k].marked = false;  // (a monster gone takes its timer with it)
            continue;
        }
        if (g_slot[k].en != en) {  // (another monster in this slot: nothing of the one before)
            g_slot[k].en = en, g_slot[k].hp = (int32_t)LD32(info + ST_HP), g_slot[k].marked = false;
            memcpy(g_slot[k].hit, G2H(info + ST_HIT), sizeof g_slot[k].hit);
        }
        mark(k, en, info, alone);
        int hp = (int32_t)LD32(info + ST_HP), most = (int32_t)LD32(info + ST_HP_MAX);
        if (numbers && hp < g_slot[k].hp && g_slot[k].hp - hp <= most && zone >= 0 && LD8(en + EN_ZONE) == zone) {
            float hit[3], at[3];
            memcpy(hit, G2H(info + ST_HIT), sizeof hit);
            body_middle(en, info, at);
            bool fresh = memcmp(hit, g_slot[k].hit, sizeof hit) && isfinite(hit[0]) && isfinite(hit[1]) && isfinite(hit[2]) &&
                         fabsf(hit[0] - at[0]) + fabsf(hit[1] - at[1]) + fabsf(hit[2] - at[2]) < 2500.0f;
            if (fresh) memcpy(at, hit, sizeof at), memcpy(g_slot[k].hit, hit, sizeof hit);
            add_number(g_slot[k].hp - hp, at, now);
            if (g_log)
                LOG("[hunt] monster %08x: -%d (%d of %d left) at the %s %.0f %.0f %.0f\n", en, g_slot[k].hp - hp, hp, most,
                    fresh ? "hit" : "body", at[0], at[1], at[2]);
        }
        g_slot[k].hp = hp;
        if (bars && v->nbar < HUNT_BARS && most > 0 && !down(en, info)) {
            HuntBar *b = &v->bar[v->nbar++];
            b->hp = hp, b->max = most, b->here = zone >= 0 && LD8(en + EN_ZONE) == zone;
        }
    }
    while (g_nnum && now >= g_num[0].born + HUNT_NUMBER_NS) memmove(g_num, g_num + 1, sizeof g_num[0] * (size_t)--g_nnum);
    if (!numbers) g_nnum = 0;
    uint32_t cam = LD32(G_SCAMERA);
    if (g_nnum && sane(cam)) {
        float view[16], proj[16];
        memcpy(view, G2H(cam + CAM_VIEW), sizeof view);
        memcpy(proj, G2H(cam + CAM_PROJ), sizeof proj);
        for (int i = 0; i < g_nnum; i++) {
            HuntNumber *n = &v->num[v->nnum++];
            n->shown = project(view, proj, g_num[i].p, &n->x, &n->y);
            n->value = g_num[i].value, n->id = g_num[i].id, n->born_ns = g_num[i].born;
        }
    }
    v->number_size = config_live_number_size(), v->number_colour = config_live_number_colour();
    g_view_any = v->nnum || v->nbar;
}

void huntaids_frame(void) {
    static int cost = -1;
    if (cost < 0) cost = getenv("MHGU_HUNT_COST") && atoi(getenv("MHGU_HUNT_COST")) > 0;
    uint64_t t0 = plat_now_ns();
    step(t0);
    if (cost) {
        static uint64_t sum, most, since, frames;
        uint64_t t = plat_now_ns() - t0;
        sum += t, frames++, most = t > most ? t : most;
        if (!since) since = t0;
        if (t0 - since >= 10000000000ull) {
            LOG("[hunt] cost: %.2f us a frame on average, %.1f at most (%llu frames, %d numbers, %d bars now)\n",
                sum / 1e3 / (double)frames, most / 1e3, (unsigned long long)frames, g_view.nnum, g_view.nbar);
            sum = most = frames = 0, since = t0;
        }
    }
}

bool huntaids_view(HuntView *out) {
    if (!g_view_any) return false;
    *out = g_view;
    return true;
}

bool huntaids_any(void) { return g_view_any; }
