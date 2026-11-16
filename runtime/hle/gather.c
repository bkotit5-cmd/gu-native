// [Gathering] GatherAll (1.0.1, the owner's idea): one gather at a gathering point (herbs, mushrooms, honey, bugs, bones,
// ore...) or one carve gives everything it had left at once, through the game's own steps for each one, instead of A again
// for each with its animation. Off: the game's way, one item a gather or a carve.
//
// The points are cPop (DTI 0x1a89db8, vtable 0x19523e0, 0x80 bytes), in the pop manager's list (the manager: *(*0x1a38d94),
// the 0x4e07f8 literal's global; +0x5c the first point, +0x2c the next): +0x04 its place (x, y, z), +0x38 its item
// table ({u8 weight, u8 count, u16 item}..., weight 0xff ends it), +0x4c its index (u16), +0x4e gathers left (u16;
// 0xffff: endless), +0x50 sure gathers left (u16), +0x52 / +0x54 a fixed item and count instead of the table, +0x60 its
// kind (3: a carve; the others are the gathering points). A monster that dies gets one (a free entry of the same list) at
// its body with its carves: carving is a gather of that point (the action 0001:083), through the same steps below.
// A hunter's gather action (0x4d4544, the action 0001:077 in MHGU_LOG_MOVES; mining 0001:082) runs, as the item comes,
// the hunter's step 0x4e07c4(hunter, a, tool) once (the Prowler's action, from 0x140cf1c, too):
//   0x51b228(manager, *(s16 *)(hunter + 0x32ae) the point's index, &point): the point if 0x519244 says it can be gathered
//     (+0x4e not 0, +0x5c 0, +0x70 bit 2 clear), then 0x51b290(manager, point): the count, as the game counts it: a sure
//     gather first (+0x50), else the chance that the point ends here (0x51b9cc: the hunter's gathering skills against a
//     roll of 32), else one less; 1 a gather (2: the last), 0 or 3 none (the game's "nothing here" message);
//   0x519318(point, &item, &count): the fixed item, or a roll of the table (0x51b37c);
//   0x4e04d4(hunter, item, count, tool, 0): the skills' extra one for some items; with a tool (a pickaxe or a bug net:
//     the action's tool flag), 0x4e02d4: its use counted (+0x32c1.. by the tool, *(u16 *)(hunter + 0x2520)) and its
//     chance to break, which takes one from the pouch with the game's message; then the hunter's vtable +0x364
//     (0x501784 for every hunter class) gives the item with its message: < 3 taken, 3 none (it can't carry more of it),
//     5 the pouch full (the game's window to choose, action 0x270f).
// Returns 1 when the point gave an item.
//
// Hooked here: 0x4e07c4 and 0x501784 (only to see what the give answered). With the option on, after the game's own
// step gave an item that went into the pouch, the same step runs again on the same point, as the game would on the next
// gather, while the point has gathers left: the same counting, skills, rolls, tool uses and breaks, messages and sounds.
// It stops when the point is spent, when the tool broke and the pouch has no other one of it, or when an item didn't go
// into the pouch: the pouch full (that one is in the game's window, the rest stays in the point) or it can't carry more
// of that item (the point gets that gather back: nothing lost, nothing spent). Endless points (0xffff) are left as the
// game has them. Only the local hunter's gathers, the points are each player's own (nothing of this goes on the network).
// Fishing doesn't go through 0x4e07c4 (0x4d8588 gives the fish): it doesn't change.
// Tests (PC, quest 101, MHGU_GATHER_LOG=1: each give's item, count and answer in the [gather] line): a point moved next to
// the hunter (its +0x04 written) and A, as out/gather/pc/poptest.py does.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../config.h"
#include "../hle.h"

#define FN_GATHER_STEP  0x4e07c4u  // (hunter, a, tool): one gather's item from the hunter's point; 1 if it gave one
#define FN_GIVE         0x501784u  // (hunter, item, flags, count, 0): hunter vtable +0x364, an item into the pouch
#define FN_POINT_READY  0x519244u  // (point) -> 1 while it can be gathered
#define FN_POUCH_COUNT  0x48813cu  // (hunter, item) -> how many of it the pouch holds (0 for another player)
#define G_POPS_GOT      0x1a38d94u  // * * the pop manager (0x4e07c4's literal: 0x4e07f8 + 0x155859c)
#define G_GAME_CONTROL  0x1a8ca48u  // * sGameControl: +0x30 the local player's slot (as plpos: host_input.c)
#define G_PLAYERS       0x1a85b58u  // * sPlayer: +0x1c the players by slot
#define MGR_FIRST       0x5cu
#define PT_NEXT         0x2cu
#define PT_INDEX        0x4cu
#define PT_LEFT         0x4eu
#define PT_SURE         0x50u
#define PL_POINT        0x32aeu  // s16: the point being gathered (-1 none)
#define PL_TOOL         0x2520u  // u16: the tool in use (an item id)
#define PL_FLAGS        0x2500u  // u32: bit 4 set by 0x4e04d4 when an item asks the hunter for something (its action 0x270f)
#define PL_ASKS         0x10u
#define LEFT_ENDLESS    0xffffu
#define GIVE_NONE       0xffffffffu
#define GIVE_NO_MORE    3u       // it can't carry more of that item
#define MAX_EXTRA       40       // (a point has a handful: anything past this is not a point's)

