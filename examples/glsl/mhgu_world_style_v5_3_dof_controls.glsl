#version 450

// =====================================================================
//  MHGU "World Style" v5.3 - grading ala Monster Hunter World + DoF + editable controls
//  Vulkan GLSL 450, single-pass fragment shader.
//  Input: t (final game image), depthTex (raw depth 0..1, optional), hudMask (HUD quad mask), uv, o.
//  Jika app hanya memberi depth 1x1 (fallback), DoF memakai blur tilt-shift.
//
//  Alur: DoF (bokeh) -> sharpen (hanya area tajam) -> soft bloom
//        -> split-toning -> warna natural -> filmic -> vignette -> dither
// =====================================================================

layout(set = 0, binding = 0) uniform sampler2D t;
layout(set = 0, binding = 1) uniform sampler2D depthTex;   // raw depth 0..1
layout(set = 0, binding = 3) uniform sampler2D hudMask;    // 1 = original 2D HUD pixel; restored after this shader
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o;

// ------------------------- PENGATURAN --------------------------------

// ---- Depth of Field ----
// The UI exposes up to 16 parameters. DEBUG_VIEW stays at 0; edit it here only for diagnostics.
// @param DOF_ENABLE bool 1 0 1 1 Enable depth of field
// @param DEPTH_MODE int 2 0 2 1 Depth convention: 0 forward, 1 reversed, 2 auto
// @param AUTOFOCUS bool 1 0 1 1 Autofocus
// @param FOCUS_SPREAD float 0.015 0.001 0.08 0.001 Autofocus sample spread
// @param MANUAL_FOCUS_D float 0.80 0.0 1.0 0.01 Manual focus depth
// @param DOF_FALLBACK bool 0 0 1 1 Enable full-image tilt-shift if scene depth is unavailable
// @param TILT_FOCUS_Y float 0.55 0.2 0.8 0.01 Tilt-shift focus position
// @param TILT_BAND float 0.20 0.05 0.45 0.01 Tilt-shift sharp band
// @param TILT_MAX float 0.010 0.0 0.03 0.001 Tilt-shift maximum blur
// @param FOCUS_RANGE float 0.12 0.01 0.35 0.01 Sharp focus range
// @param MAX_FAR float 0.012 0.0 0.03 0.001 Maximum far blur
// @param MAX_NEAR float 0.007 0.0 0.02 0.001 Maximum near blur
// @param SCENE_EDGE_PROTECT float 0.90 0.0 1.0 0.05 Preserve high-contrast scene edges
// @param SHARPEN float 0.50 0.0 1.5 0.05 Sharpen strength
// @param BLOOM_STRENGTH float 0.45 0.0 1.5 0.05 Bloom strength
// @param SATURATION float 0.78 0.0 1.5 0.02 Colour saturation

#define DOF_ENABLE PARAM_DOF_ENABLE
#define DEBUG_VIEW 0
#define DEPTH_MODE PARAM_DEPTH_MODE
const float Z_NEAR = 1.0;     // only the Z_FAR / Z_NEAR ratio matters
const float Z_FAR = 10000.0;

#define AUTOFOCUS PARAM_AUTOFOCUS
const vec2 FOCUS_POINT = vec2(0.5, 0.5);
#define FOCUS_SPREAD PARAM_FOCUS_SPREAD
#define MANUAL_FOCUS_D PARAM_MANUAL_FOCUS_D
#define DOF_FALLBACK PARAM_DOF_FALLBACK
#define TILT_FOCUS_Y PARAM_TILT_FOCUS_Y
#define TILT_BAND PARAM_TILT_BAND
#define TILT_MAX PARAM_TILT_MAX
#define FOCUS_RANGE PARAM_FOCUS_RANGE
#define MAX_FAR PARAM_MAX_FAR
#define MAX_NEAR PARAM_MAX_NEAR
#define SCENE_EDGE_PROTECT PARAM_SCENE_EDGE_PROTECT
// ---- Sharpen (0.0 = mati, hemat 4 fetch) ----
#define SHARPEN PARAM_SHARPEN
const float SHARPEN_LIMIT  = 0.10;

