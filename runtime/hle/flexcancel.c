// [Comforts] FlexCancel (1.0.1, "Move to skip the flex"; MH4U for Android has the same option and name): after a
// drink or a meal, pushing the stick or pressing A, B, X or Y ends the hunter's flex at once, as in the newer games, and
// that same press walks, rolls, draws the weapon or uses an item. Off (the default): the whole flex, as the game does it.
//
// The game's path (PC test runs with MHGU_LOG_MOVES=1, and the lifted code):
// - an item that is drunk or eaten (Y) starts the common action 0001:085 (potions, nutrients, juices, drugs, pills,
//   armorskins, hot and cool drinks, psychoserum, herbal medicine, max and ancient potions, energy drinks, lifepowder,
//   Tanzia chips...) or 0001:086 (a steak); as its motion ends it sets 0001:087, the flex.
// - the common actions' update (0x4cbfcc, the hunter's vtable +0x318, from 0x482da8) runs 0x4d5f40(hunter, kind) for
//   0001:087 with kind 0 (for 0001:110, 312, 340 and 381 with kinds 1 to 4: other poses, left as they are). Its phase is
//   the byte at hunter +5: 0 in its first update, which starts the pose's motion and gives the item (0x4fafc4: the
//   pouch's count and the item's effect), then 1 until the motion ends, when 0x4b6e94(hunter, the rest mode
//   *(u8 *)(hunter + 0x2505), 4, 0, and a fifth argument on the stack: 0) sets that mode's neutral action (0001:000
//   standing with the weapon away); the hunter's update then runs the action update again (0x49beac), the neutral's
//   first update.
// - each update of a hunter (0x49b8e8) reads the commands of its action's table (0x4e9d54(hunter)) before the action's
//   own update (0x482da8): walking (0002:001), the roll (0001:023), drawing, using an item... The flex has no table
//   (9999); standing has the common table 0, and its commands answer only once its first update has run (the neutral
//   set and its commands read at once: nothing). The hunter's input, as the game reads it each update (0x4a79cc, empty
//   while a menu has the pad): +0x13fc the buttons held (A 0x100, B 0x80, X 0x20, Y 0x10, L 0x400; the stick pushed
//   up, down, left, right 0x10000, 0x20000, 0x40000, 0x80000; the right stick 0xf00000), +0x1404 those pressed in that
//   update.
// Hooked here: 0x4d5f40, only to note that this console's hunter is in the flex (kind 0) past its first update; and
// 0x4e9d54: with the option on, when that hunter's flex has the stick pushed, or A, B, X or Y pressed in this update (not
// with L held: the item bar), the flex ends as it ends by itself (0x4b6e94, then the neutral's first update: 0x482da8)
// before this update's commands are read, so the same press walks, rolls, draws or uses an item in this same update.
// Standing still, the flex plays whole. The item was given in the flex's first update, before any of this. Only this
// console's hunter (+0x13cc bit 0, the game's own test); what goes on the network is the game's own, as always.
// `[flex]` in the log for each flex ended.
#include "../hle.h"
#include "flexcancel.h"

#define FN_FLEX          0x4d5f40u  // (hunter, kind): the flex's update (0001:087: kind 0)
#define FN_COMMANDS      0x4e9d54u  // (hunter): the action's commands, read before the action's update
#define FN_TO_NEUTRAL    0x4b6e94u  // (hunter, rest mode, 4, 0; [sp] 0): the rest mode's neutral action (the flex's end)
#define FN_ACTION_UPDATE 0x482da8u  // (hunter): the action's update (the hunter's update calls it at 0x49be90 / 0x49beac)
#define PL_PHASE         0x05u      // u8: the action's phase (the flex: 0 its first update, then 1)
#define PL_REST_MODE     0x2505u    // u8: the mode the hunter's actions go back to
#define PL_ACT_ST        0x2506u    // u16, u16: the action (act_st, act_no)
#define PL_ACT_NO        0x250au
#define PL_LOCAL         0x13ccu    // bit 0: this console's hunter (0x47bcd0)
#define PL_HELD          0x13fcu    // u32: the buttons held, as the hunter's commands read them
#define PL_PRESSED       0x1404u    // u32: those pressed in this update
#define ACT_COMMON       0x0001u
#define ACT_FLEX         87u
#define FLEX_KIND        0u         // (0x4d5f40's kind for 0001:087)
#define BTN_Y            0x10u
#define BTN_X            0x20u
#define BTN_B            0x80u
#define BTN_A            0x100u
#define BTN_L            0x400u
#define STICK_DIRS       0xf0000u   // (the left stick pushed up, down, left or right; not the right stick's 0xf00000)
#define FACE_BUTTONS     (BTN_A | BTN_B | BTN_X | BTN_Y)

