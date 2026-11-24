#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// BC1 -> ETC2 RGBA1; BC2/BC3 -> ETC2 RGBA8. Output is tightly packed 4x4 blocks.
bool nvn_transcode_bcn_etc2(int kind, const uint8_t *blocks, uint32_t width, uint32_t height, bool srgb,
                            uint8_t *out, size_t out_size);

#ifdef __cplusplus
}
#endif