// ---- Soft bloom (0.0 = mati total, hemat 12 fetch) ----
#define BLOOM_STRENGTH PARAM_BLOOM_STRENGTH
const float BLOOM_THRESHOLD = 0.60;
const float BLOOM_KNEE      = 0.35;
const float BLOOM_SIZE      = 1.00;
const float BLOOM_JITTER    = 1.00;
const vec3  BLOOM_TINT      = vec3(1.00, 0.93, 0.80);

// ---- Warna ala World ----
const float GAMMA          = 2.2;
const float EXPOSURE       = 0.96;
#define SATURATION PARAM_SATURATION
const float VIBRANCE       = 0.12;
const float FOLIAGE        = 1.00;
const vec3  FOLIAGE_SHIFT  = vec3(1.10, 0.90, 0.78);
const float SKY_DEEPEN     = 0.60;
const vec3  SHADOW_TINT    = vec3(0.90, 0.99, 1.08);
const vec3  HIGHLIGHT_TINT = vec3(1.08, 1.00, 0.88);
const float CONTRAST       = 0.55;
const float BLACK_LIFT     = 0.020;

// ---- Vignette & dither ----
const float VIGNETTE       = 0.42;
const float DITHER         = 1.0;

// ---------------------------------------------------------------------
// Kernel bloom: (x, y, bobot), offset dalam fraksi tinggi layar, bobot total = 1.
const vec3 BLOOM_KERNEL[12] = vec3[12](
    vec3(0.00138, 0.00354, 0.20970),
    vec3(-0.00589, -0.00291, 0.14057),
    vec3(0.00815, -0.00237, 0.09423),
    vec3(-0.00521, 0.00858, 0.06316),
    vec3(-0.00221, -0.01117, 0.04234),
    vec3(0.00329, 0.00847, 0.12934),
    vec3(-0.01411, -0.00696, 0.09720),
    vec3(0.01951, -0.00568, 0.07304),
    vec3(-0.01248, 0.02055, 0.05489),
    vec3(-0.00530, -0.02674, 0.04125),
    vec3(0.02429, 0.01784, 0.03100),
    vec3(-0.03257, 0.00354, 0.02329)
);

// Kernel cakram DoF (12 tap)
const vec2 DOF_KERNEL[12] = vec2[12](
    vec2(0.07397, 0.19025),
    vec2(-0.31706, -0.15644),
    vec2(0.43824, -0.12758),
    vec2(-0.28039, 0.46157),
    vec2(-0.11910, -0.60068),
    vec2(0.54567, 0.40073),
    vec2(-0.73168, 0.07948),
    vec2(0.52186, -0.59385),
    vec2(0.01739, 0.84145),
    vec2(-0.61445, -0.64352),
    vec2(0.93332, 0.06251),
    vec2(-0.76442, 0.61155)
);

float luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// ------------------------------ DoF ----------------------------------

// Depth dinormalisasi: 0 = dekat, 1 = jauh
float depthRaw(vec2 p) {
    return textureLod(depthTex, clamp(p, vec2(0.0), vec2(1.0)), 0.0).r;
}

// rev = 1.0 untuk reversed-Z
float depth01(vec2 p, float rev) {
    return clamp(abs(rev - depthRaw(p)), 0.0, 1.0);
}

float zLin(float d) {
    return (Z_NEAR * Z_FAR) / (Z_FAR - d * (Z_FAR - Z_NEAR));
}

// CoC bertanda: positif = di belakang fokus, negatif = di depan fokus
float cocOf(float z, float zf) {
    float r = zf / z;
    float farAmt  = clamp((1.0 - r - FOCUS_RANGE) / (1.0 - FOCUS_RANGE) * FAR_RATE, 0.0, 1.0);
    float nearAmt = clamp((r - 1.0 - FOCUS_RANGE) * NEAR_RATE, 0.0, 1.0);
    return MAX_FAR * farAmt - MAX_NEAR * nearAmt;
}

