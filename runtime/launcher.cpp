// The game folder screen on a desktop (runtime/gamefolder.c opens it when no folder is known, or the one known can't be
// played): Android's GameFolderActivity, in the settings menu's Hunter look, in a window of its own before the game's.
// When the folder has everything but the game code (mhgugame.dll missing, or from another version), it offers to build
// it here: the builder's page (Android's BuilderActivity; the work is runtime/pc_builder.c's). F1, System, Game code,
// Build the game code opens the program on that page. Dear ImGui with its SDL3 and SDL_Renderer backends
// (third_party/cimgui/imgui/backends), so it needs nothing of the game's Vulkan renderer. Mouse, keyboard and
// controller work it.
#ifndef __ANDROID__
#include <SDL3/SDL.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"

#include "gamefolder.h"
#include "pc_builder.h"
#include "platform.h"
#include "settings_locale.h"

namespace {
// the settings menu's Hunter theme (settings_panel.cpp): wine, the game's red header, gold
const ImU32 PANEL = IM_COL32(50, 25, 24, 255), HEADER = IM_COL32(108, 46, 46, 255), GOLD = IM_COL32(236, 212, 44, 255),
            GOLD_BRIGHT = IM_COL32(255, 232, 84, 255), TEXT = IM_COL32(242, 238, 232, 255), DIM = IM_COL32(196, 176, 166, 255),
            WARN = IM_COL32(255, 96, 72, 255), BRONZE = IM_COL32(96, 58, 34, 255), BRONZE_LIT = IM_COL32(150, 98, 46, 255),
            RED_RULE = IM_COL32(176, 34, 30, 255), GREEN_RULE = IM_COL32(80, 142, 54, 255), PLATE = IM_COL32(66, 34, 30, 255);
int lang;
const char *T(const char *en) { return settings_text(lang, en); }
// the guide's way by hand (README.md's "Create the game code by hand" for PC, README.es.md's "Crea el código del juego a
// mano"): the builder's page offers it, for a PC where the program can't build it
const char *const GUIDE_EN = "https://github.com/khorne-hub/gu-native#create-the-game-code-by-hand",
                  *const GUIDE_ES = "https://github.com/khorne-hub/gu-native/blob/main/README.es.md#crea-el-c%C3%B3digo-del-juego-a-mano";

struct Pick {
    std::atomic<int> state{0};  // 0 idle, 1 the dialog is open, 2 a folder came back, 3 checking, 4 checked
    std::string picked, utf8, native, why;
} P;

void SDLCALL picked(void *, const char *const *list, int) {
    if (list && list[0]) P.picked = list[0], P.state = 2;
    else P.state = 0;  // (cancelled)
}

// a button with the game's look: bronze, a gold edge, gold when it has the focus or the mouse
bool button(const char *label, ImVec2 size, bool primary) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(primary ? BRONZE_LIT : BRONZE));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(IM_COL32(176, 145, 24, 255)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(GOLD));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(GOLD));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);
    bool hit = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return hit;
}

// A row of buttons across width w, gap apart: all as wide when the longest label fits so, else each its label's room and
// the rest shared out (a Spanish label, "Crear el código del juego en esta PC", is twice an English one's)
std::vector<float> row_widths(const std::vector<const char *> &labels, float w, float gap) {
    size_t n = labels.size();
    float room = w - gap * (float)(n - 1), most = 0, sum = 0;
    std::vector<float> out;
    for (const char *l : labels) {
        float x = ImGui::CalcTextSize(l, NULL, true).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetFontSize();
        out.push_back(x), sum += x, most = SDL_max(most, x);
    }
    if (most * (float)n <= room || sum >= room)
        for (float &x : out) x = room / (float)n;
    else
        for (float &x : out) x += (room - sum) / (float)n;
    return out;
}

std::string fmt(const char *f, ...) SDL_PRINTF_VARARG_FUNC(1);
std::string fmt(const char *f, ...) {
    char b[1600];
    va_list ap;
    va_start(ap, f);
    SDL_vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}
std::string mb(int64_t bytes) { return fmt("%lld MB", (long long)(bytes >> 20)); }
std::string minutes(int64_t ms) {
    long long m = ms / 60000, s = ms / 1000 % 60;
    return m > 0 ? fmt("%lld min %02lld s", m, s) : fmt("%lld s", s < 0 ? 0 : s);
}

