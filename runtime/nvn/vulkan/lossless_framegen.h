#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called from GameActivity whenever the persistent, user-selected Lossless.dll path is known.
// The Windows PE is parsed as data by the Android LSFG backend; it is never dlopen()'d.
void lossless_framegen_set_dll_path(const char *path);
bool lossless_framegen_has_dll(void);
bool lossless_framegen_library_available(void);
void lossless_framegen_get_dll_path(char *out, size_t capacity);

#ifdef __cplusplus
}
#endif
