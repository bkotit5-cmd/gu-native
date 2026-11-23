# User GLSL shaders

The Shader page can import and select a custom **single-pass Vulkan GLSL fragment shader**.

## Loading one

On Android, open **Settings → Shader → Load GLSL shader**, choose a GLSL file, then press **Use** beside it. Close the manager with **Restart the game** to apply it. Imported shaders live in the app's private shaders/ folder. On desktop, the Shader page opens a GLSL file picker and selects the chosen file; restart the app after importing.

The active shader is recorded as the filename in shaders/active.txt. Select **Disable** in the Shader manager to return to the built-in presentation shader.

## Required GLSL interface

Use Vulkan GLSL 450 and this interface:

\`\`\`glsl
#version 450
layout(set = 0, binding = 0) uniform sampler2D t;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o;

void main() {
    o = texture(t, uv);
}
\`\`\`

- **t** is the resolved game image.
- **uv** is the fullscreen texture coordinate.
- **o** is the final colour for the pixel.
- The entry point must be **main**.

The shader is compiled by the app's existing shaderc compiler for the active Vulkan 1.1/1.2 device. Source files are limited to 2 MB. Compilation or pipeline errors are written to the app log; the renderer falls back to the built-in presentation shader rather than intentionally terminating the game.

## Current limits

This first integration supports one fragment pass and one sampled input image. It does not parse ReShade FX files, RetroArch slang shaders, multi-pass chains, external texture inputs, custom uniforms, or include files. Those formats require conversion or a larger shader framework.

When a user GLSL shader is active in SDR, it replaces the presentation colour/PS1 filters. HDR presentation bypasses the custom shader and uses the built-in HDR path. Disable the custom shader to restore the standard presentation filters.