float cocScreen(vec2 p) {
    float dy = abs(p.y - TILT_FOCUS_Y);
    return TILT_MAX * smoothstep(TILT_BAND, 0.55, dy);
}

vec3 dofGather(vec3 orig, float cc, float zf, float focusFade, float aspect, float ign, float rev, bool useDepth) {
    float ang = 6.2831853 * ign * JITTER;
    vec2 rs = vec2(cos(ang), sin(ang));
    float radius = abs(cc);

    vec3 acc = orig;
    float wsum = 1.0;
    for (int i = 0; i < 12; i++) {
        vec2 k = DOF_KERNEL[i];
        vec2 off = vec2(k.x * rs.x - k.y * rs.y, k.x * rs.y + k.y * rs.x) * radius;
        off.x /= aspect;
        vec2 p = uv + off;
        vec2 sampleUV = clamp(p, vec2(0.0), vec2(1.0));
        // Don't pull HUD pixels into nearby scene blur samples.
        if (textureLod(hudMask, sampleUV, 0.0).r >= 0.5) continue;

        vec3 s = textureLod(t, sampleUV, 0.0).rgb;
        float cs = useDepth ? cocOf(zLin(depth01(p, rev)), zf) * focusFade : cocScreen(p);

        // Latar: abaikan sampel yang lebih tajam/dekat (cegah halo objek depan)
        float w = 1.0;
        if (cc > 0.0) w = clamp(cs / cc, 0.0, 1.0);

        w *= 1.0 + BOKEH * smoothstep(0.65, 1.0, luma(s));
        acc += s * w;
        wsum += w;
    }
    return acc / wsum;
}

// ------------------------------ Bloom --------------------------------

vec3 bloomTap(vec2 p) {
    vec3 s = textureLod(t, clamp(p, vec2(0.0), vec2(1.0)), 0.0).rgb;
    float m = smoothstep(BLOOM_THRESHOLD, BLOOM_THRESHOLD + BLOOM_KNEE, luma(s));
    return s * s * m;
}

vec3 bloomGather(float aspect, vec2 rs) {
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 12; i++) {
        vec3 k = BLOOM_KERNEL[i];
        vec2 d = vec2(k.x * rs.x - k.y * rs.y, k.x * rs.y + k.y * rs.x) * BLOOM_SIZE;
        d.x /= aspect;
        acc += bloomTap(uv + d) * k.z;
    }
    return acc;
}