// ---- the builder's page (Android's BuilderActivity and BuilderText)
const char *stage_text(int s) {
    static const char *const names[PCB_STAGES] = {"Download the tools", "Read your game files", "Translate the game's code",
                                                  "Compile", "Install the game code"};
    return s >= 0 && s < PCB_STAGES ? T(names[s]) : "";
}
// The overall percentage: the stages weigh what they take on a PC (compiling is most of it); -1 when unknown
int percent(const PcbJob &j) {
    static const double from[PCB_STAGES] = {0, 4, 6, 25, 99}, span[PCB_STAGES] = {4, 2, 19, 74, 1};
    if (j.stage < 0 || j.stage >= PCB_STAGES) return -1;
    double part = j.total > 0 ? SDL_min(1.0, (double)j.done / (double)j.total) : 0;
    return (int)(from[j.stage] + span[j.stage] * part + .5);
}
// A tool's name from a step's detail ("download:zig@ziglang.org", "unpack:zig"), and its source ("" when none)
std::string tool_name(const char *detail) {
    const char *n = SDL_strchr(detail, ':');
    std::string name = n ? n + 1 : detail;
    if (name.find('@') != std::string::npos) name.resize(name.find('@'));
    return name == "python" ? "Python 3.14.9" : name == "zig" ? "Zig 0.13.0" : T("the builder's scripts");
}
std::string tool_source(const char *detail) {
    const char *at = SDL_strchr(detail, '@');
    return at ? at + 1 : "";
}
// One line for the page: what is going on now
std::string status_line(const PcbJob &j) {
    std::string s = stage_text(j.stage);
    if (j.stage == 0) {
        if (!SDL_strncmp(j.detail, "download:", 9))  // ("Downloading Zig 0.13.0 (ziglang.org): 12 / 75 MB")
            return fmt("%s %s (%s): %lld / %lld MB", T("Downloading"), tool_name(j.detail).c_str(), tool_source(j.detail).c_str(),
                       (long long)(j.done >> 20), (long long)(j.total >> 20));
        if (!SDL_strncmp(j.detail, "unpack:", 7)) return fmt("%s %s", T("Setting up"), tool_name(j.detail).c_str());
        return s;
    }
    if (j.stage == 3 && !SDL_strcmp(j.detail, "link")) return fmt("%s: %s", s.c_str(), T("linking"));
    if (j.total > 0 && (j.stage == 2 || j.stage == 3))
        return fmt("%s: %lld / %lld %s", s.c_str(), (long long)j.done, (long long)j.total, T(j.stage == 2 ? "functions" : "files"));
    return s;
}
// A failure in words the player can act on (the tools' own message follows it on the page)
const char *failure_text(const char *stage) {
    if (!SDL_strcmp(stage, "space"))
        return T("There isn't enough free space on the disk where MHGU is. Free some (it needs about 3 GB while it builds) and try again: it goes on from where it stopped.");
    if (!SDL_strcmp(stage, "download"))
        return T("The tools couldn't be downloaded. Check the internet connection and try again: the download goes on from where it stopped. When PyPI (files.pythonhosted.org) can't be reached, Zig comes from ziglang.org instead; if both are blocked, another network or DNS (such as 1.1.1.1) helps, or make the game code by hand (Open the guide).");
    if (!SDL_strcmp(stage, "tools"))
        return T("The builder's files that came with MHGU are missing or don't match it (builder_tools.zip). Unzip the whole MHGU-PC zip again over this folder.");
    if (!SDL_strcmp(stage, "dest"))
        return T("The game folder can't be written. Choose a folder you can write in (not in Program Files).");
    if (!SDL_strcmp(stage, "extract") || !SDL_strcmp(stage, "version") || !SDL_strcmp(stage, "mods"))
        return T("Your game files aren't the ones needed: exefs from the 1.4.0 update and romfs of the base game with 1.4.0 applied, from your own copy.");
    if (!SDL_strcmp(stage, "memory"))
        return T("The PC ran out of memory. Close other programs and try again: it goes on from where it stopped, with fewer compilers.");
    if (!SDL_strcmp(stage, "start"))
        return T("The builder's Python couldn't start. Your antivirus may have blocked or removed it: allow MHGU's builder folder, or Start over and delete the tools, and try again.");
    if (!SDL_strcmp(stage, "recompile") || !SDL_strcmp(stage, "compile") || !SDL_strcmp(stage, "link"))
        return T("The tool failed while building the code. Try again: it goes on from where it stopped. If your antivirus removed zig.exe, allow MHGU's builder folder. If it fails again, share builder_log.txt (beside MHGU.exe).");
    if (!SDL_strcmp(stage, "install"))
        return T("The game code couldn't be put in the game folder. Close the game if it is open in another window, check the space and the access to the folder, and try again.");
    return T("The build stopped. Try again: it goes on from where it stopped.");
}

// A mark for a stage, drawn (the font has no check mark): 1 done, 2 now, 3 failed, 0 to come
void stage_mark(ImDrawList *dl, ImVec2 c, float r, int kind) {
    if (kind == 1) {
        dl->AddLine(ImVec2(c.x - r * .8f, c.y), ImVec2(c.x - r * .2f, c.y + r * .7f), GOLD, r * .35f);
        dl->AddLine(ImVec2(c.x - r * .2f, c.y + r * .7f), ImVec2(c.x + r * .9f, c.y - r * .7f), GOLD, r * .35f);
    } else if (kind == 2) {
        dl->AddTriangleFilled(ImVec2(c.x - r * .6f, c.y - r * .8f), ImVec2(c.x - r * .6f, c.y + r * .8f), ImVec2(c.x + r * .8f, c.y), GOLD);
    } else if (kind == 3) {
        dl->AddLine(ImVec2(c.x - r * .7f, c.y - r * .7f), ImVec2(c.x + r * .7f, c.y + r * .7f), WARN, r * .35f);
        dl->AddLine(ImVec2(c.x - r * .7f, c.y + r * .7f), ImVec2(c.x + r * .7f, c.y - r * .7f), WARN, r * .35f);
    } else {
        dl->AddCircle(c, r * .35f, DIM, 0, r * .2f);
    }
}

