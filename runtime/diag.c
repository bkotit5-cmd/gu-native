// Diagnostics for the log a player shares (diag.h). The owner, for 1.0.1: the log should ask for more than we think we
// need, so that one log answers most questions and it stops growing release after release.
#include "diag.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "rt.h"

#ifdef _WIN32
#define DIAG_ENV _environ
#else
extern char **environ;
#define DIAG_ENV environ
#endif

static uint64_t g_start_ns;

void diag_start(void) {
    if (!g_start_ns) g_start_ns = plat_now_ns();
}

void diag_clock_into(char *buf, size_t size) {
    uint64_t now = plat_now_ns();
    if (!g_start_ns) g_start_ns = now;  // (a line before main's call: the clock starts there)
    unsigned long long s = (now - g_start_ns) / 1000000000ull;
    snprintf(buf, size, "%llu:%02llu:%02llu", s / 3600, s / 60 % 60, s % 60);
}

const char *diag_clock(void) {
    static __thread char buf[24];
    diag_clock_into(buf, sizeof buf);
    return buf;
}

// ---- settings
void diag_mask_addresses(char *s) {
    // an IPv4 (four numbers of 1-3 digits with dots between, not inside a longer run of digits and dots): its last two
    // numbers become x
    for (char *p = s; *p; p++) {
        if (!isdigit((unsigned char)*p) || (p > s && (isdigit((unsigned char)p[-1]) || p[-1] == '.'))) continue;
        char *q = p, *third = NULL;
        int parts = 0;
        for (;;) {
            int digits = 0;
            while (isdigit((unsigned char)*q) && digits < 4) q++, digits++;
            if (digits == 0 || digits > 3) break;
            if (++parts == 4) break;
            if (*q != '.') break;
            q++;
            if (parts == 2) third = q;
        }
        if (parts != 4 || isdigit((unsigned char)*q) || *q == '.' || !third) continue;
        // [third, q) -> "x.x"
        size_t tail = strlen(q);
        memcpy(third, "x.x", 3);
        memmove(third + 3, q, tail + 1);
        p = third + 3 - 1;
    }
}

// What the log may show of a setting's value: names, keys and passwords only that they are set; addresses masked
static void shown_value(const ConfigSetting *s, const Config *c, char *out, size_t size) {
    config_format(s, c, out, size);
    static const char *const secret[] = {"Key", "RoomPassword", "RyujinxPassphrase", "Nickname"};
    for (size_t i = 0; i < sizeof secret / sizeof *secret; i++)
        if (!strcmp(s->section, "Multiplayer") && !strcmp(s->key, secret[i])) {
            snprintf(out, size, "%s", *out ? "(set)" : "(none)");
            return;
        }
    diag_mask_addresses(out);
}

static void key_name(const ConfigSetting *s, char *out, size_t size) {
    snprintf(out, size, "%s.%.*s", s->section, (int)strcspn(s->key, "|"), s->key);
}

// Items appended to "[settings] <title>" lines of up to ~220 characters
typedef struct { char line[400]; int len, items; const char *title; } Lines;
static void lines_add(Lines *l, const char *item) {
    size_t n = strlen(item);
    if (l->items && l->len + n > 220) {
        LOG("%s\n", l->line);
        l->items = 0;
    }
    if (!l->items) l->len = snprintf(l->line, sizeof l->line, "[settings] %s:", l->title);
    l->len += snprintf(l->line + l->len, sizeof l->line - l->len, "%s %s", l->items ? "," : "", item);
    if (l->len >= (int)sizeof l->line) l->len = (int)sizeof l->line - 1;
    l->items++;
}
static void lines_end(Lines *l, const char *none) {
    if (l->items) LOG("%s\n", l->line);
    else if (none) LOG("[settings] %s: %s\n", l->title, none);
}

void diag_log_settings(void) {
    Config def = config_defaults();
    Lines l = {.title = "not the defaults (the file and the environment)"};
    int n = 0;
    for (int i = 0; i < g_nsettings; i++) {
        const ConfigSetting *s = &g_settings[i];
        if (config_equal(s, &g_config, &def)) continue;
        char k[96], v[200], item[320];
        key_name(s, k, sizeof k);
        shown_value(s, &g_config, v, sizeof v);
        snprintf(item, sizeof item, "%s=%s", k, v);
        lines_add(&l, item);
        n++;
    }
    if (g_config.hp.quests) {  // (per quest HP factors: how many, not each)
        char item[64];
        snprintf(item, sizeof item, "Monster HP.Quest<n>: %d quest(s)", g_config.hp.quests);
        lines_add(&l, item);
        n++;
    }
    lines_end(&l, "none, every setting as it comes");
    LOG("[settings] %d of %d settings not the defaults\n", n, g_nsettings);
    // the variables that change the runtime's ways (MHGU_*: a test build's env.txt, a desktop's environment): where a
    // switch's value came from when it is not the default or the GPU family's rule the [perf] lines name
    Lines e = {.title = "the environment's MHGU_ variables"};
    for (char **v = DIAG_ENV; v && *v; v++)
        if (!strncmp(*v, "MHGU_", 5)) {
            char item[256];
            snprintf(item, sizeof item, "%s", *v);
            diag_mask_addresses(item);
            lines_add(&e, item);
        }
    lines_end(&e, "none");
}

void diag_settings_changed(const void *was_p, const void *now_p) {
    const Config *was = was_p, *now = now_p;
    char title[64];
    snprintf(title, sizeof title, "%s changed", diag_clock());
    Lines l = {.title = title};
    for (int i = 0; i < g_nsettings; i++) {
        const ConfigSetting *s = &g_settings[i];
        if (config_equal(s, was, now)) continue;
        char k[96], a[200], b[200], item[480];
        key_name(s, k, sizeof k);
        shown_value(s, was, a, sizeof a);
        shown_value(s, now, b, sizeof b);
        snprintf(item, sizeof item, "%s %s -> %s", k, a, b);
        lines_add(&l, item);
    }
    lines_end(&l, NULL);
}

// ---- the frames' intervals: 0.25 ms buckets up to 128 ms, then one for the rest
#define FBUCKETS 512
#define FBUCKET_NS 250000ull
static uint32_t g_fb[FBUCKETS + 1];
static uint64_t g_fmax, g_fn, g_fover;

void diag_frame_interval(uint64_t ns) {
    uint64_t b = ns / FBUCKET_NS;
    g_fb[b < FBUCKETS ? b : FBUCKETS]++;
    if (ns > g_fmax) g_fmax = ns;
    if (ns > 100000000ull) g_fover++;
    g_fn++;
}

static double percentile(double p) {
    uint64_t want = (uint64_t)(p * (double)g_fn), seen = 0;
    if (want >= g_fn) want = g_fn - 1;
    for (int b = 0; b <= FBUCKETS; b++) {
        seen += g_fb[b];
        if (seen > want) return b == FBUCKETS ? g_fmax / 1e6 : (b + 0.5) * FBUCKET_NS / 1e6;
    }
    return g_fmax / 1e6;
}

void diag_frame_summary(char *out, size_t size) {
    *out = 0;
    if (g_fn)
        snprintf(out, size, "frame intervals p50 %.1f, p90 %.1f, p99 %.1f, the longest %.1f ms; %llu over 100 ms",
                 percentile(0.5), percentile(0.9), percentile(0.99), g_fmax / 1e6, (unsigned long long)g_fover);
    memset(g_fb, 0, sizeof g_fb);
    g_fmax = g_fn = g_fover = 0;
}
