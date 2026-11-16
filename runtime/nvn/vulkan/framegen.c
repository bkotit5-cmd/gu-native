// Frame generation ([Graphics] FrameGen, off by default; the project's own design and code). The game held to half the
// display's rate or less (30 FPS on a 60 Hz screen, present_pacing.c's cadence n >= 2): when a new picture B of the game's
// comes, a picture between it and the one before, A, is made and presented first, k = n / 2 vsyncs after A, and B
// n - k vsyncs after that (30 -> 60 shown at n = 2). B is shown a vsync later than without it: that is the input delay
// the setting's help speaks of. The game gives no motion vectors, so they are found from the two pictures alone:
//
//  1. luma pyramid: each picture's luma at 1/4, 1/8, 1/16 and 1/32 of its size (box averages), kept for the next pair
//     (the later picture's levels are the next pair's earlier ones);
//  2. a full search at 1/32, one vector per 64x64 pixels: every whole motion of up to 6 texels each way (192 pixels a game
//     frame), the motion split between the two pictures (A at c - floor(v / 2), B at c + v - floor(v / 2): what is at c in
//     the picture between), a 4x4 window;
//  3. at 1/16, 1/8 and 1/4 (a vector per 32, 16 and 16 pixels) each level takes the four vectors of the coarser one round
//     the block, their mix and no motion, the best a 3x3 step further, and at 1/4 below a texel by a parabola through
//     its neighbours' costs; a small cost for leaving the coarser level's mix keeps flat parts (sky, water) moving with
//     what is round them. (Steps of 2 between levels: a coarser vector is at most a texel off the finer level's answer;
//     with steps of 4, and the motion split in halves, the first design's vectors were wrong in half the blocks of a
//     plain 40-pixel pan);
//  4. a 3x3 median of the field (outliers out);
//  5. the picture: each pixel takes A at x - t v and B at x + (1 - t) v (v the field, bilinear; t = k / n), and their
//     average where they agree; where they disagree (something uncovered or covered) the one nearer A and B's blend at x
//     (a median of the three). Pixels the same in A and B, and every pixel inside a part of the HUD (touch.cpp's boxes for
//     this frame: the clock, the bars, the map, the item bar...) take B's own, unwarped, and a sample that would land in a
//     HUD box is not used: the HUD is never warped. Where the match is poor (a cut to another scene, smoke, a flash) B's
//     own pixel is taken: a scene cut shows B twice.
// Pass 5 also copies B into the history (the next pair's A), so nothing else reads the game's picture again.
// No picture is made, and the history starts again, while a menu window of the game's or its title is drawn, while the
// settings menu is open, and whenever the FPS limit is not a whole fraction of half the refresh rate or less.
//
// Cost: ten compute passes, the last at the output size (five texture reads and two writes a pixel), the others at a
// quarter of it and less; plus the made picture's own present. The per-minute line says it in GPU time.
//
//   MHGU_FRAMEGEN=1            on whatever the setting (tests); on a desktop also without paced presents (FIFO / mailbox
//                              as it is: the made picture and the game's back to back)
//   MHGU_FRAMEGEN_EVAL=<n>     (tests) n samples of quality: at the game's own rate, the picture between two frames two
//                              apart made and saved with the real one between them (shots/fgeval_<i>_{a,t,b,g}.png:
//                              earlier, truth, later, made); nothing extra is presented. MHGU_FRAMEGEN_EVAL_EVERY=<frames>
//                              between samples (20), MHGU_FRAMEGEN_EVAL_AT=<s>: from that many seconds after the first frame
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "framegen.h"
#include "shaders.h"

int touch_hud_frame_boxes(float (*out)[4], int max);  // (runtime/touch.cpp: this frame's HUD parts, on the 1280x720 canvas)
int touch_frame_menu(void);                           // (runtime/touch.cpp: bit 0 a menu window this frame, 1 the title)
bool overlay_menu_open(void);                         // (runtime/overlay.cpp: the settings menu)
uint32_t nvn_window_origin(void);
uint64_t plat_now_ns(void);
void vk_gpuprof_mark(const char *label);  // (exec.c: MHGU_CONTROL "gpuprof", each pass's time)

// ================================================================== shaders
#define FG_HEAD "#version 450\nlayout(local_size_x = 8, local_size_y = 8) in;\n"

// All four levels in one pass (on a Mali each dispatch costs its own time whatever its size): each 8x8 group makes 8x8
// texels of 1/4 (four bilinear taps of the picture each), then from its shared memory 4x4 of 1/8, 2x2 of 1/16 and the one
// texel of 1/32 that its 32x32 pixels make. 1/4's green is the HUD's mask: 1 where the texel's 4x4 pixels touch a HUD box
// of this frame (the made picture reads it there instead of testing every box at every pixel)
static const char *LEVELS_GLSL = FG_HEAD
    "layout(set = 0, binding = 0) uniform sampler2D src;\n"
    "layout(set = 0, binding = 1, rgba8) uniform writeonly image2D l1;\n"
    "layout(set = 0, binding = 2, rgba8) uniform writeonly image2D l2;\n"
    "layout(set = 0, binding = 3, rgba8) uniform writeonly image2D l3;\n"
    "layout(set = 0, binding = 4, rgba8) uniform writeonly image2D l4;\n"
    "layout(push_constant) uniform P { vec4 a; vec4 b; vec4 c; } pc;  // a: 1 / the picture's size, 1/4's size; b: 1/8's, 1/16's; c: 1/32's\n"
    "layout(set = 0, binding = 5) uniform H { vec4 n; vec4 hud[12]; } h;  // n.x: the boxes (pixels: x0, y0, x1, y1)\n"
    "shared float s[64];\n"
    "void main() {\n"
    "  ivec2 p = ivec2(gl_GlobalInvocationID.xy), l = ivec2(gl_LocalInvocationID.xy);\n"
    "  int i = l.y * 8 + l.x;\n"
    "  vec2 o = vec2(p) * 4.0;\n"
    "  vec4 t = texture(src, (o + vec2(1.0, 1.0)) * pc.a.xy) + texture(src, (o + vec2(3.0, 1.0)) * pc.a.xy)\n"
    "         + texture(src, (o + vec2(1.0, 3.0)) * pc.a.xy) + texture(src, (o + vec2(3.0, 3.0)) * pc.a.xy);\n"
    "  float v = dot(t.rgb * 0.25, vec3(0.299, 0.587, 0.114));\n"
    "  vec4 px = vec4(o, o + 4.0);\n"
    "  float hm = 0.0;\n"
    "  for (int k = 0; k < int(h.n.x); k++) {\n"
    "    vec4 r = h.hud[k];\n"
    "    if (px.x < r.z && px.z > r.x && px.y < r.w && px.w > r.y) hm = 1.0;\n"
    "  }\n"
    "  if (p.x < int(pc.a.z) && p.y < int(pc.a.w)) imageStore(l1, p, vec4(v, hm, 0.0, 1.0));\n"
    "  s[i] = v;\n"
    "  barrier();\n"
    "  if ((l.x & 1) == 0 && (l.y & 1) == 0) {\n"
    "    float m = (s[i] + s[i + 1] + s[i + 8] + s[i + 9]) * 0.25;\n"
    "    ivec2 q = p / 2;\n"
    "    if (q.x < int(pc.b.x) && q.y < int(pc.b.y)) imageStore(l2, q, vec4(m, 0.0, 0.0, 1.0));\n"
    "    s[i] = m;\n"
    "  }\n"
    "  barrier();\n"
    "  if ((l.x & 3) == 0 && (l.y & 3) == 0) {\n"
    "    float m = (s[i] + s[i + 2] + s[i + 16] + s[i + 18]) * 0.25;\n"
    "    ivec2 q = p / 4;\n"
    "    if (q.x < int(pc.b.z) && q.y < int(pc.b.w)) imageStore(l3, q, vec4(m, 0.0, 0.0, 1.0));\n"
    "    s[i] = m;\n"
    "  }\n"
    "  barrier();\n"
    "  if (i == 0) {\n"
    "    float m = (s[0] + s[4] + s[32] + s[36]) * 0.25;\n"
    "    ivec2 q = p / 8;\n"
    "    if (q.x < int(pc.c.x) && q.y < int(pc.c.y)) imageStore(l4, q, vec4(m, 0.0, 0.0, 1.0));\n"
    "  }\n"
    "}\n";

