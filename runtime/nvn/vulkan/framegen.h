// Frame generation ([Graphics] FrameGen, off by default): runtime/nvn/vulkan/framegen.c. One made-up picture between
// each two of the game's, for a game held to half the display's rate or less (30 FPS on 60 Hz: 60 shown). The hook is in
// device.c backend_present ("FRAMEGEN:"); with the setting off nothing here runs and the present path is as before.
#pragma once
#include "vk.h"

// The setting on (or MHGU_FRAMEGEN=1 for tests): read every frame, a change takes effect at the next one
bool vk_framegen_enabled(void);
// (device.c backend_present, under vk_lock, outside any render pass, after the game's picture `real` is final) Records
// the generation into vk_cmd(). n: the vsyncs a game frame takes on the display (present_pacing.c; 1: not paced to a
// whole fraction of it, nothing made). Returns the made picture, to present before `real`, and in *follow the vsyncs
// `real` comes after it; NULL when none is made for this frame (warming up, a menu, a scene it should not touch...).
TexBackend *vk_framegen_frame(TexBackend *real, int n, int *follow);
// The made picture's present is done (device.c), for the per-minute line
void vk_framegen_presented(void);