static uint32_t g_flex;     // this console's hunter, in the flex past its first update (noted by its last update)
static unsigned g_updates;  // the flex's updates so far (for the log)

static bool sane(uint32_t a) { return a >= 0x08000000u && a < 0xF0000000u && !(a & 3); }

static bool in_flex(uint32_t h) {
    return LD16(h + PL_ACT_ST) == ACT_COMMON && LD16(h + PL_ACT_NO) == ACT_FLEX && LD8(h + PL_PHASE) == 1 &&
           (LD32(h + PL_LOCAL) & 1);
}

static void flex_update(Cpu *c) {
    uint32_t h = c->r[0], kind = c->r[1];
    if (!config_live_flex_cancel() || kind != FLEX_KIND || !sane(h)) {
        fnhook_call_original(c, FN_FLEX);
        return;
    }
    if (LD8(h + PL_PHASE) == 0) g_updates = 0;
    fnhook_call_original(c, FN_FLEX);
    g_updates++;
    g_flex = in_flex(h) ? h : 0;  // (its first update gave the item: from the next one it may end)
}

// The flex's end as it ends by itself: its call of 0x4b6e94 when the pose's motion is over, then the action update the
// hunter's update runs again after an action changed: the neutral's first update (the registers kept)
static void end_flex(Cpu *c, uint32_t h) {
    uint32_t saved[16];
    for (int i = 0; i < 16; i++) saved[i] = c->r[i];
    uint32_t sp = saved[13] - 8;  // (the fifth argument, on the stack, kept 8-byte aligned)
    ST32(sp, 0);
    c->r[13] = sp;
    c->r[0] = h, c->r[1] = LD8(h + PL_REST_MODE), c->r[2] = 4, c->r[3] = 0;
    guest_call(c, FN_TO_NEUTRAL);
    c->r[13] = saved[13];
    c->r[0] = h;
    guest_call(c, FN_ACTION_UPDATE);
    for (int i = 0; i < 16; i++) c->r[i] = saved[i];
}

static void commands(Cpu *c) {
    uint32_t h = c->r[0];
    if (g_flex && h == g_flex) {
        g_flex = 0;  // (noted again by the flex's next update while it goes on)
        uint32_t held = LD32(h + PL_HELD), pressed = LD32(h + PL_PRESSED);
        const char *why = NULL;
        if (held & STICK_DIRS) why = "the stick";
        else if (!(held & BTN_L) && (pressed & FACE_BUTTONS))
            why = pressed & BTN_B ? "B" : pressed & BTN_A ? "A" : pressed & BTN_X ? "X" : "Y";
        if (why && config_live_flex_cancel() && in_flex(h)) {
            unsigned mode = LD8(h + PL_REST_MODE), updates = g_updates;
            end_flex(c, h);
            uint32_t standing = (uint32_t)LD16(h + PL_ACT_ST) << 16 | LD16(h + PL_ACT_NO);
            fnhook_call_original(c, FN_COMMANDS);
            LOG("[flex] %llu: %s ended the flex after %u of its updates (rest mode %u: %04x:%03u, then %04x:%03u)\n",
                (unsigned long long)g_present_count, why, updates, mode, standing >> 16, standing & 0xffff,
                LD16(h + PL_ACT_ST), LD16(h + PL_ACT_NO));
            return;
        }
    }
    fnhook_call_original(c, FN_COMMANDS);
}

void flexcancel_init(void) {
    fnhook_set(FN_FLEX, hle_register("flexcancel:flex", flex_update));
    fnhook_set(FN_COMMANDS, hle_register("flexcancel:commands", commands));
}