// The mean absolute difference of the 4x4 texels round the block's centre c (a texel corner) between A and B for the
// whole motion v (texels of the level), split between the two: A at c - floor(v / 2), B at c + v - floor(v / 2), so that
// v takes every whole texel (a split in halves would take every other one). Each gather takes exactly the 2x2 texels
// round a corner.
#define SAD_GLSL                                                                               \
    "float sad4(vec2 c, vec2 v) {\n"                                                           \
    "  vec2 h = floor(v * 0.5), ca = c - h, cb = c + v - h;\n"                                 \
    "  float s = 0.0;\n"                                                                       \
    "  for (int j = 0; j < 2; j++)\n"                                                          \
    "    for (int i = 0; i < 2; i++) {\n"                                                      \
    "      vec2 o = vec2(float(i * 2 - 1), float(j * 2 - 1));\n"                               \
    "      vec4 d = abs(textureGather(la, (ca + o) * pc.a.xy) - textureGather(lb, (cb + o) * pc.a.xy));\n" \
    "      s += d.x + d.y + d.z + d.w;\n"                                                      \
    "    }\n"                                                                                  \
    "  return s * 0.0625;\n"                                                                   \
    "}\n"

// The top of the pyramid (1/32): one vector per 2x2 texels (64x64 pixels), every whole motion of up to R texels each way
// (R = 6: 192 pixels a game frame), a hair's preference for less motion. Out: the motion from A to B in pixels, its cost.
static const char *SEARCH_GLSL = FG_HEAD
    "layout(set = 0, binding = 0) uniform sampler2D la;\n"
    "layout(set = 0, binding = 1) uniform sampler2D lb;\n"
    "layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D vo;\n"
    "layout(push_constant) uniform P { vec4 a; vec4 b; } pc;  // a: 1 / the level's size, the field's size; b.x: pixels a texel, b.y: R\n"
    SAD_GLSL
    "void main() {\n"
    "  ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
    "  if (p.x >= int(pc.a.z) || p.y >= int(pc.a.w)) return;\n"
    "  vec2 c = vec2(p) * 2.0 + 1.0;\n"
    "  int r = int(pc.b.y);\n"
    "  float best = 1e9;\n"
    "  vec2 bv = vec2(0.0);\n"
    "  for (int y = -r; y <= r; y++)\n"
    "    for (int x = -r; x <= r; x++) {\n"
    "      vec2 v = vec2(float(x), float(y));\n"
    "      float s = sad4(c, v) + 0.0005 * (abs(v.x) + abs(v.y));\n"
    "      if (s < best) { best = s; bv = v; }\n"
    "    }\n"
    "  imageStore(vo, p, vec4(bv * pc.b.x, best, 0.0));\n"
    "}\n";

// A finer level: one vector per bs x bs texels; candidates the parent level's four vectors round the block, their
// bilinear mix and no motion, the best taken a 3x3 step further (and, at the last level, below a texel by a parabola
// through its neighbours' costs where it stays in the middle); a small cost for leaving the parents' mix keeps flat
// parts (sky, water) moving with what is round them.
static const char *REFINE_GLSL = FG_HEAD
    "layout(set = 0, binding = 0) uniform sampler2D la;\n"
    "layout(set = 0, binding = 1) uniform sampler2D lb;\n"
    "layout(set = 0, binding = 2) uniform sampler2D vp;\n"
    "layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D vo;\n"
    "layout(push_constant) uniform P { vec4 a; vec4 b; vec4 c; } pc;\n"
    "// a: 1 / the level's size, the field's size; b: pixels a texel, texels a block, the parent field's size;\n"
    "// c.x: pixels a parent block, c.y: the cost of leaving the parents' mix (a texel), c.z: 1 at the last level\n"
    SAD_GLSL
    "vec2 pm;\n"
    "float cost(vec2 c, vec2 v) { return sad4(c, v) + pc.c.y * length(v - pm); }\n"
    "void main() {\n"
    "  ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
    "  if (p.x >= int(pc.a.z) || p.y >= int(pc.a.w)) return;\n"
    "  vec2 c = vec2(p) * pc.b.y + pc.b.y * 0.5;\n"
    "  vec2 q = c * pc.b.x / pc.c.x - 0.5;\n"
    "  ivec2 q0 = ivec2(floor(q)), hi = ivec2(pc.b.zw) - 1;\n"
    "  vec2 f = q - vec2(q0);\n"
    "  vec2 p00 = texelFetch(vp, clamp(q0, ivec2(0), hi), 0).xy / pc.b.x;\n"
    "  vec2 p10 = texelFetch(vp, clamp(q0 + ivec2(1, 0), ivec2(0), hi), 0).xy / pc.b.x;\n"
    "  vec2 p01 = texelFetch(vp, clamp(q0 + ivec2(0, 1), ivec2(0), hi), 0).xy / pc.b.x;\n"
    "  vec2 p11 = texelFetch(vp, clamp(q0 + ivec2(1, 1), ivec2(0), hi), 0).xy / pc.b.x;\n"
    "  pm = mix(mix(p00, p10, f.x), mix(p01, p11, f.x), f.y);\n"
    "  vec2 cand[6];\n"
    "  cand[0] = floor(pm + 0.5); cand[1] = floor(p00 + 0.5); cand[2] = floor(p10 + 0.5);\n"
    "  cand[3] = floor(p01 + 0.5); cand[4] = floor(p11 + 0.5); cand[5] = vec2(0.0);\n"
    "  float best = 1e9;\n"
    "  vec2 bv = vec2(0.0);\n"
    "  for (int i = 0; i < 6; i++) {\n"
    "    float s = cost(c, cand[i]);\n"
    "    if (s < best) { best = s; bv = cand[i]; }\n"
    "  }\n"
    "  float s3[9];\n"
    "  int bi = 4;\n"
    "  for (int y = -1; y <= 1; y++)\n"
    "    for (int x = -1; x <= 1; x++) {\n"
    "      int k = (y + 1) * 3 + x + 1;\n"
    "      s3[k] = k == 4 ? best : cost(c, bv + vec2(float(x), float(y)));\n"
    "      if (s3[k] < s3[bi]) bi = k;\n"
    "    }\n"
    "  vec2 sub = vec2(0.0);\n"
    "  if (bi == 4 && pc.c.z > 0.5) {\n"
    "    float dx = s3[3] - 2.0 * s3[4] + s3[5], dy = s3[1] - 2.0 * s3[4] + s3[7];\n"
    "    if (dx > 1e-5) sub.x = clamp(0.5 * (s3[3] - s3[5]) / dx, -0.5, 0.5);\n"
    "    if (dy > 1e-5) sub.y = clamp(0.5 * (s3[1] - s3[7]) / dy, -0.5, 0.5);\n"
    "  }\n"
    "  bv += vec2(float(bi % 3 - 1), float(bi / 3 - 1));\n"
    "  imageStore(vo, p, vec4((bv + sub) * pc.b.x, s3[bi], 0.0));\n"
    "}\n";

