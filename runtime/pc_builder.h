// The game code made inside MHGU.exe (runtime/pc_builder.c): Windows' counterpart of the Android app's on-phone builder
// (BuilderService, BuilderTools). The game folder screen (runtime/launcher.cpp) shows it and works its buttons; F1,
// System, Game code, Build the game code opens that screen again (the program starts over into it).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// What a build is doing (pcb_snapshot copies it)
enum { PCB_IDLE, PCB_RUNNING, PCB_DONE, PCB_FAILED, PCB_CANCELLED };
// The stages, in order (tools/phone_build.py's names; "tools" is the downloads)
#define PCB_STAGES 5
extern const char *const pcb_stage_names[PCB_STAGES];  // tools, extract, recompile, compile, install

typedef struct {
    int status;            // PCB_*
    int stage;             // 0..PCB_STAGES-1
    char detail[32];       // "download:python", "unpack:zig", the build's own stage name
    int64_t done, total;   // how far the stage is (bytes, functions, files)
    int64_t started_ms, stage_started_ms, ended_ms;  // (SDL_GetTicks)
    char error_stage[16];  // the failure's kind: space, download, tools, dest, extract, recompile, compile, link,
                           // install, memory, start
    char error_text[400];
    char folder[1100];     // the game folder of the run (UTF-8)
    int jobs;              // compilers at once
} PcbJob;

// The tools' state and the room they need (bytes)
typedef struct {
    int64_t to_download;   // what the downloads still take (0: both in place)
    int64_t installed;     // what the tools take once unpacked (Python and Zig)
    int64_t used;          // what the builder's folder takes now (tools, work folder, downloads)
    int64_t needed;        // free space a run needs now (the tools still missing, the work folder)
    int64_t free;          // free space where the builder works
    bool half_done;        // something to Start over from: a work folder, a run cut, failed or cancelled
    bool interrupted;      // a run was going on when the program closed (state.txt)
    bool scripts_ok;       // builder_tools.zip is beside MHGU.exe
    char where[600];       // the builder's folder (UTF-8), for the texts
} PcbInfo;

// The builder's folder: builder\ in the program's own (beside the settings). Called once, on the main thread, before
// anything else here.
void pcb_init(void);
void pcb_snapshot(PcbJob *out);
// The build's last lines (not its @@ ones), newest last: up to max, each up to 160 bytes; how many
int pcb_tail(char (*lines)[160], int max);
void pcb_info(PcbInfo *out);
// A build of the folder (its runtime spelling, the system's code page): false when one runs already or the folder is
// missing. It goes on in the background: pcb_snapshot says how.
bool pcb_start(const char *game_native);
void pcb_cancel(void);
// Start over (as Android's): the work folder (and, with tools, the downloaded Python and Zig) set aside in one step and
// deleted on a thread of its own; state.txt goes. Never while a build runs (false). The game folder is not touched.
bool pcb_start_over(bool tools);
// Delete the tools and anything left of a build (the screen's "Delete the tools"): never while a build runs
bool pcb_delete_tools(void);
bool pcb_running(void);
// F1, System, Game code, Build the game code: the next start opens the builder (builder_open.txt beside the settings);
// pcb_take_open_request says so once (and removes the mark)
void pcb_request_open(void);
bool pcb_take_open_request(void);
// The game code id this program wants (GAMELIB_ID) and the one the folder's mhgugame.id says ("" when none)
const char *pcb_wanted_id(void);
void pcb_folder_id(const char *game_native, char *out, size_t size);
#ifdef __cplusplus
}
#endif