static Cpu *g_loop;     // the gathers running here, on this thread
static uint32_t g_give;  // what the last give answered while they ran
static char g_trace[512];  // (MHGU_GATHER_LOG=1) each give: item x count = answer
static int g_trace_on = -1;

static bool sane(uint32_t a) { return a >= 0x08000000u && a < 0xF0000000u && !(a & 3); }

static uint32_t local_hunter(void) {
    uint32_t gc = LD32(G_GAME_CONTROL), sp = LD32(G_PLAYERS);
    if (!sane(gc) || !sane(sp)) return 0;
    uint32_t slot = LD32(gc + 0x30);
    return slot < 4 ? LD32(sp + 0x1c + 4u * slot) : 0;
}

// A guest function, its registers kept; orig: the hooked function itself (not this file's hook)
static uint32_t call(Cpu *c, uint32_t fn, uint32_t r0, uint32_t r1, uint32_t r2, bool orig) {
    uint32_t saved[16];
    for (int i = 0; i < 16; i++) saved[i] = c->r[i];
    c->r[0] = r0, c->r[1] = r1, c->r[2] = r2;
    if (orig) fnhook_call_original(c, fn);
    else guest_call(c, fn);
    uint32_t v = c->r[0];
    for (int i = 0; i < 16; i++) c->r[i] = saved[i];
    return v;
}

// The hunter's point, as 0x51b228 finds it
static uint32_t point_of(uint32_t hunter) {
    int16_t index = (int16_t)LD16(hunter + PL_POINT);
    uint32_t got = LD32(G_POPS_GOT), mgr = got && !(got & 3) ? LD32(got) : 0;  // (got: a global in the image)
    if (index < 0 || !sane(mgr)) return 0;
    uint32_t p = LD32(mgr + MGR_FIRST);
    for (int n = 0; sane(p) && n < 4096; n++, p = LD32(p + PT_NEXT))
        if (LD16(p + PT_INDEX) == (uint16_t)index) return p;
    return 0;
}

static void give(Cpu *c) {
    uint32_t item = c->r[1], count = c->r[3];
    fnhook_call_original(c, FN_GIVE);
    if (g_loop != c) return;
    g_give = c->r[0];
    size_t n = strlen(g_trace);
    if (g_trace_on > 0 && n < sizeof g_trace - 32)
        snprintf(g_trace + n, sizeof g_trace - n, " %x x%u=%u", item & 0xffff, count & 0xff, g_give);
}

static void gather_step(Cpu *c) {
    uint32_t hunter = c->r[0], a = c->r[1], tool = c->r[2];
    if (g_loop || !config_live_gather_all() || !sane(hunter) || hunter != local_hunter()) {
        fnhook_call_original(c, FN_GATHER_STEP);
        return;
    }
    uint32_t asked = LD32(hunter + PL_FLAGS) & PL_ASKS;
    if (g_trace_on < 0) g_trace_on = getenv("MHGU_GATHER_LOG") && atoi(getenv("MHGU_GATHER_LOG")) > 0;
    g_trace[0] = 0;
    g_loop = c, g_give = GIVE_NONE;
    fnhook_call_original(c, FN_GATHER_STEP);
    uint32_t got = c->r[0];
    int more = 0;
    const char *why = "";
    bool asks = (LD32(hunter + PL_FLAGS) & PL_ASKS) && !asked;
    uint32_t point = got == 1 && g_give < GIVE_NO_MORE && !asks ? point_of(hunter) : 0;
    if (!point)
        why = got != 1                  ? " (none given)"
              : g_give >= GIVE_NO_MORE ? " (it didn't go into the pouch)"
              : asks                   ? " (the item asks the hunter)"
                                       : " (no point)";
    else if (LD16(point + PT_LEFT) == LEFT_ENDLESS) why = " (an endless point: left as it is)", point = 0;
    for (int i = 0; point && i < MAX_EXTRA; i++) {
        if (call(c, FN_POINT_READY, point, 0, 0, false) != 1) { why = ": the point is spent"; break; }
        uint16_t item = LD16(hunter + PL_TOOL);
        if (tool && call(c, FN_POUCH_COUNT, hunter, item, 0, false) < 1) { why = ": no tool left"; break; }
        uint16_t left = LD16(point + PT_LEFT), sure = LD16(point + PT_SURE);
        g_give = GIVE_NONE;
        if (call(c, FN_GATHER_STEP, hunter, a, tool, true) != 1) { why = ": the point gave nothing"; break; }
        if (g_give == GIVE_NO_MORE) {  // (it can't carry more of that item: the point keeps that gather)
            ST16(point + PT_LEFT, left), ST16(point + PT_SURE, sure);
            why = ": it can't carry more of an item (the point keeps the rest)";
            break;
        }
        if (g_give == GIVE_NONE) { why = ": a gather gave no item"; break; }
        more++;
        if (g_give > GIVE_NO_MORE) { why = ": the pouch is full (the game's window; the point keeps the rest)"; break; }
        if ((LD32(hunter + PL_FLAGS) & PL_ASKS) && !asked) { why = ": an item asks the hunter"; break; }
    }
    g_loop = NULL;
    c->r[0] = got;
    if (more || *why)
        LOG("[gather] %d more from the point%s%s%s%s\n", more, why, tool ? " (with a tool)" : "", *g_trace ? "; gives:" : "",
            g_trace);
}

void gather_init(void) {
    fnhook_set(FN_GATHER_STEP, hle_register("gather:step", gather_step));
    fnhook_set(FN_GIVE, hle_register("gather:give", give));
}
