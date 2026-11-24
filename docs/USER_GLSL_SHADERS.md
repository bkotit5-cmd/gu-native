# User GLSL shaders

Installed GLSL files are listed and selectable directly in the monochrome in-game **Settings → Shader** page. Select a filename to activate it, or choose **Use built-in shader** to disable custom post-processing. The orange Android shader activity is only the import/remove file manager.

## Switching without restarting

On Android, use **Import / manage shaders**, import or remove a `.glsl` file, then return to the game. Selecting, disabling, removing, or replacing the active shader is detected by the renderer and applied on the next presented frame. Closing the shader manager does not restart the game. On desktop, use **Import / add shader**; the selected file hot-reloads on the next presented frame.

Shaders are stored in `shaders/` (under the app's private files directory on Android). The active basename is recorded in `shaders/active.txt`. The renderer checks the active marker and file metadata, compiles changed source on the render thread, and retires the previous presentation pipeline after submitted GPU work is idle.

## Vulkan GLSL interface

Use Vulkan GLSL 450:

```glsl
#version 450
layout(set = 0, binding = 0) uniform sampler2D t;
layout(set = 0, binding = 1) uniform sampler2D depthTex; // optional
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o;

void main() {
    o = texture(t, uv);
}
```

- `t` is the resolved game image.
- `depthTex` is optional and exposes the scene depth attachment paired with the presented colour target. It contains raw device depth in the range 0..1, not linearized eye-space distance. A 1×1 fallback texture is bound when the renderer cannot identify a matching depth target.
- `uv` is the fullscreen texture coordinate.
- `o` is the final colour for the pixel.
- The entry point must be `main`.

The renderer captures the same scene depth attachment selected by the game pass, including its FSR output-size depth target when FSR scaling is active, and resolves supported multisampled depth before the presentation pass. Depth-aware blur and DoF can sample `depthTex`; shaders must linearize it and account for the game's depth convention if they need eye-space distance. This input does not provide a separate depth prepass or guarantee a matching depth attachment in every menu/UI scene.

The app compiles GLSL with shaderc for the active Vulkan 1.1/1.2 device. Source files are limited to 2 MB. Shader selection and source replacement trigger a render-thread reload; HDR presentation still bypasses custom GLSL.

## ReShade compatibility status

ReShade support is **not implemented by this change**. A ReShade `.fx` file is effect source, while a ReShade `.ini` file is a preset that selects techniques and stores their settings; presets also depend on referenced effect files, includes and texture assets. Correct support requires the ReShade FX parser/code generator, uniform and texture management, technique/pass scheduling, intermediate render targets, and preset loading. Simply accepting `.fx` or `.ini` extensions would not load MHGU PC shaders correctly.

The current user-shader path remains single-pass Vulkan GLSL. It does not yet parse ReShade `.fx` / `.fxh`, load ReShade `.ini` presets, or execute multi-pass techniques. In SDR, an active custom shader replaces the built-in colour/PS1 presentation filters; HDR uses the built-in HDR path.