void main() {
    vec2 sz = vec2(textureSize(t, 0));
    vec2 px = 1.0 / sz;
    float aspect = sz.x / sz.y;

    // noise per-pixel: jitter DoF + jitter bloom + dither
    float ign = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));

    vec3 orig = textureLod(t, uv, 0.0).rgb;
    vec3 base = orig;
    float cc = 0.0;

    // 0) Depth of Field
    // depthTex 1x1 = texture fallback dari app (depth scene tidak terdeteksi)
    bool hasDepth = textureSize(depthTex, 0).x > 1;

    if (DOF_ENABLE) {
        float zf = 1.0;
        float focusFade = 1.0;
        float rev = 0.0;

        if (hasDepth) {
            // arah depth: biasa atau reversed-Z (deteksi dari langit: 1.0 biasa, 0.0 reversed)
            if (DEPTH_MODE == 1) {
                rev = 1.0;
            } else if (DEPTH_MODE == 2) {
                float v;
                float dmin = 1.0;
                float dmax = 0.0;
                v = depthRaw(vec2(0.25, 0.06)); dmin = min(dmin, v); dmax = max(dmax, v);
                v = depthRaw(vec2(0.75, 0.06)); dmin = min(dmin, v); dmax = max(dmax, v);
                v = depthRaw(vec2(0.25, 0.94)); dmin = min(dmin, v); dmax = max(dmax, v);
                v = depthRaw(vec2(0.75, 0.94)); dmin = min(dmin, v); dmax = max(dmax, v);
                v = depthRaw(vec2(0.50, 0.06)); dmin = min(dmin, v); dmax = max(dmax, v);
                v = depthRaw(vec2(0.50, 0.94)); dmin = min(dmin, v); dmax = max(dmax, v);
                rev = (dmin <= 0.0001 && dmax < 0.9999) ? 1.0 : 0.0;
            }

            float dC = depth01(uv, rev);

            // fokus (autofocus: rata-rata 5 titik di sekitar FOCUS_POINT)
            if (AUTOFOCUS) {
                vec2 h = vec2(FOCUS_SPREAD / aspect, FOCUS_SPREAD);
                float d0 = depth01(FOCUS_POINT, rev);
                float d1 = depth01(FOCUS_POINT + vec2( h.x, 0.0), rev);
                float d2 = depth01(FOCUS_POINT + vec2(-h.x, 0.0), rev);
                float d3 = depth01(FOCUS_POINT + vec2(0.0,  h.y), rev);
                float d4 = depth01(FOCUS_POINT + vec2(0.0, -h.y), rev);
                zf = (zLin(d0) + zLin(d1) + zLin(d2) + zLin(d3) + zLin(d4)) * 0.2;
                float dAvg = (d0 + d1 + d2 + d3 + d4) * 0.2;
                // fokus jatuh ke langit: matikan efek supaya tidak "meloncat"
                focusFade = 1.0 - smoothstep(0.9990, 0.9999, dAvg);
            } else {
                zf = zLin(MANUAL_FOCUS_D);
            }

            cc = cocOf(zLin(dC), zf) * focusFade;
        } else if (DOF_FALLBACK) {
            cc = cocScreen(uv);
        }

        // Optional scene-edge safeguard; the renderer restores HUD quads separately after this shader.
        // This can also keep high-contrast world geometry sharper around its edges.
        if (SCENE_EDGE_PROTECT > 0.0 && abs(cc) >= 0.0004) {
            float centerY = luma(orig);
            float edge = 0.0;
            const vec2 edgeDirs[4] = vec2[4](
                vec2(1.0, 0.0), vec2(-1.0, 0.0),
                vec2(0.0, 1.0), vec2(0.0, -1.0)
            );
            for (int j = 0; j < 4; ++j) {
                vec2 delta = edgeDirs[j] * px;
                float farY = luma(textureLod(t, clamp(uv + delta * 2.0, vec2(0.0), vec2(1.0)), 0.0).rgb);
                edge = max(edge, abs(centerY - farY));
            }
            float edgeProtect = smoothstep(0.025, 0.18, edge) * clamp(SCENE_EDGE_PROTECT, 0.0, 1.0);
            cc *= 1.0 - edgeProtect;
        }

        if (DEBUG_VIEW == 1) {
            if (!hasDepth) {
                o = vec4(1.0, 0.0, 0.0, 1.0);
                return;
            }
            // kiri: cocok untuk depth biasa, kanan: cocok untuk reversed-Z
            float raw = depthRaw(uv);
            float g = (uv.x < 0.5) ? pow(raw, 40.0) : pow(raw, 0.25);
            vec3 pr = vec3(g);
            if (abs(uv.x - 0.5) < 0.0012) pr = vec3(1.0, 0.0, 1.0);
            o = vec4(pr, 1.0);
            return;
        }
        if (DEBUG_VIEW == 2) {
            vec3 dbg = orig * 0.35;
            dbg.r += max(cc, 0.0) / MAX_FAR;
            dbg.b += max(-cc, 0.0) / MAX_NEAR;
            o = vec4(clamp(dbg, 0.0, 1.0), 1.0);
            return;
        }

        // The renderer also composites the exact original HUD pixels after the shader.
        // Skip DoF at their centres and exclude them from neighbouring scene taps.
        if (textureLod(hudMask, uv, 0.0).r >= 0.5)
            cc = 0.0;
        if (abs(cc) >= 0.0004) {
            base = dofGather(orig, cc, zf, focusFade, aspect, ign, rev, hasDepth);
        }
    }

    // 1) Sharpen halus (hanya di area yang tajam)
    if (SHARPEN > 0.0) {
        vec3 nb = textureLod(t, uv + vec2(0.0, -px.y), 0.0).rgb
                + textureLod(t, uv + vec2(0.0,  px.y), 0.0).rgb
                + textureLod(t, uv + vec2(-px.x, 0.0), 0.0).rgb
                + textureLod(t, uv + vec2( px.x, 0.0), 0.0).rgb;
        float edge = luma(orig) - luma(nb * 0.25);
        float sharpMask = 1.0 - clamp(abs(cc) / 0.003, 0.0, 1.0);
        base += clamp(edge * SHARPEN, -SHARPEN_LIMIT, SHARPEN_LIMIT) * sharpMask;
        base = clamp(base, 0.0, 1.0);
    }

    // 2) Soft bloom (12 tap, kernel konstan + rotasi per-pixel)
    vec3 bloom = vec3(0.0);
    if (BLOOM_STRENGTH > 0.0) {
        float ang = 6.2831853 * ign * BLOOM_JITTER;
        bloom = bloomGather(aspect, vec2(cos(ang), sin(ang))) * BLOOM_TINT * BLOOM_STRENGTH;
    }

    // 3) Ruang linear: split-toning + bloom (screen blend)
    vec3 lin = pow(base, vec3(GAMMA)) * EXPOSURE;
    float ll = luma(lin);
    float sw = 1.0 - smoothstep(0.0, 0.35, ll);
    float hw = smoothstep(0.25, 0.90, ll);
    lin *= mix(vec3(1.0), SHADOW_TINT, sw);
    lin *= mix(vec3(1.0), HIGHLIGHT_TINT, hw);
    lin = clamp(lin, 0.0, 1.0);
    lin = vec3(1.0) - (vec3(1.0) - lin) * (vec3(1.0) - clamp(bloom, 0.0, 1.0));

    // 4) Kembali ke gamma untuk grading
    vec3 c = pow(lin, vec3(1.0 / GAMMA));

    // Foliage: hijau lime MHGU -> olive yang lebih dalam
    float fol = smoothstep(0.02, 0.15, c.g - max(c.r, c.b));
    c *= mix(vec3(1.0), FOLIAGE_SHIFT, fol * FOLIAGE);

    // Saturasi + vibrance
    float lg = luma(c);
    float mx = max(max(c.r, c.g), c.b);
    float mn = min(min(c.r, c.g), c.b);
    float vib = 1.0 + VIBRANCE * (1.0 - (mx - mn));
    c = mix(vec3(lg), c, SATURATION * vib);

    // Langit: kurangi cyan, biru lebih dalam
    float sky = smoothstep(0.10, 0.40, c.b - c.r);
    c.g *= 1.0 - 0.10 * sky * SKY_DEEPEN;
    c.r *= 1.0 - 0.06 * sky * SKY_DEEPEN;
    c = clamp(c, 0.0, 1.0);

    // Kurva filmic + black lift
    vec3 sc = c * c * (3.0 - 2.0 * c);
    c = mix(c, sc, CONTRAST);
    c = c * (1.0 - BLACK_LIFT) + BLACK_LIFT;

    // 5) Vignette
    vec2 pv = uv - 0.5;
    float vig = smoothstep(0.10, 0.50, dot(pv, pv));
    c *= 1.0 - VIGNETTE * vig;

    // 6) Dither
    c += (ign - 0.5) * (DITHER / 255.0);

    // Lencana status depth (DEBUG_VIEW = 3)
    if (DEBUG_VIEW == 3 && uv.x < 0.05 && uv.y < 0.08) {
        c = hasDepth ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    }

    o = vec4(clamp(c, 0.0, 1.0), 1.0);
}
