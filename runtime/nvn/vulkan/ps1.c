// The PS1 filter's 15-bit colour ([Graphics] PS1Filter, 1.0.1.1) where neither FSR nor plain scaling runs (g_fsr_on false;
// where one does, fsr.c hud_start dithers there): the frame's 3D picture is dithered once, in place, at the start of the
// HUD, so the HUD and the text are drawn over it as they are. The start is found as fsr.c finds it (its comment at the
// top):
//  - H, the full-screen image, is learnt at the presents: the first texture the frame's last draw (the one into the
//    presented texture) sampled.
//  - The start of the HUD: the first draw into H that is neither a post pass (a draw of <= 6 vertices that samples a
//    render target) nor a 3D effect drawn with the scene (tested against a depth target of the render size, or sampling
//    the picture: H, or a render target of its size or half of it); or else the draw into the presented texture that
//    samples H (a frame with no HUD).
// There, before that draw, exec.c vk_ps1_dither. Menus that draw 3D models after their first 2D draw (Character Select):
// the models come after the dither (fsr.c waits for them; here they keep the picture's own colours).
// Off, nothing of this runs: exec.c calls vk_ps1_draw only while it is on, and vk_ps1_present only learns while it is.
#include <stdlib.h>

#include "../../config.h"
#include "fsr.h"
#include "vk.h"

// The PS1 filter's parts: all of them; MHGU_PS1_PARTS=<mask of PS1_*> (read once) only those, for tests
bool vk_ps1_part(int parts) {
    static int mask = -1;
    if (mask < 0) {
        const char *e = getenv("MHGU_PS1_PARTS");
        mask = e ? (int)strtol(e, NULL, 0) : PS1_ALL;
        if (e) LOG("[ps1] MHGU_PS1_PARTS=%#x\n", mask);
    }
    return mask & parts;
}

// ... while it is on
bool vk_ps1_on(int parts) { return vk_ps1_part(parts) && config_live_ps1_filter(); }

static NvnTexture *g_h;                     // the full-screen image (learnt at the presents; compared, read only when drawn)
static NvnTexture *last_target, *last_src;  // the frame's last draw: its target, the first texture it sampled
static bool seen, done;                     // a draw seen since the present; the picture dithered this frame

static bool is_rt(const NvnTexture *t) { return t && t->backend && ((TexBackend *)t->backend)->gpu_owned; }
static bool presented_texture(const NvnTexture *t) { return t && t->backend && ((TexBackend *)t->backend)->presented; }

// A render target of the render size or half of it: the picture, or a copy of it (fsr.c picture_sized)
static bool picture_sized(const NvnTexture *t) {
    for (uint32_t k = 1; k <= 2; k++) {
        int dw = (int)t->width - (int)(g_render_size[0] / k), dh = (int)t->height - (int)(g_render_size[1] / k);
        if (dw >= -1 && dw <= 1 && dh >= -1 && dh <= 1) return true;
    }
    return false;
}

static void dither(const char *why) {
    done = true;
    TexBackend *b = vk_tex(g_h, true);
    vk_ps1_dither(b);
    static int nlog;
    if (nlog++ < 1) LOG("[ps1] 15-bit colour: the 3D picture (%ux%u) dithered at the start of the HUD (%s)\n", g_h->width,
                        g_h->height, why);
}

void vk_ps1_draw(const FsrDraw *d) {
    seen = true;
    last_target = d->color0;
    last_src = d->ntex ? d->tex[0] : NULL;
    if (done || !g_h || !d->color0) return;
    if (d->color0 == g_h) {
        bool post = d->count <= 6, reads_rt = false, reads_picture = false;
        for (int i = 0; i < d->ntex; i++) {
            NvnTexture *t = d->tex[i];
            if (t == g_h) { reads_picture = true; continue; }
            if (!is_rt(t)) continue;
            reads_rt = true;
            reads_picture |= picture_sized(t);
        }
        if (post && reads_rt) return;  // (a post pass)
        bool depth_tested = d->depth_test && d->depth && d->depth->width == g_render_size[0] &&
                            d->depth->height == g_render_size[1];
        if (depth_tested || reads_picture) return;  // (a 3D effect drawn with the scene)
        dither("first HUD draw");
        return;
    }
    if (presented_texture(d->color0))
        for (int i = 0; i < d->ntex; i++)
            if (d->tex[i] == g_h) { dither("final draw, no HUD"); return; }
}

void vk_ps1_present(NvnTexture *t) {
    if (!seen) return;
    if (vk_ps1_on(PS1_COLOUR) && last_target == t && last_src && last_src != t && last_src->width == g_render_size[0] &&
        last_src->height == g_render_size[1] && last_src->backend &&
        !(((TexBackend *)last_src->backend)->aspect & VK_IMAGE_ASPECT_DEPTH_BIT)) {
        if (g_h != last_src) LOG("[ps1] full-screen image: %p %ux%u\n", (void *)last_src, last_src->width, last_src->height);
        g_h = last_src;
    }
    seen = done = false;
    last_target = last_src = NULL;
}