// Text whose capital O with acute gets its accent: Dear ImGui's default font draws that letter as a plain O at the
// launcher's size ("CÓDIGO", "CÓMO VA" in the Spanish titles), so a stroke is drawn over it
void accented(ImDrawList *dl, ImVec2 at, ImU32 col, const char *s) {
    dl->AddText(at, col, s);
    float f = ImGui::GetFontSize(), w = ImGui::CalcTextSize("O").x;
    for (const char *p = s; (p = SDL_strstr(p, "Ó")); p += 2) {
        float x = at.x + ImGui::CalcTextSize(s, p).x;
        dl->AddLine(ImVec2(x + w * .38f, at.y + f * .02f), ImVec2(x + w * .72f, at.y - f * .2f), col, SDL_max(1.f, f * .1f));
    }
}
void heading(const char *title) {
    ImVec2 at = ImGui::GetCursorScreenPos();
    accented(ImGui::GetWindowDrawList(), at, GOLD, title);
    ImGui::Dummy(ImGui::CalcTextSize(title));
    ImGui::Spacing();
}
void kv(const char *label, const std::string &value, bool ok) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(GOLD));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ok ? TEXT : WARN));
    ImGui::TextWrapped("%s", value.c_str());
    ImGui::PopStyleColor();
}
void wrapped(const std::string &text, ImU32 col) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

// What the builder's page knows of the folder it builds for
struct Folder {
    std::string native, utf8;  // (the runtime's spelling, and UTF-8 for the texts and SDL's file functions)
    bool main = false, version = false, romfs = false;
    std::string id;            // its mhgugame.id's
    bool playable = false;     // nothing wrong with it (gamefolder_problem)
    std::string problem;
};
Folder look(const std::string &native) {
    Folder f;
    f.native = native;
    char u[1200];
    f.utf8 = plat_path_to_utf8(native.c_str(), u, sizeof u) ? u : native;
    if (SDL_IOStream *io = SDL_IOFromFile((f.utf8 + "/exefs/main").c_str(), "rb")) {
        f.main = true;
        unsigned char h[0x48] = {0};
        static const unsigned char want[8] = {0xfb, 0x08, 0xf1, 0xd2, 0x0f, 0xd1, 0x20, 0x4f};
        f.version = SDL_ReadIO(io, h, sizeof h) == sizeof h && !SDL_memcmp(h, "NSO0", 4) && !SDL_memcmp(h + 0x40, want, 8);
        SDL_CloseIO(io);
    }
    SDL_PathInfo info;
    f.romfs = SDL_GetPathInfo((f.utf8 + "/romfs").c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
    char id[64];
    pcb_folder_id(native.c_str(), id, sizeof id);
    f.id = id;
    char why[700];
    gamefolder_problem(native.c_str(), lang, why, sizeof why);
    f.problem = why;
    f.playable = !why[0];
    return f;
}

// ---- tests: MHGU_LAUNCHER_SCRIPT="open; build; wait compile 40; shot a.png; cancel; wait stopped; startover; build;
// wait done; play" works the screen as a player would (open: the folder screen's Build the game code on this PC; shot:
// the window to a PNG; wait: until the build gets there, at most 2 hours; sleep <ms>; ask cancel|close|startover|tools:
// that question shown, for a shot; the page's other buttons by their names: startover-tools, deletetools, back, exit)
std::vector<std::string> script_steps(const char *s) {
    std::vector<std::string> out;
    std::string all = s ? s : "", step;
    for (size_t i = 0; i <= all.size(); i++) {
        if (i == all.size() || all[i] == ';') {
            while (!step.empty() && step[0] == ' ') step.erase(0, 1);
            while (!step.empty() && step.back() == ' ') step.pop_back();
            if (!step.empty()) out.push_back(step);
            step.clear();
        } else {
            step += all[i];
        }
    }
    return out;
}
}  // namespace

