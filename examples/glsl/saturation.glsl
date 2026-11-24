#version 450
// @param saturation float 1.18 0.0 2.0 0.01 Saturation

layout(set = 0, binding = 0) uniform sampler2D t;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o;

void main() {
    vec4 color = texture(t, uv);
    float luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec3 saturated = mix(vec3(luma), color.rgb, PARAM_saturation);
    o = vec4(clamp(saturated, 0.0, 1.0), color.a);
}