// the last field's 3x3 median, each component on its own
static const char *CLEAN_GLSL = FG_HEAD
    "layout(set = 0, binding = 0) uniform sampler2D vi;\n"
    "layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D vo;\n"
    "layout(push_constant) uniform P { vec4 a; } pc;  // a.xy: the field's size\n"
    "#define S(i, j) { vec4 lo = min(v[i], v[j]); v[j] = max(v[i], v[j]); v[i] = lo; }\n"
    "void main() {\n"
    "  ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
    "  if (p.x >= int(pc.a.x) || p.y >= int(pc.a.y)) return;\n"
    "  ivec2 hi = ivec2(pc.a.xy) - 1;\n"
    "  vec4 v[9];\n"
    "  for (int y = -1; y <= 1; y++)\n"
    "    for (int x = -1; x <= 1; x++) v[(y + 1) * 3 + x + 1] = texelFetch(vi, clamp(p + ivec2(x, y), ivec2(0), hi), 0);\n"
    "  S(1, 2) S(4, 5) S(7, 8) S(0, 1) S(3, 4) S(6, 7) S(1, 2) S(4, 5) S(7, 8) S(0, 3) S(5, 8) S(4, 7) S(3, 6)\n"
    "  S(1, 4) S(2, 5) S(4, 7) S(4, 2) S(6, 4) S(4, 2)\n"
    "  imageStore(vo, p, v[4]);\n"
    "}\n";

// the made picture, and B into the history. %s: the pictures' storage format (rgba8 or rgba16f)
static const char *COMPOSE_GLSL = FG_HEAD
    "layout(set = 0, binding = 0) uniform sampler2D pa;\n"
    "layout(set = 0, binding = 1) uniform sampler2D pb;\n"
    "layout(set = 0, binding = 2) uniform sampler2D vf;\n"
    "layout(set = 0, binding = 3, %s) uniform writeonly image2D gen;\n"
    "layout(set = 0, binding = 4, %s) uniform writeonly image2D hist;\n"
    "layout(set = 0, binding = 5) uniform sampler2D hm;  // B's 1/4 level: its green, the HUD's mask\n"
    "layout(push_constant) uniform P { vec4 a; vec4 b; vec4 c; } u;\n"
    "// a: size, 1 / size; b: t, 1 = make the picture (0: B into the history only), the same-pixel threshold, -;\n"
    "// c: 1 / the field's size in pixels, the poor-match cost range\n"
    "bool in_hud(vec2 x) { return texelFetch(hm, ivec2(x * 0.25), 0).g > 0.5; }\n"
    "float maxc(vec4 d) { return max(max(d.r, d.g), d.b); }\n"
    "vec4 med3(vec4 a, vec4 b, vec4 c) { return max(min(a, b), min(max(a, b), c)); }\n"
    "void main() {\n"
    "  ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
    "  if (p.x >= int(u.a.x) || p.y >= int(u.a.y)) return;\n"
    "  vec4 c1 = texelFetch(pb, p, 0);\n"
    "  imageStore(hist, p, c1);\n"
    "  if (u.b.y < 0.5) return;\n"
    "  vec4 c0 = texelFetch(pa, p, 0);\n"
    "  vec2 x = vec2(p) + 0.5;\n"
    "  vec4 o = c1;\n"
    "  if (maxc(abs(c1 - c0)) > u.b.z && !in_hud(x)) {\n"
    "    vec4 m = texture(vf, x * u.c.xy);\n"
    "    float t = u.b.x;\n"
    "    vec2 xa = x - t * m.xy, xb = x + (1.0 - t) * m.xy;\n"
    "    bool oka = all(greaterThanEqual(xa, vec2(0.0))) && all(lessThan(xa, u.a.xy)) && !in_hud(xa);\n"
    "    bool okb = all(greaterThanEqual(xb, vec2(0.0))) && all(lessThan(xb, u.a.xy)) && !in_hud(xb);\n"
    "    vec4 a = texture(pa, xa * u.a.zw), b = texture(pb, xb * u.a.zw);\n"
    "    vec4 g = c1;\n"
    "    if (oka && okb) {\n"
    "      float k = smoothstep(0.04, 0.12, maxc(abs(a - b)));\n"
    "      g = mix(mix(a, b, t), med3(a, b, mix(c0, c1, t)), k);\n"
    "    } else if (oka) {\n"
    "      g = a;\n"
    "    } else if (okb) {\n"
    "      g = b;\n"
    "    }\n"
    "    o = mix(g, c1, smoothstep(u.c.z, u.c.w, m.z));\n"
    "  }\n"
    "  imageStore(gen, p, vec4(o.rgb, 1.0));\n"
    "}\n"
;

// ================================================================== Vulkan objects
typedef struct {
    VkDescriptorSetLayout set;
    VkPipelineLayout layout;
    VkPipeline pipe;
} Pass;
static Pass p_levels, p_search, p_refine, p_clean, p_compose;

