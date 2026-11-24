# MHGU Recompiled — Android and Windows PC

A native Android and Windows PC port of **Monster Hunter Generations Ultimate (MHGU)**. The game is recompiled to run directly on the device; this is not an emulator.

This repository is a fork of [khorne-hub/gu-native](https://github.com/khorne-hub/gu-native) by KHORNE, which is itself based on [MHGURecomp](https://github.com/MHGU-Recomp/MHGURecomp). This fork adds Android app/runtime integrations and must be used with your own legally obtained game files.

> **Game version:** the runtime targets MHGU **version 1.4.0**. Cheat files must match build ID `FB08F1D20FD1204F`.
>
> **No game assets are included.** This repository does not provide the game, ROMs, keys, or copyrighted game files.

## Current app menu

The settings interface is organized into seven main sections:

| Section | What it contains |
| --- | --- |
| **Controls** | Touch layout, controller mapping, combined buttons, keyboard and mouse settings, control profiles |
| **Graphics & Audio** | Resolution, render scale, upscaling, anti-aliasing, performance options, HUD settings, sound |
| **Shader** | Installed GLSL shader selection and management, live parameters, depth input and shader import |
| **Multiplayer** | Internet hubs, local network, emulator crossplay, chat and friends |
| **Mods & Cheats** | Mod load order, conflicts, cheats, DLC/event content, Forge and texture-pack information |
| **System** | App behaviour, app version and updates, game folder, save tools, game-file verification and game-code builder |
| **Credits & Licenses** | Project credits and third-party licenses |

Some settings and implementation code remain in the runtime but are not separate top-level pages in the current menu. This guide describes the current menu and active integrations, not every internal configuration field.

## Controls and touch interface

### Android touch controls

- On-screen movement stick, camera drag, attack, evade, guard, sheathe and menu controls.
- Context-aware layouts for melee, bow and bowgun gameplay, including aiming, firing, ammo and item-bar controls.
- A menu navigation pad for operating the game's menus by touch.
- Lock-on control and target switching, where the game state allows it.
- A touch-layout editor to move, resize, hide and customize controls and HUD elements.
- Ten saved control-profile slots for complete touch-control and HUD layouts, including the app's layout and an emulator-style layout.
- Floating-stick behaviour, touch opacity, camera speed and smoothing, optional vibration, and a configurable lock-on control.
- Gyroscope aiming for bows and bowguns when a phone or connected controller provides a gyro.
- Camera shake strength, long-press hiding and slide-between-buttons behaviour.
- Custom button pictures: add PNG, JPG or WebP images and assign them to touch controls in the layout editor.

### Controllers, keyboard and mouse

- Gamepad support with configurable face-button presentation, editable mappings and a guided controller-mapping page.
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

The **Shader** page is the in-game manager for custom **single-pass Vulkan GLSL fragment shaders**. It lists installed `.glsl` files, marks the active shader, lets you select a shader, switch back to **Use built-in shader**, and edit annotated parameters on a separate **Edit shader parameters** page. On Android, press and hold a shader row to open its removal confirmation; Windows retains its remove control. **Add shader file** imports a `.glsl` file. On Android it opens Android's system document picker and copies the selected file into the app's `shaders/` directory; the picker is a small bridge activity, not a separate orange shader-management screen. Android shader imports are limited to 2 MiB. On Windows, the app uses the native SDL file dialog. Shader changes are detected by the render thread and applied by hot-reloading on the next presented frame, without restarting the game.

The renderer expects Vulkan GLSL 450. When a custom shader is active, `t` is the final game image including the 2D HUD. The renderer restores original HUD pixels after the shader using a mask built from the HUD quad transforms. The optional `hudMask` sampler at **set 0, binding 3** exposes that mask to shaders; 1 marks a HUD pixel and 0 marks scene. The mask uses slightly expanded quad bounds, so transparent areas inside a sprite rectangle may also be preserved. If a compatible mask cannot be captured for a frame, the renderer bypasses custom GLSL for that frame and presents the original image rather than risking a depth-blurred HUD:

```glsl
#version 450
layout(set = 0, binding = 0) uniform sampler2D t;
layout(set = 0, binding = 1) uniform sampler2D depthTex; // optional scene depth
layout(set = 0, binding = 3) uniform sampler2D hudMask;  // optional: red = HUD pixel
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o;

void main() {
    o = texture(t, uv);
}
```

- `t`: resolved final game image, including the HUD (binding 0).
- `depthTex`: optional scene depth (binding 1), raw device depth 0..1, not linearized eye-space distance. Sample the value from `.r`.
- `hudMask`: optional HUD mask (binding 3), red channel 1 for HUD and 0 for scene. Use it to reject HUD pixels from blur taps; binding 3 is reserved for this sampler.
- `uv`: fullscreen texture coordinates shared by colour, depth and mask.
- `o`: output pixel colour.
- Fragment entry point: `main`; source file limit: 2 MB.

When a matching scene depth target is available, the renderer binds it to `depthTex` and resolves supported multisampled depth before presentation. A 1×1 fallback texture is bound when depth capture or view creation is unavailable. With custom GLSL active in SDR, the log reports `[shader] depthTex: BOUND` or `FALLBACK` plus the reason, dimensions, formats, sample count, aspect and active colour attachments. Diagnostics are emitted when that reported state changes, not every frame.

Raw device depth must be linearized before using it as eye-space distance. Do not assume camera near/far planes, forward/reversed-Z, or depth availability in every scene. Camera near/far uniforms and a separate depth prepass are not provided. The DoF sample uses `hudMask` to avoid blurring HUD centres or sampling HUD pixels into nearby scene blur; the renderer then composites the original HUD back. The Forge overlay is drawn afterward.

### Live shader parameters

Expose up to 16 controls by adding comments to a GLSL file:

```glsl
#version 450
// @param saturation float 1.18 0.0 2.0 0.01 Saturation
// @param enabled bool 1 0 1 1 Enable effect
// @param quality int 2 1 4 1 Quality
```

Format: `// @param <name> <type> <default> <minimum> <maximum> <step> <display label>`. Supported types are `float`, `int` and `bool`; names are GLSL-style identifiers. Float/integer parameters appear as adjustable number rows; boolean parameters appear as toggles. Values are clamped to their ranges, and integer values/steps are rounded to whole numbers.

The app automatically injects a uniform block at **set 0, binding 2** when the shader has valid parameter annotations. Use `PARAM_<name>` in shader code, for example `PARAM_saturation`; do not declare another resource at binding 2. Parameter changes are sent through a Vulkan uniform buffer on the next presented frame, so they do not recompile the shader or rebuild its pipeline. Values are stored per shader in a sidecar file named `<shader>.params` (for example, `saturation.glsl.params`). **Reset shader parameters** restores the defaults in the source file without changing the original GLSL. Arbitrary local variables and custom uniform blocks are not automatically exposed; only annotated `float`, `int` and `bool` values are supported.

A compile or pipeline failure falls back to the built-in presentation shader and is logged. Custom GLSL replaces the built-in Colour/PS1 presentation filters in SDR; HDR output bypasses the custom shader.

**Not supported:** ReShade `.fx` / `.fxh` effects and `.ini` presets, RetroArch slang shaders, multipass chains, arbitrary uniform reflection and shader include files. ReShade requires a separate effect runtime or conversion to the supported single-pass GLSL interface.

See [the GLSL shader guide](docs/USER_GLSL_SHADERS.md), [the sample saturation shader](examples/glsl/saturation.glsl), and [the parameterized MHGU World-style DoF shader](examples/glsl/mhgu_world_style_v5_3_dof_controls.glsl). The DoF sample's `SCENE_EDGE_PROTECT` option is an optional edge safeguard for 3D scene details; HUD exclusion is handled by the renderer.

## Forge plugin support

The current integration includes a **baseline of Forge 2.0.0 graphics/plugin support**, including NVN lifecycle callbacks, swap-chain texture access/registration and additional ImDrawList polygon wrappers.

This should not be read as a guarantee that every Forge 2.0.0 plugin or every API is compatible. Plugin behaviour depends on the APIs a plugin uses and the runtime environment. Use plugins from sources you trust; native plugins can affect stability and may execute code with the app's permissions.

## Mods, cheats and extra content

- **Mods:** import mod folders or ZIP archives containing supported `romfs`, `exefs`, `cheats` or native-NX content. Mods can be enabled, disabled and ordered. Later mods win when they replace the same file; the app can identify conflicts and lets you choose which mod takes precedence.
- **Cheats:** import Atmosphère-style `.txt` cheat files and toggle supported codes in the Cheats page. Files for another game build are left out. Use files matching MHGU 1.4.0 / build ID `FB08F1D20FD1204F`.
- **DLC and event quests (Android):** the normal menu action imports event quests and downloads that you already own from a save or backup made by an emulator or console. The app supplies no DLC, event quests or other game content. A separate raw `DLC.dlcp` folder-import path is development-gated and is not presented as a normal public-release feature.
- **Texture packs:** treated as mods and loaded through the Mods page. Texture packs can increase memory use.
- **Forge:** a Forge page/entry is shown when a Forge plugin is active.

On Android, ordinary cheat, mod, shader and save imports use Android's system document picker; there is no intermediate custom import browser or installed-content browser. Cheat `.txt` files use `ACTION_OPEN_DOCUMENT`. For mods, a small native dialog lets you choose a `.zip` file or a mod folder, then opens the corresponding Android picker. The selected file or folder is copied into private temporary staging, validated, and passed to the existing installer. A native confirmation dialog appears after a successful mod import and lets you restart the game immediately or later. The DLC/event-quest import is a separate Android activity for importing content already present in one of your own saves. `FolderBrowser` remains for choosing the game and save directories, not for importing content. On Windows, import actions use native SDL file dialogs.

Only install mods, cheats and plugins from sources you trust. Cheats and executable patches can change game state or affect online play. In multiplayer, use cheats only with the agreement of everyone in the lobby.

## Multiplayer

The app includes:

- Local-network play and internet hubs.
- Hub discovery, server selection and optional matchmaking keys.
- Chat and friends features, plus optional Discord Rich Presence.
- Crossplay options for supported yuzu-family emulator rooms and Ryujinx LDN servers.
- Hub monster-HP scaling and Village-quest options.

Crossplay requires the supported game version (1.4.0) and compatible networking on the other players' side. When crossplay is selected, the game uses the original game behaviour for that mode; some of this port's hub-specific features, such as monster-HP scaling and Village quests in the Hub, are not available there.

The default internet hub is a community-operated service, not a server operated by this repository's maintainers. A hub server can see the connecting player's IP address. Review the online notice and connect only to servers you trust.

**Small-monster synchronization is off by default.** Its implementation remains under investigation because small monsters may fail to attack players in some circumstances.

## Saves and game files

- Choose the game folder used by the app.
- Verify game files against the version expected by the port. Verification reports missing, truncated, changed or mismatched files; it does not download or replace game files.
- Import and export saves. On Android, save import opens the system document picker for the `system` save file, validates it and stages it for the next game launch. Before replacing the current save, the app backs up the existing `system` and `system_backup` files under `MHGURecomp/save_backups/`.
- On Android, choose a save folder that can be shared with a sync service to move a save between devices.
- The Android app includes a game-code builder workflow.

Keep independent backups of important saves before changing game files, importing saves or installing mods.

## Diagnostics and Detailed Log

The shared `log.txt` includes a session-relative clock, a record of settings that differ from their defaults, settings changes made during the session, and frame-interval summaries once a minute. Each summary includes p50, p90 and p99 frame intervals, the longest interval, and the count of intervals over 100 ms.

Open **Settings → System → Your game → Detailed log** and enable it before reproducing a problem. It is **off by default**. When enabled, it adds extra diagnostic records for:

- Shader discovery/list refresh, compilation and compile-failure reporting.
- Shader import completion and refresh requests.
- HUD-mask capture and upload state.
- Vulkan presentation path, image target and relevant format/resolution changes.

The detailed traces are intended to record useful operations and state changes rather than repeatedly printing an unchanged status every frame. Important normal error messages remain available when Detailed Log is off. On Android, use **Share log** on the same **Your game** page; on Windows, use **Log folder** to open the log location. The log writer also limits log growth so long sessions do not create an unbounded log file.

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


## Android app version and updates

The repository-root `VERSION` file is the source of the app's visible version and Android `versionName`. `VERSION_CODE` is the Android package version code and must increase for every published update. The current source version is **1.0.1.3** (version code **18**).

On Android, open **Settings → System → Your game → App version & updates** to see the installed version and check the latest stable release from this repository. When a newer release contains an APK, the updater downloads it, checks the package ID, version name/code, file size and the release SHA-256 digest when GitHub provides one, then compares the APK signing certificate with the installed app. Only a compatible APK is handed to Android's package installer. Android still asks the player to approve installation; the app does not install updates silently.

The start-up notice also checks this repository's latest stable release at most once per day. It only announces a newer version and opens the release page; use the System updater to download and install the APK.

