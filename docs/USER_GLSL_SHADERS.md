# User GLSL shaders

Custom shaders are Vulkan GLSL 450 single-pass fragment shaders. The in-game **Settings → Shader** page lists installed `.glsl` files, selects one, disables custom processing, exposes its annotated parameters and removes files. Android uses the system document picker to import a `.glsl` file; there is no separate orange shader import/management screen.

## Switching and importing without restarting

On Android, choose **Add shader file** in the in-game Shader page. Android's document picker copies the chosen `.glsl` file into the app's private `shaders/` folder. Return to the game and select its filename in **Settings → Shader**. Use the remove control beside a file to delete it. On Windows, use **Add shader file** to open the platform file picker.

Selecting, disabling, removing, or replacing the active shader is detected by the renderer and applied on the next presented frame. It does not require restarting the game. On Android, tap a shader row to select it; press and hold the row to open a separate removal confirmation. On Windows, use the row's remove control. The **Edit shader parameters** row opens a dedicated page for the active shader's annotated values. Shaders are stored in `shaders/`; `shaders/active.txt` records the selected basename. The renderer compiles changed source on the render thread and retires the previous presentation pipeline after submitted GPU work is idle.

## Vulkan GLSL interface

Use Vulkan GLSL 450:

```glsl
#version 450
layout(set = 0, binding = 0) uniform sampler2D t;
layout(set = 0, binding = 1) uniform sampler2D depthTex; // optional scene depth
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o;

void main() {
    o = texture(t, uv);
}
```

- `t` is the resolved game image.
- `depthTex` is optional scene depth paired with the presented colour target. It contains raw device depth in the range 0..1, not linearized eye-space distance. Sample its value from the `.r` component.
- `uv` is the fullscreen texture coordinate shared by the colour and depth inputs.
- `o` is the final output pixel colour.
- The fragment entry point must be `main`; source files are limited to 2 MB.

When a matching scene depth target is available, the renderer binds it to `depthTex` and resolves supported multisampled depth before presentation. A 1×1 fallback texture is bound when depth capture or view creation is unavailable. With a custom GLSL shader active in SDR, the log reports `[shader] depthTex: BOUND` or `FALLBACK`, the rejection reason, target dimensions and formats, sample count, depth aspect, and active colour attachments. These diagnostic messages are emitted when the reported state changes, rather than every frame.

Raw device depth must be linearized by the shader before using it as eye-space distance. Do not assume a near/far plane, forward/reversed-Z convention, or identical depth availability in every menu, loading, cutscene or gameplay scene. This input does not provide a dedicated UI mask, camera near/far uniforms, or a separate depth prepass. The presentation shader receives the final game colour image, so game HUD pixels already composited into `t` cannot be identified with certainty and may be blurred. The Forge overlay is drawn afterward. A reliable HUD exclusion feature requires a distinct UI mask/layer from the renderer; colour-edge detection is only a visual approximation and can also preserve edges in the 3D scene.

## Editable shader parameters

A shader can expose up to 16 controls to **Settings → Shader**. Add one annotation comment per parameter:

```glsl
#version 450

// @param saturation float 1.18 0.0 2.0 0.01 Saturation
// @param enabled bool 1 0 1 1 Enable effect
// @param quality int 2 1 4 1 Quality
```

The format is:

```text
// @param <name> <type> <default> <minimum> <maximum> <step> <display label>
```

Supported types are `float`, `int`, and `bool`. Names must be GLSL-style identifiers. The label is shown in the menu; if omitted, the parameter name is used. A float or integer appears as an adjustable number row; a boolean appears as a toggle. Values are clamped to the declared range. Integer values and steps are rounded to whole numbers. Use a minimum lower than the maximum and a positive step.

The app injects a uniform block at **set 0, binding 2** when at least one valid annotation exists. Do not declare another resource at that binding in the same shader. The app also defines a macro for each parameter; use `PARAM_<name>` in GLSL rather than reading the injected block yourself:

```glsl
#version 450
// @param saturation float 1.18 0.0 2.0 0.01 Saturation

layout(set = 0, binding = 0) uniform sampler2D t;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o;

void main() {
    vec4 color = texture(t, uv);
    float luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec3 result = mix(vec3(luma), color.rgb, PARAM_saturation);
    o = vec4(clamp(result, 0.0, 1.0), color.a);
}
```

Parameter changes are sent through a Vulkan uniform buffer on the next presented frame. They do not recompile the shader or rebuild its pipeline. Each shader's values are saved separately in a sidecar file named `<shader>.params` (for example, `saturation.glsl.params`) and restored when that shader is loaded again. **Reset shader parameters** restores the defaults declared in the GLSL source. Parameters do not rewrite the original `.glsl` file.

Only annotated `float`, `int`, and `bool` values are exposed by this interface. Arbitrary GLSL local variables and user-defined uniform blocks are not automatically reflected into controls.



The repository includes [an MHGU World-style DoF example with 16 editable controls](../examples/glsl/mhgu_world_style_v5_3_dof_controls.glsl). Its `HUD_EDGE_PROTECT` option reduces blur around strong local colour edges to help preserve text and icon outlines. This is an edge-aware approximation, not a true HUD mask; adjust it conservatively and expect some scene edges to be preserved as well.

## ReShade compatibility status

ReShade support is **not implemented**. A ReShade `.fx` file is effect source, while a ReShade `.ini` file is a preset that selects techniques and stores their settings; presets can also depend on referenced effect files, includes and texture assets. Correct support requires the ReShade FX parser/code generator, uniform and texture management, technique/pass scheduling, intermediate render targets, and preset loading. Simply accepting `.fx` or `.ini` extensions would not load MHGU PC shaders correctly.

The current user-shader path remains single-pass Vulkan GLSL. It does not parse ReShade `.fx` / `.fxh`, load ReShade `.ini` presets, or execute multi-pass techniques. In SDR, an active custom shader replaces the built-in colour/PS1 presentation filters; HDR uses the built-in HDR path.