static bool make_pass(const char *src, const char *name, const VkDescriptorType *types, uint32_t n, uint32_t push, Pass *out) {
    size_t nw = 0;
    uint32_t *spv = vk_glsl_to_spirv(src, STAGE_COMPUTE, name, &nw);
    if (!spv) return false;
    VkShaderModule m = vk_module(spv, nw);
    free(spv);
    VkDescriptorSetLayoutBinding b[8];
    for (uint32_t i = 0; i < n; i++) b[i] = (VkDescriptorSetLayoutBinding){i, types[i], 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = n, .pBindings = b};
    VK_CHECK(vkCreateDescriptorSetLayout(g_vk.dev, &li, NULL, &out->set));
    VkPushConstantRange r = {VK_SHADER_STAGE_COMPUTE_BIT, 0, push};
    VkPipelineLayoutCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &out->set,
                                     .pushConstantRangeCount = push ? 1 : 0, .pPushConstantRanges = &r};
    VK_CHECK(vkCreatePipelineLayout(g_vk.dev, &pi, NULL, &out->layout));
    VkComputePipelineCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = m, .pName = "main"},
                                      .layout = out->layout};
    VkResult res = vkCreateComputePipelines(g_vk.dev, g_vk.pcache, 1, &ci, NULL, &out->pipe);
    vkDestroyShaderModule(g_vk.dev, m, NULL);
    return res == VK_SUCCESS;
}

// One image of ours: a TexBackend (device.c presents the made picture as it presents the game's) and its view
typedef struct {
    TexBackend b;
    VkImageView view;
} Img;

static bool img_make(Img *im, VkFormat f, uint32_t w, uint32_t h) {
    memset(im, 0, sizeof *im);
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = f,
                            .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    if (vk_create_image(&ci, &im->b.image, &im->b.mem) != VK_SUCCESS) return false;
    im->b.format = f, im->b.base_type = VK_IMAGE_VIEW_TYPE_2D, im->b.width = w, im->b.height = h;
    im->b.depth = im->b.layers = im->b.levels = im->b.samples = 1;
    im->b.aspect = VK_IMAGE_ASPECT_COLOR_BIT, im->b.gpu_owned = true;
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = im->b.image, .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                .format = f, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VK_CHECK(vkCreateImageView(g_vk.dev, &vi, NULL, &im->view));
    VkCommandBuffer cb = vk_upload_cmd();
    vk_image_init_layout(cb, im->b.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VkClearColorValue zero = {{0, 0, 0, 0}};
    VkImageSubresourceRange rg = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cb, im->b.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &rg);
    vk_full_barrier(cb);
    return true;
}

static void img_free(Img *im) {
    if (!im->b.image) return;
    vk_views_forget(&im->b);  // (exec.c may have made views of the made picture to present it)
    vk_defer_view(im->view);
    vk_defer_image(im->b.image, im->b.mem);
    memset(im, 0, sizeof *im);
}

// ================================================================== state
static struct {
    int tried;                // 0 not yet, 1 the passes are made, -1 they failed (never again this session)
    uint32_t w, h, lw[5], lh[5], fw[5], fh[5];  // the picture; the levels 1..4 (1/4, 1/8, 1/16, 1/32); the fields 1..4
    VkFormat fmt;             // the history's and the made picture's
    Img hist[3], lv[3][5];    // B kept for the next pair and its levels (a ring: [ring] is the newest)
    Img fd[5], vm, gen;       // the fields (64, 32, 16, 16 pixels a vector at 4, 3, 2, 1), the cleaned one, the made picture
    int ring;                 // the newest history slot
    int have;                 // how many frames in a row the history holds (0: start again)
    // timing
    VkQueryPool qp;
    bool ts;                  // timestamps work on this queue
    uint64_t qframe[8];       // which made picture each pair of queries is for (0: none waiting)
    uint64_t made_no;
    // the per-minute line
    uint64_t log_t0;
    uint32_t n_game, n_made, n_shown, n_warm, n_menu, n_settings, n_cadence, n_format;  // (n_slow: below)
    double gpu_sum, gpu_max;
    uint32_t gpu_n;
    int last_n;
    int state, told_state;    // 0 off / 1 making; the last one logged
    uint64_t told_at;
    int told_lines;
    // the game's own rate while pictures are made (a phone whose GPU can't do both: none made for a while)
    uint64_t last_real_ns, pause_until;
    double avg_ns;            // the time between game frames, smoothed
    int slow, pauses;
    uint32_t n_slow;
    // eval (MHGU_FRAMEGEN_EVAL)
    int eval_left, eval_every, eval_no;
    uint64_t eval_at, first_ns;  // (MHGU_FRAMEGEN_EVAL_AT, ns after the first frame; when that was)
    uint64_t eval_frame;
    VkBuffer eval_buf;
    VmaAllocation eval_mem;
    void *eval_ptr;
    uint64_t eval_seq;        // the submission whose copies wait to be saved (0: none)
    float hud[12][4];         // the last made picture's HUD boxes (logged with a quality sample)
    int nhud;
} fg;

static int env_force = -1;
bool vk_framegen_enabled(void) {
    if (env_force < 0) {
        const char *e = getenv("MHGU_FRAMEGEN");
        env_force = e && *e == '1';
        const char *v = getenv("MHGU_FRAMEGEN_EVAL");
        fg.eval_left = v ? atoi(v) : 0;
        const char *k = getenv("MHGU_FRAMEGEN_EVAL_EVERY");
        fg.eval_every = k && atoi(k) > 2 ? atoi(k) : 20;
        const char *at = getenv("MHGU_FRAMEGEN_EVAL_AT");
        fg.eval_at = at ? (uint64_t)(atof(at) * 1e9) : 0;
        if (fg.eval_left > 0) LOG("[framegen] quality samples: %d, one every %d frames (MHGU_FRAMEGEN_EVAL)\n", fg.eval_left, fg.eval_every);
    }
    return env_force || fg.eval_left > 0 || fg.eval_seq || config_live_frame_gen();  // (eval_seq: the last sample to save)
}

static bool setup(void) {
    if (fg.tried) return fg.tried > 0;
    fg.tried = -1;
    static const VkDescriptorType levels_t[] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
    static const VkDescriptorType search_t[] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    static const VkDescriptorType refine_t[] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    static const VkDescriptorType clean_t[] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    static const VkDescriptorType compose_t[] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                 VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
    char compose[8192];
    const char *f = fg.fmt == VK_FORMAT_R8G8B8A8_UNORM ? "rgba8" : "rgba16f";
    snprintf(compose, sizeof compose, COMPOSE_GLSL, f, f);
    if (!make_pass(LEVELS_GLSL, "framegen_levels", levels_t, 6, 48, &p_levels) ||
        !make_pass(SEARCH_GLSL, "framegen_search", search_t, 3, 32, &p_search) ||
        !make_pass(REFINE_GLSL, "framegen_refine", refine_t, 4, 48, &p_refine) ||
        !make_pass(CLEAN_GLSL, "framegen_clean", clean_t, 2, 16, &p_clean) ||
        !make_pass(compose, fg.fmt == VK_FORMAT_R8G8B8A8_UNORM ? "framegen_compose8" : "framegen_compose16", compose_t, 6, 48, &p_compose)) {
        LOG("[framegen] its shaders could not be made: frame generation stays off this session\n");
        return false;
    }
    uint32_t qf = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.phys, &qf, NULL);
    VkQueueFamilyProperties qprops[16];
    if (qf > 16) qf = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.phys, &qf, qprops);
    fg.ts = g_vk.qfam < qf && qprops[g_vk.qfam].timestampValidBits > 0 && g_vk.props.limits.timestampPeriod > 0;
    if (fg.ts) {
        VkQueryPoolCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 16};
        fg.ts = vkCreateQueryPool(g_vk.dev, &qi, NULL, &fg.qp) == VK_SUCCESS;
    }
    fg.tried = 1;
    LOG("[framegen] ready (GPU time %s)\n", fg.ts ? "measured" : "not measurable on this queue");
    return true;
}

