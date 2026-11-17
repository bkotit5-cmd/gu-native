// Hunting aids (1.0.1, the owner's; MH4U for Android has the first two with the same names and texts), each off unless
// the player turns it on (Graphics & Audio > Screen and HUD; [Hunting Aids] in the settings file):
//   - Monsters always on the map: in a quest hunted alone, the large monsters show on the game's maps from the start, as
//     if marked (the game draws its own icons);
//   - Damage numbers: each hit on a large monster shows its damage as a number over the monster, which rises and fades
//     in a second; their size and colour;
//   - Monster health bars: a bar a large monster at the top of the screen in a quest, with its health (MH4U for
//     Android's look), moved and sized in the HUD editors.
// The game's side (runtime/hle/huntaids.c) reads and writes the game's memory once a frame, on the game's update thread,
// after the game's update: no hook of the game's code, the game library is the same. The drawing (runtime/hunt_overlay.cpp)
// is the overlay's, over the game's picture and under the touch controls: frame generation and the upscalers never touch it.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HUNT_NUMBERS 24      // damage numbers kept at once (the oldest goes first)
#define HUNT_BARS 5          // monsters with a bar at once
#define HUNT_NUMBER_NS 1000000000ull  // how long a number shows (it rises and fades meanwhile)

typedef struct {
    float x, y;        // where its hit is on the game's picture this frame: 0..1 from its left and top edges
    bool shown;        // in front of the camera (else not drawn this frame)
    int value;         // the damage
    uint32_t id;       // its own number (a steady sideways offset for numbers at one place)
    uint64_t born_ns;  // when it was dealt (plat_now_ns)
} HuntNumber;

typedef struct {
    int hp, max;
    bool here;         // in the hunter's zone of the map (else drawn fainter, as MH4U's)
} HuntBar;

typedef struct {
    int nnum, nbar;
    HuntNumber num[HUNT_NUMBERS];
    HuntBar bar[HUNT_BARS];
    int number_size, number_colour;  // [Hunting Aids] DamageNumberSize (0 Medium, 1 Small, 2 Large), DamageNumberColour
} HuntView;

// (game's update thread, once a frame, after the game's update: runtime/overlay.cpp overlay_frame) both sides' step:
// the marks kept, the numbers made and placed for this frame, the bars read
void huntaids_frame(void);
// (game thread, the overlay's frame) what to draw now; false: nothing (every aid off, or none to show). huntaids_any:
// only whether there is anything
bool huntaids_view(HuntView *out);
bool huntaids_any(void);
// (game thread, inside the overlay's ImGui frame: runtime/hunt_overlay.cpp) the numbers and the bars, display pixels; and
// (MHGU_PERF_OVERLAY_REUSE) whether the frame built last drew just what they would draw now (bars alone that didn't
// change, or nothing: a number moves every frame)
void hunt_overlay_draw(float display_w, float display_h);
bool hunt_overlay_unchanged(float display_w, float display_h);
// (any thread) where the monster health bars were drawn last (display pixels: x0, y0, x1, y1), in a quest or as their
// stand-in in an editor; false while they aren't drawn. The editors frame them by it, and move and size them
// (runtime/touch.cpp: the layout's monster_bars line), which touch_bars_place hands back (x, y: the bars' top middle in
// the screen's width and height, < 0 their own place; k: their size, 1 the default)
bool hunt_bars_rect(float r[4]);
void touch_bars_place(float *x, float *y, float *k);
// (tests: the control file's "huntaids <map> <numbers> <bars> [size] [colour]", runtime/host_input.c) the three aids, the
// numbers' size and colour from now on, as if the menu had saved them (not saved); config_live_hunt_* read them
void config_huntaids_test(int map, int numbers, int bars, int size, int colour);
int config_live_monsters_on_map(void);  // [Hunting Aids] MonstersOnMap
int config_live_damage_numbers(void);   // [Hunting Aids] DamageNumbers
int config_live_number_size(void);      // [Hunting Aids] DamageNumberSize: 0 Medium, 1 Small, 2 Large
int config_live_number_colour(void);    // [Hunting Aids] DamageNumberColour: 0 White, 1 Yellow, 2 Red, 3 Light blue, 4 Green
int config_live_monster_bars(void);     // [Hunting Aids] MonsterHealthBars

#ifdef __cplusplus
}
#endif
