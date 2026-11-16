// Diagnostics for the log a player shares (1.0.1): the session's clock on the lines that tell where it was and what it
// did, the settings that are not the defaults (and each change to them), and each minute's frame intervals. So one log
// answers what used to take a second one: which settings, where in the game, how the frames went before an error.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// (main.c, first thing) the session's start: diag_clock counts from here
void diag_start(void);
// "h:mm:ss" since diag_start, in a buffer of the calling thread's (valid until its next call)
const char *diag_clock(void);
// The same into `buf` (no thread-local storage: for a crash's report, in a signal handler)
void diag_clock_into(char *buf, size_t size);

// [settings] the settings that are not the defaults, a few to a line (main thread, after config_load); names, keys and
// passwords show only that they are set, and an address only its first two numbers
void diag_log_settings(void);
// (config_store.c, a save) what changed, from `was` to `now`, on one line
void diag_settings_changed(const void *was, const void *now);

// The frames' intervals, for the minute's line (one thread only: the one that calls both, the translating thread's
// backend_execute): each interval, then once a minute "frame intervals p50 16.7, p90 17.0, p99 33.4, the longest 120.3
// ms; 2 over 100 ms" (out: "" with none), which starts the next minute's count.
void diag_frame_interval(uint64_t ns);
void diag_frame_summary(char *out, size_t size);

// An address as the log may show it: an IPv4's first two numbers ("192.168.x.x"), everything else as it is (in place)
void diag_mask_addresses(char *s);

#ifdef __cplusplus
}
#endif