// (runtime/host_input.c, the control file's "fgeval <n> [frames]": tests) quality samples from now on, as MHGU_FRAMEGEN_EVAL
void vk_framegen_eval(int n, int every) {
    vk_framegen_enabled();  // (the environment read first, so it doesn't override this)
    fg.eval_every = every > 2 ? every : 20;
    fg.eval_at = 0;
    fg.eval_left = n > 0 ? n : 0;
    LOG("[framegen] quality samples: %d, one every %d frames (control file)\n", fg.eval_left, fg.eval_every);
}

static void free_images(void) {
    for (int i = 0; i < 3; i++) {
        img_free(&fg.hist[i]);
        for (int k = 1; k <= 4; k++) img_free(&fg.lv[i][k]);
    }
    for (int k = 1; k <= 4; k++) img_free(&fg.fd[k]);
    img_free(&fg.vm), img_free(&fg.gen);
    fg.w = fg.h = 0, fg.have = 0;
}

// The images for a w x h picture in format f (made again, and the history started again, when either changes)
static bool images_for(uint32_t w, uint32_t h, VkFormat f) {
    if (fg.w == w && fg.h == h && fg.fmt == f) return true;
    bool remake_compose = fg.tried > 0 && fg.fmt != f;
    free_images();
    if (remake_compose) {  // (the storage format is in the compose shader)
        vkDestroyPipeline(g_vk.dev, p_compose.pipe, NULL);
        vkDestroyPipelineLayout(g_vk.dev, p_compose.layout, NULL);
        vkDestroyDescriptorSetLayout(g_vk.dev, p_compose.set, NULL);
        vkDestroyPipeline(g_vk.dev, p_levels.pipe, NULL), vkDestroyPipelineLayout(g_vk.dev, p_levels.layout, NULL);
        vkDestroyDescriptorSetLayout(g_vk.dev, p_levels.set, NULL);
        vkDestroyPipeline(g_vk.dev, p_search.pipe, NULL), vkDestroyPipelineLayout(g_vk.dev, p_search.layout, NULL);
        vkDestroyDescriptorSetLayout(g_vk.dev, p_search.set, NULL);
        vkDestroyPipeline(g_vk.dev, p_refine.pipe, NULL), vkDestroyPipelineLayout(g_vk.dev, p_refine.layout, NULL);
        vkDestroyDescriptorSetLayout(g_vk.dev, p_refine.set, NULL);
        vkDestroyPipeline(g_vk.dev, p_clean.pipe, NULL), vkDestroyPipelineLayout(g_vk.dev, p_clean.layout, NULL);
        vkDestroyDescriptorSetLayout(g_vk.dev, p_clean.set, NULL);
        if (fg.qp) vkDestroyQueryPool(g_vk.dev, fg.qp, NULL), fg.qp = VK_NULL_HANDLE;
        fg.tried = 0;
    }
    fg.fmt = f;
    if (!setup()) return false;
    fg.w = w, fg.h = h;
    fg.lw[1] = (w + 3) / 4, fg.lh[1] = (h + 3) / 4;
    for (int k = 2; k <= 4; k++) fg.lw[k] = (fg.lw[k - 1] + 1) / 2, fg.lh[k] = (fg.lh[k - 1] + 1) / 2;
    fg.fw[1] = (fg.lw[1] + 3) / 4, fg.fh[1] = (fg.lh[1] + 3) / 4;  // (4x4 texels a vector at 1/4, 2x2 at the others)
    for (int k = 2; k <= 4; k++) fg.fw[k] = (fg.lw[k] + 1) / 2, fg.fh[k] = (fg.lh[k] + 1) / 2;
    bool ok = true;
    for (int i = 0; i < 3 && ok; i++) {
        ok = img_make(&fg.hist[i], f, w, h);
        for (int k = 1; k <= 4 && ok; k++) ok = img_make(&fg.lv[i][k], VK_FORMAT_R8G8B8A8_UNORM, fg.lw[k], fg.lh[k]);
    }
    for (int k = 1; k <= 4 && ok; k++) ok = img_make(&fg.fd[k], VK_FORMAT_R16G16B16A16_SFLOAT, fg.fw[k], fg.fh[k]);
    ok = ok && img_make(&fg.vm, VK_FORMAT_R16G16B16A16_SFLOAT, fg.fw[1], fg.fh[1]) && img_make(&fg.gen, f, w, h);
    if (!ok) {
        LOG("[framegen] its images (%ux%u) could not be made: none made until the picture's size changes\n", w, h);
        free_images();
        fg.w = w, fg.h = h;  // (not tried again for this size)
        return false;
    }
    LOG("[framegen] pictures %ux%u (%s), vectors %ux%u (searched at %ux%u)\n", w, h,
        f == VK_FORMAT_R8G8B8A8_UNORM ? "RGBA8" : "RGBA16F", fg.fw[1], fg.fh[1], fg.fw[4], fg.fh[4]);
    return true;
}