// 1: a folder that can be played was chosen (native: the runtime's spelling of it; it is kept in game_folder.txt),
// 0: the player left, -1: no window could be made (the caller asks with the system's boxes instead)
extern "C" int launcher_choose_folder(const char *problem, const char *problem_folder, bool open_builder, char *native,
                                      size_t native_size) {
    lang = gamefolder_language();
    pcb_init();
    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale < 1.f) scale = 1.f;
    // (MHGU_HIDDEN=1, tests: the window drawn but not shown, as the game's; its shots still work)
    const char *hidden = SDL_getenv("MHGU_HIDDEN");
    SDL_Window *win = SDL_CreateWindow("MHGU", (int)(880 * scale), (int)(600 * scale),
                                       SDL_WINDOW_HIGH_PIXEL_DENSITY | (hidden && *hidden == '1' ? SDL_WINDOW_HIDDEN : 0));
    if (!win) return -1;
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    if (!ren) {
        SDL_DestroyWindow(win);
        return -1;
    }
    SDL_SetRenderVSync(ren, 1);
    ImGuiContext *previous = ImGui::GetCurrentContext();
    ImGuiContext *ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL, io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGuiStyle &st = ImGui::GetStyle();
    st.WindowRounding = 0, st.FrameRounding = 3, st.WindowBorderSize = 0, st.FramePadding = ImVec2(14, 8);
    st.ChildRounding = 6, st.PopupRounding = 6, st.PopupBorderSize = 2;
    st.Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(TEXT);
    st.Colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(PANEL);
    st.Colors[ImGuiCol_ChildBg] = ImGui::ColorConvertU32ToFloat4(PLATE);
    st.Colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(PLATE);
    st.Colors[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(GOLD);
    st.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, .6f);
    st.Colors[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(BRONZE);
    st.Colors[ImGuiCol_PlotHistogram] = ImGui::ColorConvertU32ToFloat4(GOLD);
    st.Colors[ImGuiCol_NavCursor] = ImGui::ColorConvertU32ToFloat4(GOLD_BRIGHT);
    st.ScaleAllSizes(scale);
    st.FontScaleMain = 1.4f * scale;
    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer3_Init(ren);

    std::string status = problem && *problem ? problem : "";
    bool status_bad = !status.empty();
    bool no_lib = gamefolder_lib_missing(status.c_str(), lang);  // (mhgugame.dll missing or old: the builder's button too)
    std::string lib_folder = (no_lib || open_builder) && problem_folder ? problem_folder : "";  // (the folder it builds for)
    int result = 0;
    bool running = true;
    std::thread checker;
    bool page = open_builder && !lib_folder.empty();  // (the builder's page)
    Folder bf;
    if (page) bf = look(lib_folder);
    PcbJob job;
    PcbInfo info;
    std::string confirm;  // the question open over the page: "cancel", "close", "startover", "tools" ("": none)
    bool want_close = false;
    // (tests: MHGU_LAUNCHER_SHOT=<file.png> saves the screen after a moment, once a folder being checked is done, and
    // leaves; MHGU_LAUNCHER_PICK=<folder> answers the folder dialog with that folder, once; MHGU_LAUNCHER_SCRIPT above)
    const char *test_shot = SDL_getenv("MHGU_LAUNCHER_SHOT"), *test_pick = SDL_getenv("MHGU_LAUNCHER_PICK");
    std::vector<std::string> script = script_steps(SDL_getenv("MHGU_LAUNCHER_SCRIPT"));
    size_t script_at = 0;
    uint64_t script_since = 0;
    std::string shot_now;  // (a shot asked for this frame)
    int64_t looked_at = -1;  // (the build whose end the page has read the folder again for)
    int frame = 0;
    while (running) {
        pcb_snapshot(&job);
        pcb_info(&info);
        bool building = pcb_running(), key_back = false;
        if (test_pick && *test_pick && frame == 20) P.picked = test_pick, P.state = 2, test_pick = NULL;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            bool close = e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(win));
            // Esc, or a controller's B, is Exit (Back on the builder's page), as a dialog's Cancel (nothing is lost: the
            // game asks again when opened). B by its label, at the right of the face buttons (an Xbox controller's B,
            // a PlayStation's circle): there ImGui takes it for its own Back, where a Switch controller has its A. Not
            // while the system's folder dialog is open, whose Esc is its own, nor over a question (its own Cancel)
            bool esc = e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_ESCAPE;
            bool back = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST;
            if (back) {
                SDL_GamepadButtonLabel label =
                    SDL_GetGamepadButtonLabelForType(SDL_GetGamepadTypeForID(e.gbutton.which), SDL_GAMEPAD_BUTTON_EAST);
                back = label == SDL_GAMEPAD_BUTTON_LABEL_B || label == SDL_GAMEPAD_BUTTON_LABEL_CIRCLE;
            }
            if (close) {
                if (building) confirm = "close";  // (a build running: asked first; what it did is kept)
                else running = false;
            } else if ((esc || back) && P.state != 1 && confirm.empty()) {
                if (page) key_back = true;  // (the page's Back: not while it builds)
                else running = false;
            }
        }
        if (want_close && !building) running = false;  // (closing during a build: once it has stopped)
        if (P.state == 2) {  // (a folder came back from the dialog: checked away from this loop, romfs is 35,531 files)
            P.state = 3;
            status = T("Checking the files..."), status_bad = false, no_lib = false;
            if (checker.joinable()) checker.join();
            checker = std::thread([] {
                char utf8[1100], nat[1100], why[700];
                bool ok = gamefolder_check_picked(P.picked.c_str(), lang, utf8, sizeof utf8, nat, sizeof nat, why, sizeof why);
                P.utf8 = utf8, P.native = nat, P.why = ok ? "" : why;
                P.state = 4;
            });
        }
        if (P.state == 4) {
            P.state = 0;
            if (P.why.empty()) {
                gamefolder_remember(P.utf8.c_str());
                SDL_strlcpy(native, P.native.c_str(), native_size);
                result = 1, running = false;
            } else {
                status = P.why, status_bad = true, no_lib = gamefolder_lib_missing(P.why.c_str(), lang);
                lib_folder = no_lib ? P.native : "";
                if (no_lib) gamefolder_remember(P.utf8.c_str());  // (the player's folder: the next start knows it)
            }
        }
        // what the page's buttons (or the test script) ask for this frame
        bool act_open = false, act_build = false, act_cancel = false, act_play = false, act_back = key_back, act_startover = false,
             act_tools = false, act_deltools = false;
        if (script_at < script.size() && P.state == 0) {
            const std::string &stp = script[script_at];
            uint64_t now = SDL_GetTicks();
            if (!script_since) script_since = now;
            bool next = true;
            if (stp == "open") act_open = true;
            else if (stp == "build") act_build = true;
            else if (stp == "cancel") act_cancel = true;
            else if (stp == "play") act_play = true;
            else if (stp == "back") act_back = true;
            else if (stp == "startover") act_startover = true;
            else if (stp == "startover-tools") act_tools = true;
            else if (stp == "deletetools") act_deltools = true;
            else if (stp == "exit") running = false;
            else if (!stp.compare(0, 4, "ask ")) confirm = stp.substr(4);  // (a question shown, for its shot)
            else if (!stp.compare(0, 5, "shot ")) shot_now = stp.substr(5);
            else if (!stp.compare(0, 6, "sleep ")) next = now - script_since >= (uint64_t)SDL_atoi(stp.c_str() + 6);
            else if (!stp.compare(0, 5, "wait ")) {
                char what[32] = "";
                int n = 0;
                SDL_sscanf(stp.c_str() + 5, "%31s %d", what, &n);
                std::string w = what;
                next = w == "done"        ? job.status == PCB_DONE
                       : w == "stopped"   ? !building && job.status != PCB_RUNNING
                       : w == "failed"    ? job.status == PCB_FAILED
                       : w == "python"    ? job.stage == 0 && !SDL_strncmp(job.detail, "download:python", 15) && job.done >= ((int64_t)n << 20)
                       : w == "zig"       ? job.stage == 0 && !SDL_strncmp(job.detail, "download:zig", 12) && job.done >= ((int64_t)n << 20)
                       : w == "recompile" ? job.stage == 2 && job.done >= n
                       : w == "compile"   ? job.stage == 3 && job.total > 0 && job.done >= n
                                          : true;
                if (!next && now - script_since > 2 * 3600 * 1000ull) next = true;  // (never forever)
            }
            if (act_build || act_cancel || act_play || act_back || act_startover || act_tools || act_deltools) confirm.clear();
            if (next) {
                SDL_Log("[launcher] script: %s", stp.c_str());
                script_at++, script_since = 0;
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        float f = ImGui::GetFontSize(), band = f * 3.2f, pad = f * 1.2f, W = vp->Size.x;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));  // (the text's margins, all of it)
        ImGui::Begin("##folder", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        // the header: the game's red band and its red and green rules, a gold title
        dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + W, vp->Pos.y + band), HEADER);
        dl->AddRectFilled(ImVec2(vp->Pos.x, vp->Pos.y + band), ImVec2(vp->Pos.x + W, vp->Pos.y + band + 3 * scale), RED_RULE);
        dl->AddRectFilled(ImVec2(vp->Pos.x, vp->Pos.y + band + 3 * scale), ImVec2(vp->Pos.x + W, vp->Pos.y + band + 5 * scale), GREEN_RULE);
        accented(dl, ImVec2(vp->Pos.x + pad, vp->Pos.y + (band - f) * .5f), GOLD,
                 T(page ? "MHGU: BUILD THE GAME CODE" : "MHGU: YOUR GAME FOLDER"));
        float bh = f * 2.2f, by = vp->Size.y - pad - bh;
        if (!page) {
            ImGui::SetCursorPos(ImVec2(pad, band + pad));
            ImGui::PushTextWrapPos(W - pad);
            ImGui::TextUnformatted(T("Choose the folder with your own copy of the game. It must hold:"));
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            ImGui::Indent(pad);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(GOLD));
            ImGui::TextUnformatted("MHGU");
            ImGui::PopStyleColor();
            const char *tree[][2] = {{"|- exefs", "the game's exefs, version 1.4.0"},
                                     {"|- romfs", "its romfs with the 1.4.0 update applied (35,531 files)"},
                                     {"|- mhgugame.dll", "the game code for PC, built here"},
                                     {"'- mhgugame.id", ""}};
            for (auto &t : tree) {
                ImGui::TextUnformatted(t[0]);
                if (*t[1]) {
                    ImGui::SameLine(pad * 2 + ImGui::CalcTextSize("|- mhgugame.dll").x + f);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(DIM));
                    ImGui::TextUnformatted(T(t[1]));
                    ImGui::PopStyleColor();
                }
            }
            ImGui::Unindent(pad);
            ImGui::Spacing();
            ImGui::PushTextWrapPos(W - pad);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(DIM));
            ImGui::TextUnformatted(T("mhgugame.dll and mhgugame.id don't come in the download: they are made once on this PC from your own "
                                     "files. Once the folder is chosen, Build the game code on this PC makes them. Nothing is copied: "
                                     "the game reads that folder."));
            ImGui::PopStyleColor();
            ImGui::Spacing();
            if (!status.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(status_bad ? WARN : GOLD));
                ImGui::TextUnformatted(status.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::PopTextWrapPos();
            // the buttons at the bottom (a third between them, the builder, when the folder lacks only the game code)
            bool can_build = no_lib && !lib_folder.empty();
            std::vector<const char *> labels = {T("Choose folder")};
            if (can_build) labels.push_back(T("Build the game code on this PC"));
            labels.push_back(T("Exit"));
            std::vector<float> bw = row_widths(labels, W - pad * 2, pad);
            float x = pad;
            ImGui::SetCursorPos(ImVec2(x, by));
            bool busy = P.state != 0;
            ImGui::BeginDisabled(busy);
            if (button(labels[0], ImVec2(bw[0], bh), !can_build) && !busy) {
                P.state = 1;
                char start[1100];
                gamefolder_saved(start, sizeof start);
                SDL_ShowOpenFolderDialog(picked, NULL, win, start[0] ? start : NULL, false);
            }
            ImGui::EndDisabled();
            x += bw[0] + pad;
            if (can_build) {
                ImGui::SameLine(x);
                if (button(labels[1], ImVec2(bw[1], bh), true)) act_open = true;
                x += bw[1] + pad;
            }
            ImGui::SameLine(x);
            if (button(labels.back(), ImVec2(bw.back(), bh), false)) running = false;
            if (act_open && can_build) page = true, bf = look(lib_folder);
        } else {
            // ---- the builder's page
            ImGui::SetCursorPos(ImVec2(pad, band + pad * .8f));
            ImGui::PushTextWrapPos(W - pad);
            ImGui::TextUnformatted(T("The game code is made once, on this PC, from your own copy. Nothing is sent anywhere."));
            ImGui::PopTextWrapPos();
            bool ran = job.status != PCB_IDLE || info.interrupted;
            bool active = job.status == PCB_RUNNING;
            bool done = job.status == PCB_DONE;
            float top = ImGui::GetCursorPosY() + f * .4f;
            // (before a first build the panels take the bar's and the build's lines' room too)
            float tail_h = ran ? f * 2.6f : 0, bar_h = ran ? f * .7f : 0;
            float bottom = by - pad * .4f - tail_h - bar_h - f * 1.7f;
            float colw = (W - pad * 3) / 2, colh = bottom - top;
            // left: what it uses
            ImGui::SetCursorPos(ImVec2(pad, top));
            ImGui::BeginChild("##uses", ImVec2(colw, colh), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
            heading(T("WHAT IT USES"));
            kv(T("Folder:"), bf.utf8.empty() ? std::string(T("none chosen")) : bf.utf8, !bf.utf8.empty());
            kv("exefs/main:", !bf.main ? T("missing") : bf.version ? T("version 1.4.0") : T("not version 1.4.0"), bf.main && bf.version);
            kv("romfs:", bf.romfs ? T("found") : T("missing"), bf.romfs);
            kv(T("Makes:"), T("mhgugame.dll (+ .id), in the folder"), true);
            kv(T("Now:"), bf.id.empty()                ? std::string(T("no game code yet"))
                          : bf.id == pcb_wanted_id()   ? fmt("%s (%s)", T("up to date"), bf.id.c_str())
                                                       : fmt("%s (%s)", T("another version's"), bf.id.c_str()),
               true);
            if (!active && info.used > (1 << 20)) {
                ImGui::Spacing();
                if (button(fmt("%s (%s)###deltools", T("Delete the tools"), mb(info.used).c_str()).c_str(), ImVec2(0, f * 1.9f), false))
                    confirm = "tools";
            }
            ImGui::EndChild();
            // right: what it needs (before a build), or how it goes
            ImGui::SetCursorPos(ImVec2(pad * 2 + colw, top));
            // (it scrolls: a failure's words and its details can be longer than the panel, in Spanish more so)
            ImGui::BeginChild("##goes", ImVec2(colw, colh), ImGuiChildFlags_AlwaysUseWindowPadding);
            if (!ran) {
                heading(T("WHAT IT NEEDS"));
                if (info.to_download > 0)
                    wrapped(fmt(T("- The first time it downloads its tools, about %s: Python (python.org) and Zig (PyPI, or ziglang.org when PyPI can't be reached), free software, checked before they are used. Nothing of the game is ever downloaded."),
                                mb(info.to_download).c_str()), TEXT);
                else wrapped(T("- The tools are already on this PC."), TEXT);
                wrapped(fmt(T("- Space: %s free while it builds (there are %s). When it ends the temporary files go and the tools stay (about 300 MB)."),
                            mb(info.needed).c_str(), mb(info.free).c_str()),
                        info.free < info.needed ? WARN : TEXT);
                wrapped(T("- Time: about 5 to 30 minutes, depending on the PC. It uses all the processor's cores but one, at low priority: you can use the PC meanwhile."), TEXT);
                wrapped(T("- If it is cut (Cancel, the window closed, the PC restarted), it goes on from where it stopped."), TEXT);
            } else {
                heading(T("HOW IT GOES"));
                float r = f * .42f;
                for (int i = 0; i < PCB_STAGES; i++) {
                    int kind = done || i < job.stage ? 1 : i == job.stage && active ? 2 : i == job.stage && job.status == PCB_FAILED ? 3 : 0;
                    ImVec2 c = ImGui::GetCursorScreenPos();
                    stage_mark(ImGui::GetWindowDrawList(), ImVec2(c.x + r, c.y + f * .5f), r, kind);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + r * 2 + f * .5f);
                    std::string name = stage_text(i);
                    if (i == 0 && info.to_download == 0 && kind == 1) name += T(" (already here)");
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kind == 3 ? WARN : kind ? GOLD : TEXT));
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::Spacing();
                int64_t now = (int64_t)SDL_GetTicks();
                if (active) {
                    std::string eta;
                    if (job.stage == 3 && job.done >= 3 && job.total > job.done)
                        eta = fmt(T(" - about %s left"), minutes((now - job.stage_started_ms) * (job.total - job.done) / job.done).c_str());
                    wrapped(fmt("%s %s%s", T("Time:"), minutes(now - job.started_ms).c_str(), eta.c_str()), TEXT);
                    if (job.jobs > 0) wrapped(fmt(T("%d compilers at once, at low priority: you can use the PC meanwhile."), job.jobs), DIM);
                } else if (job.status == PCB_FAILED) {
                    wrapped(failure_text(job.error_stage), WARN);
                    if (job.error_text[0]) wrapped(fmt("%s %s", T("Details:"), job.error_text), DIM);
                } else if (done) {
                    wrapped(fmt("%s %s. %s", T("Done in"), minutes(job.ended_ms - job.started_ms).c_str(),
                                T("The game code is in your folder, beside exefs and romfs.")), TEXT);
                } else if (job.status == PCB_CANCELLED) {
                    wrapped(T("What it did is kept: Go on starts from there."), TEXT);
                } else if (info.interrupted) {
                    wrapped(T("The last build was interrupted: it goes on from where it stopped."), TEXT);
                }
            }
            ImGui::EndChild();
            // the bar, the status line and the build's last lines
            ImGui::SetCursorPos(ImVec2(pad, bottom + f * .5f));
            int pc = done ? 100 : ran ? percent(job) : 0;
            if (ran) ImGui::ProgressBar(SDL_max(0, pc) / 100.f, ImVec2(W - pad * 2, bar_h), "");
            bool ready = bf.main && bf.version && bf.romfs && info.scripts_ok;
            std::string line = active                         ? fmt("%s  (%d %%)", status_line(job).c_str(), SDL_max(pc, 0))
                               : job.status == PCB_FAILED    ? fmt("%s %s", T("Stopped at:"), stage_text(job.stage))
                               : job.status == PCB_CANCELLED ? std::string(T("Cancelled. You can go on whenever you like."))
                               : done                        ? std::string(T("The game code is ready."))
                               : !bf.main || !bf.romfs       ? std::string(T("exefs or romfs is missing from the folder."))
                               : !bf.version                 ? std::string(T("This exefs is not version 1.4.0."))
                               : !info.scripts_ok            ? std::string(T("builder_tools.zip is missing beside MHGU.exe: unzip the whole MHGU-PC zip again."))
                               : bf.playable                 ? std::string(T("The game code here is up to date. Build again makes it anew."))
                                                             : std::string(T("Press Build to start."));
            ImVec2 ls = ImGui::CalcTextSize(line.c_str());
            ImGui::SetCursorPos(ImVec2(SDL_max(pad, (W - ls.x) * .5f), bottom + f * .5f + bar_h + f * .35f));
            ImGui::PushTextWrapPos(W - pad);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(job.status == PCB_FAILED ? WARN : GOLD));
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            if (ran) {  // (the build's own words, its last two lines)
                char tail[2][160];
                int n = pcb_tail(tail, 2);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(DIM));
                ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * .8f);
                for (int i = 0; i < n; i++) {
                    ImGui::SetCursorPos(ImVec2(pad, by - pad * .4f - tail_h + i * ImGui::GetFontSize() * 1.2f));
                    ImGui::TextUnformatted(tail[i]);
                }
                ImGui::PopFont();
                ImGui::PopStyleColor();
            }
            // the buttons: Build / Go on / Cancel / Play, Start over (a build half done), the guide (by hand), Back
            bool half = info.half_done && !active && !done;
            int nb = 3 + (half ? 1 : 0);
            float bw = (W - pad * (nb + 1)) / nb;
            ImGui::SetCursorPos(ImVec2(pad, by));
            const char *main_label = active ? T("Cancel")
                                     : done ? T("Play")
                                     : job.status == PCB_FAILED || job.status == PCB_CANCELLED || info.half_done ? T("Go on")
                                     : bf.playable ? T("Build again")
                                                   : T("Build");
            ImGui::BeginDisabled(!active && !done && !ready);
            if (button(fmt("%s###main", main_label).c_str(), ImVec2(bw, bh), true)) {
                if (active) confirm = "cancel";
                else if (done) act_play = true;
                else act_build = true;
            }
            ImGui::EndDisabled();
            int slot = 1;
            if (half) {
                ImGui::SameLine(pad * (slot + 1) + bw * slot);
                if (button(T("Start over"), ImVec2(bw, bh), false)) confirm = "startover";
                slot++;
            }
            ImGui::SameLine(pad * (slot + 1) + bw * slot);
            if (button(T("Open the guide"), ImVec2(bw, bh), false)) {
                const char *url = lang == 1 ? GUIDE_ES : GUIDE_EN;
                if (!SDL_OpenURL(url)) SDL_Log("[launcher] the browser didn't open: %s", url);
            }
            slot++;
            ImGui::SameLine(pad * (slot + 1) + bw * slot);
            ImGui::BeginDisabled(active);
            if (button(T("Back"), ImVec2(bw, bh), false)) act_back = true;
            ImGui::EndDisabled();

            // the questions (Android's Look.notice): Cancel?, close while it builds, Start over?, Delete the tools?
            if (!confirm.empty() && !ImGui::IsPopupOpen("##ask")) ImGui::OpenPopup("##ask");
            ImGui::SetNextWindowSize(ImVec2(W * .7f, 0));
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + W * .5f, vp->Pos.y + vp->Size.y * .5f), ImGuiCond_Always, ImVec2(.5f, .5f));
            if (ImGui::BeginPopupModal("##ask", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
                bool stop = confirm == "cancel" || confirm == "close";
                heading(confirm == "cancel" ? T("CANCEL?") : confirm == "close" ? T("CLOSE?")
                        : confirm == "tools"  ? T("DELETE THE TOOLS")
                                              : T("START OVER?"));
                std::string body = stop ? std::string(T("What is done is kept: the next time it goes on from where it stopped."))
                                   : confirm == "tools"
                                       ? fmt(T("The tools and anything left from a build are deleted (%s). The game code you already made stays in your folder. If you build it again, they are downloaded again."),
                                             mb(info.used).c_str())
                                       : std::string(T("What this build has done so far is deleted, and the next one starts from the beginning. Your game folder, its files and any game code already made stay as they are. The downloaded tools stay too, unless you choose to delete them (a later build downloads them again)."));
                ImGui::PushTextWrapPos(0);
                ImGui::TextUnformatted(body.c_str());
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
                float aw = ImGui::GetContentRegionAvail().x;
                if (confirm == "startover") {
                    if (button(T("Start over"), ImVec2(aw, bh), true)) act_startover = true, confirm.clear();
                    if (button(T("Start over and delete the tools"), ImVec2(aw, bh), false)) act_tools = true, confirm.clear();
                } else if (confirm == "tools") {
                    if (button(T("Delete"), ImVec2(aw, bh), true)) act_deltools = true, confirm.clear();
                } else if (confirm == "cancel") {
                    if (button(T("Cancel the build"), ImVec2(aw, bh), true)) act_cancel = true, confirm.clear();
                } else if (confirm == "close") {
                    if (button(T("Stop and close"), ImVec2(aw, bh), true)) act_cancel = true, want_close = true, confirm.clear();
                }
                if (!confirm.empty() && (button(T(stop ? "Keep building" : "Cancel"), ImVec2(aw, bh), false) ||
                                         ImGui::IsKeyPressed(ImGuiKey_Escape)))
                    confirm.clear();
                if (confirm.empty()) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if (act_cancel && active) pcb_cancel();
            if (act_build && !active && !done && ready) pcb_start(bf.native.c_str());
            if (act_startover && !active) pcb_start_over(false);
            if (act_tools && !active) pcb_start_over(true);
            if (act_deltools && !active) pcb_delete_tools();
            if (done && job.ended_ms != looked_at) bf = look(bf.native), looked_at = job.ended_ms;  // (the new game code, once)
            if ((act_play && done) || (act_back && !active)) {
                Folder now = look(bf.native);
                if (now.playable && (act_play || open_builder || done)) {  // (Play, or Back to the game it can play now)
                    if (!bf.utf8.empty()) gamefolder_remember(bf.utf8.c_str());
                    SDL_strlcpy(native, bf.native.c_str(), native_size);
                    result = 1, running = false;
                } else {  // (Back to the folder screen, which says what is wrong)
                    page = false, status = now.problem, status_bad = !now.problem.empty();
                    no_lib = gamefolder_lib_missing(now.problem.c_str(), lang);
                }
            }
        }
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 50, 25, 24, 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
        if (!shot_now.empty()) {
            if (SDL_Surface *s = SDL_RenderReadPixels(ren, NULL)) SDL_SavePNG(s, shot_now.c_str()), SDL_DestroySurface(s);
            shot_now.clear();
        }
        if (test_shot && *test_shot && P.state != 3 && ++frame == 45) {
            SDL_Surface *s = SDL_RenderReadPixels(ren, NULL);
            if (s) SDL_SavePNG(s, test_shot), SDL_DestroySurface(s);
            running = false;
        } else if (!(test_shot && *test_shot)) {
            ++frame;
        }
        SDL_RenderPresent(ren);
        // (at most 60 frames a second: a hidden or minimized window's VSync doesn't wait, and the build wants the CPU)
        static uint64_t last_frame;
        uint64_t now_ns = SDL_GetTicksNS();
        if (now_ns - last_frame < 16000000ull) SDL_DelayNS(16000000ull - (now_ns - last_frame));
        last_frame = SDL_GetTicksNS();
    }
    if (pcb_running()) {  // (closed while it builds: the build stops, what it did is kept)
        pcb_cancel();
        for (int i = 0; i < 500 && pcb_running(); i++) SDL_Delay(20);
    }
    if (checker.joinable()) checker.join();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(previous);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    return result;
}
#endif
