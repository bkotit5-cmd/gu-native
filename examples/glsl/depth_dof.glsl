#version 450
layout(set = 0, binding = 0) uniform sampler2D t;
layout(set = 0, binding = 1) uniform sampler2D depthTex;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o;

// Lightweight single-pass depth-aware blur example.
// Assumes standard forward depth (near=0, far=1). Adjust focusDepth for the game's depth convention.
const float focusDepth = 0.985;
const float depthSensitivity = 5.0;
const float maxBlurPixels = 4.0;

void main() {
    vec4 base = texture(t, uv);
    ivec2 depthSize = textureSize(depthTex, 0);
    if (depthSize.x <= 1 || depthSize.y <= 1) {
        o = base;
        return;
    }

    float centerDepth = texture(depthTex, uv).r;
    float coc = clamp(abs(centerDepth - focusDepth) * depthSensitivity, 0.0, 1.0);
    float radius = coc * maxBlurPixels;
    vec2 texel = 1.0 / vec2(textureSize(t, 0));

    const vec2 offsets[8] = vec2[8](
        vec2( 1.0,  0.0), vec2(-1.0,  0.0),
        vec2( 0.0,  1.0), vec2( 0.0, -1.0),
        vec2( 0.707,  0.707), vec2(-0.707,  0.707),
        vec2( 0.707, -0.707), vec2(-0.707, -0.707)
    );

    vec3 sum = base.rgb * 2.0;
    float weightSum = 2.0;
    for (int i = 0; i < 8; ++i) {
        vec2 sampleUV = clamp(uv + offsets[i] * texel * radius, vec2(0.0), vec2(1.0));
        float sampleDepth = texture(depthTex, sampleUV).r;
        float sameSurface = 1.0 - smoothstep(0.015, 0.12, abs(sampleDepth - centerDepth));
        float weight = 0.2 + 0.8 * sameSurface;
        sum += texture(t, sampleUV).rgb * weight;
        weightSum += weight;
    }

    vec3 blurred = sum / weightSum;
    o = vec4(mix(base.rgb, blurred, coc), base.a);
}