// ================================================================== recording
typedef struct { VkDescriptorType type; VkImageView view; bool linear; VkDescriptorBufferInfo buf; } Bind;
static void bind_dispatch(VkCommandBuffer cb, const Pass *p, const Bind *b, uint32_t n, const void *push, uint32_t push_size,
                          uint32_t gx, uint32_t gy) {
    VkDescriptorSet set = vk_alloc_set(p->set);
    VkDescriptorImageInfo ii[8];
    VkWriteDescriptorSet w[8];
    for (uint32_t i = 0; i < n; i++) {
        w[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = i,
                                      .descriptorCount = 1, .descriptorType = b[i].type};
        if (b[i].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            w[i].pBufferInfo = &b[i].buf;
        } else {
            ii[i] = (VkDescriptorImageInfo){b[i].type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ? VK_NULL_HANDLE : vk_default_sampler(b[i].linear),
                                            b[i].view, VK_IMAGE_LAYOUT_GENERAL};
            w[i].pImageInfo = &ii[i];
        }
    }
    vkUpdateDescriptorSets(g_vk.dev, n, w, 0, NULL);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p->layout, 0, 1, &set, 0, NULL);
    if (push_size) vkCmdPushConstants(cb, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size, push);
    vkCmdDispatch(cb, (gx + 7) / 8, (gy + 7) / 8, 1);
    // what the next pass reads (another dispatch, the window image's draw, a sample's copy) waits for these writes only:
    // a full barrier after each of the ten dispatches cost a Mali its caches every time (MHGU_FRAMEGEN_BARRIER=full: as
    // the rest of the renderer does)
    static int full = -1;
    if (full < 0) full = getenv("MHGU_FRAMEGEN_BARRIER") && !strcmp(getenv("MHGU_FRAMEGEN_BARRIER"), "full");
    if (full) {
        vk_full_barrier(cb);
    } else {
        VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                              .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT};
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 1, &mb, 0, NULL, 0, NULL);
    }
}
#define TEX(v, lin) {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (v), (lin), {0}}
#define OUT(v) {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, (v), false, {0}}

// The game's picture as raw values (exec.c raw_view: an sRGB format read as UNORM, as the present samples it)
static VkImageView raw_view(TexBackend *b) {
    VkFormat f = b->format == VK_FORMAT_R8G8B8A8_SRGB ? VK_FORMAT_R8G8B8A8_UNORM
               : b->format == VK_FORMAT_B8G8R8A8_SRGB ? VK_FORMAT_B8G8R8A8_UNORM : b->format;
    static const VkComponentMapping id = {0};
    return vk_image_view(b, VK_IMAGE_VIEW_TYPE_2D, f, 0, 1, 0, 1, id, VK_IMAGE_ASPECT_COLOR_BIT);
}

// This frame's HUD boxes in the picture's pixels (touch.cpp's parts on the 1280x720 canvas, which spans the picture),
// a few pixels wider all round
static int hud_boxes(float (*out)[4], int max) {
    float c[12][4];
    int n = touch_hud_frame_boxes(c, max < 12 ? max : 12);
    float sx = fg.w / 1280.0f, sy = fg.h / 720.0f;
    bool flip = nvn_window_origin() == 0;  // (the picture kept bottom-up: exec.c vk_present_draw's flip)
    for (int i = 0; i < n; i++) {
        float x0 = c[i][0] * sx - 4, x1 = c[i][2] * sx + 4, y0 = c[i][1] * sy - 4, y1 = c[i][3] * sy + 4;
        if (flip) { float t = fg.h - y1; y1 = fg.h - y0; y0 = t; }
        out[i][0] = x0, out[i][1] = y0, out[i][2] = x1, out[i][3] = y1;
    }
    return n;
}

// The levels of the picture just put into the history slot: 1/4, 1/8, 1/16 and 1/32 in one pass, with this frame's HUD
// mask in 1/4's green
static void record_levels(VkCommandBuffer cb, VkImageView src, uint32_t sw, uint32_t sh, int slot) {
    float pc[12] = {1.0f / sw, 1.0f / sh, (float)fg.lw[1], (float)fg.lh[1], (float)fg.lw[2], (float)fg.lh[2],
                    (float)fg.lw[3], (float)fg.lh[3], (float)fg.lw[4], (float)fg.lh[4], 0, 0};
    struct { float n[4], hud[12][4]; } u = {{0}};
    u.n[0] = (float)(fg.nhud = hud_boxes(u.hud, 12));
    memcpy(fg.hud, u.hud, sizeof fg.hud);
    VkSpan sp = vk_transient_copy(&u, sizeof u, g_vk.props.limits.minUniformBufferOffsetAlignment);
    Bind b[] = {TEX(src, true), OUT(fg.lv[slot][1].view), OUT(fg.lv[slot][2].view), OUT(fg.lv[slot][3].view),
                OUT(fg.lv[slot][4].view), {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_NULL_HANDLE, false, {sp.buf, sp.off, sizeof u}}};
    bind_dispatch(cb, &p_levels, b, 6, pc, 48, fg.lw[1], fg.lh[1]);
    vk_gpuprof_mark("framegen: the luma levels");
}

// pass 5 (and the history): mode 0 B into the history only, 1 the made picture too (the HUD's mask: B's 1/4 level)
static void record_compose(VkCommandBuffer cb, VkImageView pb, int slot_a, int slot_b, float t, int mode) {
    static float conf_lo = -1, conf_hi;  // (MHGU_FRAMEGEN_CONF=lo,hi: the poor-match cost range, tests)
    if (conf_lo < 0) {
        const char *e = getenv("MHGU_FRAMEGEN_CONF");
        if (!e || sscanf(e, "%f,%f", &conf_lo, &conf_hi) != 2 || conf_hi <= conf_lo) conf_lo = 0.10f, conf_hi = 0.25f;
    }
    float pc[12] = {(float)fg.w, (float)fg.h, 1.0f / fg.w, 1.0f / fg.h, t, (float)mode, 1.5f / 255.0f, 0,
                    1.0f / (16.0f * fg.fw[1]), 1.0f / (16.0f * fg.fh[1]), conf_lo, conf_hi};
    Bind b[] = {TEX(fg.hist[slot_a].view, true), TEX(pb, true), TEX(fg.vm.view, true), OUT(fg.gen.view), OUT(fg.hist[slot_b].view),
                TEX(fg.lv[slot_b][1].view, false)};
    bind_dispatch(cb, &p_compose, b, 6, pc, 48, fg.w, fg.h);
    vk_gpuprof_mark(mode ? "framegen: the made picture (and B kept)" : "framegen: B kept");
}

// passes 2 to 4: the field between the history slots a (earlier) and b (later): a full search at 1/32, refined at 1/16,
// 1/8 and 1/4 (texels of 32, 16, 8 and 4 pixels; parent blocks of 64, 32 and 16 pixels), then its median
static void record_field(VkCommandBuffer cb, int a, int b) {
    float ps[8] = {1.0f / fg.lw[4], 1.0f / fg.lh[4], (float)fg.fw[4], (float)fg.fh[4], 32.0f, 6.0f, 0, 0};
    Bind bs[] = {TEX(fg.lv[a][4].view, false), TEX(fg.lv[b][4].view, false), OUT(fg.fd[4].view)};
    bind_dispatch(cb, &p_search, bs, 3, ps, 32, fg.fw[4], fg.fh[4]);
    vk_gpuprof_mark("framegen: the search at 1/32");
    for (int k = 3; k >= 1; k--) {
        float texel = (float)(4 << (k - 1)), block = k == 1 ? 4.0f : 2.0f, parent = k == 3 ? 64.0f : k == 2 ? 32.0f : 16.0f;
        float pr[12] = {1.0f / fg.lw[k], 1.0f / fg.lh[k], (float)fg.fw[k], (float)fg.fh[k],
                        texel, block, (float)fg.fw[k + 1], (float)fg.fh[k + 1],
                        parent, 0.002f, k == 1 ? 1.0f : 0.0f, 0};
        Bind br[] = {TEX(fg.lv[a][k].view, false), TEX(fg.lv[b][k].view, false), TEX(fg.fd[k + 1].view, false), OUT(fg.fd[k].view)};
        bind_dispatch(cb, &p_refine, br, 4, pr, 48, fg.fw[k], fg.fh[k]);
        vk_gpuprof_mark(k == 3 ? "framegen: refined at 1/16" : k == 2 ? "framegen: refined at 1/8" : "framegen: refined at 1/4");
    }
    float pm[4] = {(float)fg.fw[1], (float)fg.fh[1], 0, 0};
    Bind bm[] = {TEX(fg.fd[1].view, false), OUT(fg.vm.view)};
    bind_dispatch(cb, &p_clean, bm, 2, pm, 16, fg.fw[1], fg.fh[1]);
    vk_gpuprof_mark("framegen: the median");
}

