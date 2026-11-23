# MHGU Recompiled — Android and Windows PC

A native Android and Windows PC port of **Monster Hunter Generations Ultimate (MHGU)**. The game is recompiled to run directly on the device; this is not an emulator.

This repository is a feature fork/integration of [KHORNE's gu-native](https://github.com/khorne-hub/gu-native), itself based on [MHGURecomp](https://github.com/MHGU-Recomp/MHGURecomp). It contains app/runtime changes and must be used with your own legally obtained game files.

> **Game version:** the runtime targets MHGU **version 1.4.0**. Cheat files must match build ID `FB08F1D20FD1204F`.
>
> **No game assets are included.** This repository does not provide the game, ROMs, keys, or copyrighted game files.

## Current app menu

The settings interface is organized into seven main sections:

| Section | What it contains |
| --- | --- |
| **Controls** | Touch layout, controller mapping, combined buttons, keyboard and mouse settings, control profiles |
| **Graphics & Audio** | Resolution, render scale, upscaling, anti-aliasing, performance options, HUD settings, sound |
| **Shader** | Import a custom Vulkan GLSL fragment shader |
| **Multiplayer** | Internet hubs, local network, emulator crossplay, chat and friends |
| **Mods & Cheats** | Mod load order, conflicts, cheats, DLC/event content, Forge and texture-pack information |
| **System** | App behaviour, game folder, save tools, game-file verification and game-code builder |
| **Credits & Licenses** | Project credits and third-party licenses |

Some settings and implementation code remain in the runtime but are not separate top-level pages in the current menu. This guide describes the current menu and active integrations, not every internal configuration field.

## Controls and touch interface

### Android touch controls

- On-screen movement stick, camera drag, attack, evade, guard, sheathe and menu controls.
- Context-aware layouts for melee, bow and bowgun gameplay, including aiming, firing, ammo and item-bar controls.
- A menu navigation pad for operating the game's menus by touch.
- Lock-on control and target switching, where the game state allows it.
- A touch-layout editor to move, resize, hide and customize controls and HUD elements.
- Control profiles, floating-stick behaviour, touch opacity, camera speed/smoothing and optional vibration.
- Long-press hiding and slide-between-buttons behaviour can be configured.

### Controllers, keyboard and mouse

- Gamepad support with configurable face-button presentation and controller mapping.
- Keyboard and mouse support on Windows PC, with editable keybinds.
- Controller and keyboard bindings for custom combined inputs.

### 15 Combined Buttons

The app provides **15 configurable combined-button slots**. Each slot can trigger a combination of the game's inputs and can be assigned to a touch button, controller input or supported PC keybind. The button combinations are configurable; this is not a change to the game's native item or stack limits.

## Added in this integration

### Map Button

A touch button can show or hide the in-game minimap during a hunt. Its visibility is configurable, and its position can be adjusted in the touch-layout editor.

### HUD Size and Text Outline

- **HUD Size:** global quest-HUD scaling from 100% down to 60%, in 5% steps.
- **Text Outline:** adds a dark outline to quest dialogue and help-box text to improve readability. It does not outline every UI label or every piece of game text.

## Graphics and performance

The available options depend on the device, GPU, driver and platform. The app includes:

- Screen/render resolution, render scale, aspect ratio, field of view and HUD anchoring for wider screens.
- MSAA and FXAA anti-aliasing options.
- FidelityFX Super Resolution (FSR 3.1 and supported FSR 4 paths); XeSS is available on supported desktop configurations.
- Upscaler quality, sharpness and optional motion/reactive controls.
- VSync, frame-rate limit, shader prewarming and performance statistics.
- Low-end presets, plus individual controls for shadows, grass, particle effects and bloom/depth of field.
- PS1-style visual filtering and smooth-gradient rendering.
- Volume, background mute and reverb controls.

Upscaling and frame-generation paths are hardware/driver dependent. An option being present in the menu does not mean every GPU can run that mode; the renderer may report or use a fallback when a required capability is unavailable.

## Custom GLSL shaders

The **Shader** page can load a custom **single-pass Vulkan GLSL fragment shader** from a `.glsl` file. On Android, the shader manager lists imported shaders and lets you select, disable or remove them. On Windows, a file picker imports and selects a shader. Restart the game to apply a changed selection.

The renderer expects Vulkan GLSL 450 with this interface:

```glsl
#version 450
layout(set = 0, binding = 0) uniform sampler2D t;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o;

void main() {
    o = texture(t, uv);
}
```

- `t`: the resolved game image.
- `uv`: fullscreen texture coordinates.
- `o`: output pixel colour.
- The fragment entry point is `main`; the current renderer supports source files up to 2 MB.

A compile or pipeline failure falls back to the built-in presentation shader and is logged. Custom GLSL replaces the built-in Colour/PS1 presentation filters in SDR; HDR output bypasses the custom shader.

**Not supported:** ReShade `.fx` presets, RetroArch slang shaders, multipass chains, extra texture inputs, custom uniform interfaces or shader include files. These require conversion or a different shader framework.

See [the GLSL shader guide](docs/USER_GLSL_SHADERS.md) and [the sample saturation shader](examples/glsl/saturation.glsl).

## Forge plugin support

The current integration includes a **baseline of Forge 2.0.0 graphics/plugin support**, including NVN lifecycle callbacks, swap-chain texture access/registration and additional ImDrawList polygon wrappers.

This should not be read as a guarantee that every Forge 2.0.0 plugin or every API is compatible. Plugin behaviour depends on the APIs a plugin uses and the runtime environment. Use plugins from sources you trust; native plugins can affect stability and may execute code with the app's permissions.

## Mods, cheats and extra content

- **Mods:** import mod folders or ZIP archives containing supported `romfs`, `exefs`, `cheats` or native-NX content. Mods can be enabled, disabled and ordered. Later mods win when they replace the same file; the app can identify conflicts and lets you choose which mod takes precedence.
- **Cheats:** import Atmosphère-style `.txt` cheat files and toggle supported codes in the Cheats page. Files for another game build are left out. Use files matching MHGU 1.4.0 / build ID `FB08F1D20FD1204F`.
- **DLC and event content:** the app includes an importer for folders containing `DLC.dlcp` files. The importer is experimental; verify imported content in-game.
- **Texture packs:** treated as mods and loaded through the Mods page. Texture packs can increase memory use.
- **Forge:** a Forge page/entry is shown when a Forge plugin is active.

Only install mods, cheats and plugins from sources you trust. Cheats and executable patches can change game state or affect online play. In multiplayer, use cheats only with the agreement of everyone in the lobby.

## Multiplayer

The app includes:

- Local-network play and internet hubs.
- Hub discovery, server selection and optional matchmaking keys.
- Chat and friends features.
- Crossplay options for supported yuzu-family emulator rooms and Ryujinx LDN servers.
- Hub monster-HP scaling and Village-quest options.

Crossplay requires the supported game version (1.4.0) and compatible networking on the other players' side. When crossplay is selected, the game uses the original game behaviour for that mode; some of this port's hub-specific features, such as monster-HP scaling and Village quests in the Hub, are not available there.

The default internet hub is a community-operated service, not a server operated by this repository's maintainers. A hub server can see the connecting player's IP address. Review the online notice and connect only to servers you trust.

**Small-monster synchronization is off by default.** Its implementation remains under investigation because small monsters may fail to attack players in some circumstances.

## Saves and game files

- Choose the game folder used by the app.
- Verify game files against the version expected by the port. Verification reports missing, truncated, changed or mismatched files; it does not download or replace game files.
- Import and export saves. The current save is backed up before an imported save is applied.
- On Android, choose a save folder that can be shared with a sync service to move a save between devices.
- The Android app includes a game-code builder workflow.

Keep independent backups of important saves before changing game files, importing saves or installing mods.

## Compatibility and safety notes

- **Game build:** the runtime targets MHGU 1.4.0. Cheat compatibility is version-specific.
- **Forge and native plugins:** baseline support only; individual plugins are not guaranteed to work.
- **Custom shaders:** use shaders from trusted sources. A fallback handles shader compilation or pipeline errors, but it cannot guarantee that every GPU driver will behave correctly with arbitrary shader code.
- **GPU texture formats:** when a GPU lacks support for formats such as BCn or ASTC, the renderer can decode those textures on the CPU. This preserves a compatibility path but can increase CPU work, memory use and texture-upload time on affected devices.
- **DLC importing:** experimental.
- **No build/runtime validation is implied by this feature list.** Device-specific behaviour still needs to be tested on the intended hardware and game files.

## Downloads and development

Use the Releases page of this repository for builds published from this fork: [Releases](../../releases). Do not assume a build from another fork or mirror contains the same features or commits.

For the upstream project and its history, see [khorne-hub/gu-native](https://github.com/khorne-hub/gu-native). Development/build notes and third-party notices are kept in the repository.

## Legal

You must supply your own copy of Monster Hunter Generations Ultimate and all game files required by the port. This repository does not distribute game code, ROMs, encryption keys or other copyrighted game assets. Monster Hunter, Capcom and Nintendo are trademarks of their respective owners. This project is not affiliated with or endorsed by Capcom or Nintendo.