// ================================================================== timing and the log
static void read_times(void) {
    if (!fg.ts) return;
    for (int i = 0; i < 8; i++) {
        if (!fg.qframe[i] || fg.made_no - fg.qframe[i] < 3) continue;  // (three made pictures later: long done)
        uint64_t t[2];
        VkResult r = vkGetQueryPoolResults(g_vk.dev, fg.qp, (uint32_t)i * 2, 2, sizeof t, t, 8, VK_QUERY_RESULT_64_BIT);
        if (r == VK_NOT_READY && fg.made_no - fg.qframe[i] < 8) continue;
        fg.qframe[i] = 0;
        if (r != VK_SUCCESS || t[1] < t[0]) continue;
        double ms = (t[1] - t[0]) * (double)g_vk.props.limits.timestampPeriod / 1e6;
        if (ms > 100) continue;
        fg.gpu_sum += ms, fg.gpu_n++;
        if (ms > fg.gpu_max) fg.gpu_max = ms;
    }
}

static void minute_line(uint64_t now) {
    if (!fg.log_t0) fg.log_t0 = now;
    if (now - fg.log_t0 < 60000000000ull) return;
    double s = (now - fg.log_t0) / 1e9;
    if (fg.n_game)
        LOG("[framegen] last %.0f s: %u pictures made between %u of the game's (shown %.1f FPS, the game %.1f); not made: %u "
            "warming up, %u the game's menus, %u the settings menu, %u not at half the refresh rate or less, %u other "
            "picture format, %u paused (the game fell behind its limit); GPU %.2f ms a made picture (max %.2f, %u measured), "
            "%ux%u, %d vsyncs a game frame\n",
            s, fg.n_made, fg.n_game, (fg.n_game + fg.n_shown) / s, fg.n_game / s, fg.n_warm, fg.n_menu, fg.n_settings,
            fg.n_cadence, fg.n_format, fg.n_slow, fg.gpu_n ? fg.gpu_sum / fg.gpu_n : 0.0, fg.gpu_max, fg.gpu_n, fg.w, fg.h,
            fg.last_n);
    fg.log_t0 = now;
    fg.n_game = fg.n_made = fg.n_shown = fg.n_warm = fg.n_menu = fg.n_settings = fg.n_cadence = fg.n_format = 0;
    fg.n_slow = 0;
    fg.gpu_sum = fg.gpu_max = 0, fg.gpu_n = 0;
}

// a change between making pictures and not, with the reason (at most one a 5 s, 60 lines)
static void fg_tell(int making, const char *why, uint64_t now) {
    fg.state = making;
    if (making == fg.told_state || fg.told_lines >= 60 || (fg.told_at && now - fg.told_at < 5000000000ull)) return;
    fg.told_state = making, fg.told_at = now, fg.told_lines++;
    LOG("[framegen] %s%s\n", making ? "making pictures" : "not making pictures: ", making ? "" : why);
}

volatile uint64_t g_framegen_presents;  // (runtime/perfstats.cpp: the FPS counter's "(60 shown)")
void vk_framegen_presented(void) {
    fg.n_shown++;
    __atomic_add_fetch(&g_framegen_presents, 1, __ATOMIC_RELAXED);
}

// ================================================================== quality samples (MHGU_FRAMEGEN_EVAL)
static float half_to_float(uint16_t h) {
    uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v = e == 0 ? ldexpf((float)m, -24) : e == 31 ? 65504.0f : ldexpf((float)(m | 1024), (int)e - 25);
    return s ? -v : v;
}
static void eval_save(void) {
    if (!fg.eval_seq) return;
    vk_wait_seq(fg.eval_seq);
    fg.eval_seq = 0;
    vmaInvalidateAllocation(g_vk.vma, fg.eval_mem, 0, VK_WHOLE_SIZE);
    size_t px = (size_t)fg.w * fg.h, bpp = fg.fmt == VK_FORMAT_R8G8B8A8_UNORM ? 4 : 8;
    uint8_t *rgba = malloc(px * 4);
    if (!rgba) return;
    static const char tag[4] = {'a', 't', 'b', 'g'};
    mkdir("shots", 0755);
    for (int k = 0; k < 4; k++) {
        const uint8_t *src = (const uint8_t *)fg.eval_ptr + k * px * bpp;
        if (bpp == 4) {
            memcpy(rgba, src, px * 4);
        } else {
            const uint16_t *h = (const uint16_t *)src;
            for (size_t i = 0; i < px * 4; i++) {
                float v = half_to_float(h[i]);
                rgba[i] = (uint8_t)(v <= 0 ? 0 : v >= 1 ? 255 : v * 255.0f + 0.5f);
            }
        }
        for (size_t i = 0; i < px; i++) rgba[i * 4 + 3] = 255;
        char path[96];
        snprintf(path, sizeof path, "shots/fgeval_%03d_%c.png", fg.eval_no, tag[k]);
        vk_save_png(path, rgba, fg.w, fg.h);
    }
    free(rgba);
    fg.eval_no++;
}
// the copies of one sample: the earlier, the truth (the frame between), the later, the made picture
static void eval_copy(VkCommandBuffer cb, int sa, int st, int sb) {
    size_t px = (size_t)fg.w * fg.h, bpp = fg.fmt == VK_FORMAT_R8G8B8A8_UNORM ? 4 : 8;
    if (!fg.eval_buf) {
        VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = px * bpp * 4, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO,
                                      .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT};
        VmaAllocationInfo info;
        if (vmaCreateBuffer(g_vk.vma, &bi, &ai, &fg.eval_buf, &fg.eval_mem, &info) != VK_SUCCESS) {
            fg.eval_left = 0;
            return;
        }
        fg.eval_ptr = info.pMappedData;
    }
    TexBackend *im[4] = {&fg.hist[sa].b, &fg.hist[st].b, &fg.hist[sb].b, &fg.gen.b};
    for (int k = 0; k < 4; k++) {
        VkBufferImageCopy rg = {.bufferOffset = k * px * bpp, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .imageExtent = {fg.w, fg.h, 1}};
        vkCmdCopyImageToBuffer(cb, im[k]->image, VK_IMAGE_LAYOUT_GENERAL, fg.eval_buf, 1, &rg);
    }
    vk_full_barrier(cb);
    fg.eval_seq = vk_pending_seq();
    char boxes[600] = "";
    for (int i = 0; i < fg.nhud; i++)
        snprintf(boxes + strlen(boxes), sizeof boxes - strlen(boxes), " %.0f,%.0f,%.0f,%.0f", fg.hud[i][0], fg.hud[i][1],
                 fg.hud[i][2], fg.hud[i][3]);
    LOG("[framegen] quality sample %d: HUD boxes%s\n", fg.eval_no, fg.nhud ? boxes : " none");
}

// ================================================================== the frame
TexBackend *vk_framegen_frame(TexBackend *real, int n, int *follow) {
    uint64_t now = plat_now_ns();
    *follow = 0;
    if (fg.tried < 0) return NULL;
    eval_save();
    read_times();
    minute_line(now);
    fg.n_game++;
    if (!fg.first_ns) fg.first_ns = now;
    bool eval = fg.eval_left > 0;
    if (eval && now - fg.first_ns < fg.eval_at) {  // (not yet: nothing at all)
        fg.n_game--;
        return NULL;
    }
    fg.last_n = n;
    // when none is made: the history starts again (the next pair must be two frames in a row)
    const char *why = NULL;
    int menu = touch_frame_menu();
    if (!eval && n < 2) why = "the FPS limit is not half the screen's refresh rate or less", fg.n_cadence++;
    else if (overlay_menu_open()) why = "the settings menu is open", fg.n_settings++;
    else if (menu) why = menu & 2 ? "the title or the main menu" : "a menu of the game's", fg.n_menu++;
    else if (!eval && fg.pauses >= 3) why = "the game can't keep its frame rate with it: off until the game starts again", fg.n_slow++;
    else if (!eval && now < fg.pause_until) why = "the game fell behind its frame rate with it: paused for 20 s", fg.n_slow++;
    // The game's own rate while pictures are made: when the time between its frames stays 12 % over the limit's for 3 s
    // (a phone whose GPU can't draw the game and the made pictures both), none is made for 20 s, and after the third
    // time not again until the game starts again. A load's hitch alone is far from 3 s of it.
    uint64_t dt = fg.last_real_ns ? now - fg.last_real_ns : 0;
    fg.last_real_ns = now;
    float cap = config_live_fps();
    if (!eval && !why && fg.state == 1 && dt > 0 && dt < 1000000000ull && cap > 0 && cap < FPS_UNLIMITED) {  // (a desktop's)
        double want = 1e9 / cap;
        fg.avg_ns = fg.avg_ns > 0 ? fg.avg_ns * 0.95 + dt * 0.05 : (double)dt;
        fg.slow = fg.avg_ns > want * 1.12 ? fg.slow + 1 : 0;
        if ((double)fg.slow * want >= 3e9) {
            fg.pauses++, fg.slow = 0, fg.avg_ns = 0;
            fg.pause_until = now + 20000000000ull;
            LOG("[framegen] the game falls behind its %.0f FPS with the made pictures (%.1f ms a frame): %s\n", cap,
                (double)dt / 1e6, fg.pauses >= 3 ? "none made until the game starts again" : "none made for 20 s");
            why = fg.pauses >= 3 ? "the game can't keep its frame rate with it: off until the game starts again"
                                 : "the game fell behind its frame rate with it: paused for 20 s";
            fg.n_slow++;
        }
    } else if (why || fg.state != 1) {
        fg.slow = 0, fg.avg_ns = 0;
    }
    VkFormat f = real->format == VK_FORMAT_R8G8B8A8_UNORM || real->format == VK_FORMAT_R8G8B8A8_SRGB ||
                         real->format == VK_FORMAT_B8G8R8A8_UNORM || real->format == VK_FORMAT_B8G8R8A8_SRGB
                     ? VK_FORMAT_R8G8B8A8_UNORM
                 : real->format == VK_FORMAT_R16G16B16A16_UNORM || real->format == VK_FORMAT_R16G16B16A16_SFLOAT
                     ? VK_FORMAT_R16G16B16A16_SFLOAT
                     : VK_FORMAT_UNDEFINED;
    if (!why && f == VK_FORMAT_UNDEFINED) why = "the game's picture has a format it does not take", fg.n_format++;
    if (!why && (real->width < 64 || real->height < 64)) why = "the picture is too small", fg.n_format++;
    if (why) {
        fg.have = 0;
        fg_tell(0, why, now);
        return NULL;
    }
    if (!images_for(real->width, real->height, f) || !fg.gen.b.image) {
        if (fg.tried < 0) fg_tell(0, "its shaders could not be made", now);
        return NULL;
    }
    VkCommandBuffer cb = vk_cmd();
    vk_full_barrier(cb);  // (the game's picture is finished)
    int slot = (fg.ring + 1) % 3;
    VkImageView pb = raw_view(real);
    int gap = eval ? 2 : 1;
    bool make = fg.have >= gap && (!eval || fg.n_game - fg.eval_frame >= (uint64_t)fg.eval_every);
    int qi = (int)(fg.made_no % 8);
    if (make && fg.ts) {
        vkCmdResetQueryPool(cb, fg.qp, (uint32_t)qi * 2, 2);
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, fg.qp, (uint32_t)qi * 2);
    }
    record_levels(cb, pb, real->width, real->height, slot);
    int a = (slot + 3 - gap) % 3;
    float t = n >= 2 ? (float)(n / 2) / n : 0.5f;
    if (make) record_field(cb, a, slot);
    record_compose(cb, pb, a, slot, t, make ? 1 : 0);
    fg.ring = slot;
    if (fg.have < 3) fg.have++;
    if (!make) {
        fg.n_warm++;
        return NULL;
    }
    if (fg.ts) {
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, fg.qp, (uint32_t)qi * 2 + 1);
        fg.qframe[qi] = ++fg.made_no;
    } else {
        ++fg.made_no;
    }
    if (eval) {  // (the made picture is between the history's two older slots; the truth is the one in the middle)
        eval_copy(cb, a, (slot + 2) % 3, slot);
        fg.eval_frame = fg.n_game;
        if (--fg.eval_left == 0) LOG("[framegen] quality samples done\n");
        return NULL;
    }
    fg.n_made++;
    fg_tell(1, NULL, now);
    *follow = n >= 2 ? n - n / 2 : 1;
    return &fg.gen.b;
}
