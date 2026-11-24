// BC1/2/3 -> ETC2, for GPUs that expose ETC2 but not BC. Results are cached by SHA-256 of
// the linear BC blocks and exact conversion parameters; the cache may be deleted at any time.
#include "etc2_transcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <mutex>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define ETC2_MKDIR(p) _mkdir(p)
#else
#define ETC2_MKDIR(p) mkdir(p, 0755)
#endif

#include "bcn.h"
#include "Etc.h"
#include "EtcImage.h"
extern "C" {
#include "../../updater/updater.h"
}

namespace {
constexpr uint32_t CACHE_VERSION = 1;
constexpr uint32_t ETC2_RGBA1 = 1;
constexpr uint32_t ETC2_RGBA8 = 2;

struct CacheHeader {
    char magic[8];
    uint32_t version, kind, width, height, srgb, format;
    uint64_t output_bytes;
    char digest[65];
};

std::mutex cache_mutex;
std::mutex encode_mutex; // avoid several full-image float buffers competing for memory
std::atomic<uint64_t> temp_serial{0};
int cache_dir_state;

bool cache_dirs() {
    if (cache_dir_state) return cache_dir_state > 0;
    ETC2_MKDIR("cache");
    ETC2_MKDIR("cache/textranscode");
    struct stat st;
    cache_dir_state = stat("cache/textranscode", &st) == 0 && (st.st_mode & S_IFDIR) ? 1 : -1;
    return cache_dir_state > 0;
}

void make_digest(int kind, uint32_t width, uint32_t height, bool srgb,
                 const uint8_t *blocks, size_t bytes, char digest[65]) {
    // Increment CACHE_VERSION when the encoder policy or input interpretation changes.
    const uint32_t meta[6] = {CACHE_VERSION, static_cast<uint32_t>(kind), width, height, srgb ? 1u : 0u,
                              kind == BCN_BC1 ? ETC2_RGBA1 : ETC2_RGBA8};
    Sha256 hash;
    sha256_init(&hash);
    sha256_update(&hash, meta, sizeof(meta));
    sha256_update(&hash, blocks, bytes);
    sha256_hex(&hash, digest);
}

bool cache_read(const char *path, const CacheHeader &expected, uint8_t *out, size_t bytes) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    if (!cache_dirs()) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    CacheHeader h{};
    const bool ok = fread(&h, sizeof(h), 1, f) == 1 &&
        !memcmp(h.magic, expected.magic, sizeof(h.magic)) &&
        h.version == expected.version && h.kind == expected.kind &&
        h.width == expected.width && h.height == expected.height &&
        h.srgb == expected.srgb && h.format == expected.format &&
        h.output_bytes == expected.output_bytes && !strcmp(h.digest, expected.digest) &&
        fread(out, 1, bytes, f) == bytes && fgetc(f) == EOF && !ferror(f);
    fclose(f);
    if (!ok) remove(path);
    return ok;
}

void cache_write(const char *path, const CacheHeader &header, const uint8_t *data, size_t bytes) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    if (!cache_dirs()) return;
    struct stat st;
    if (stat(path, &st) == 0) return;
    char tmp[256];
    const uint64_t serial = temp_serial.fetch_add(1, std::memory_order_relaxed);
    if (snprintf(tmp, sizeof(tmp), "%s.tmp.%llu", path, static_cast<unsigned long long>(serial)) >=
        static_cast<int>(sizeof(tmp))) return;
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    bool ok = fwrite(&header, sizeof(header), 1, f) == 1 &&
              fwrite(data, 1, bytes, f) == bytes && fflush(f) == 0;
    if (fclose(f) != 0) ok = false;
    if (ok) {
        if (rename(tmp, path) != 0) remove(tmp);
    } else {
        remove(tmp);
    }
}
} // namespace

extern "C" bool nvn_transcode_bcn_etc2(int kind, const uint8_t *blocks, uint32_t width, uint32_t height,
                                       bool srgb, uint8_t *out, size_t out_size) {
    if (kind < BCN_BC1 || kind > BCN_BC3 || !blocks || !out || !width || !height) return false;
    const uint64_t block_count = ((static_cast<uint64_t>(width) + 3) / 4) *
                                 ((static_cast<uint64_t>(height) + 3) / 4);
    const uint32_t source_bpb = bcn_block_bytes(kind), target_bpb = kind == BCN_BC1 ? 8 : 16;
    if (block_count > SIZE_MAX / source_bpb || block_count > SIZE_MAX / target_bpb) return false;
    const size_t source_bytes = static_cast<size_t>(block_count * source_bpb);
    const size_t expected_bytes = static_cast<size_t>(block_count * target_bpb);
    if (out_size != expected_bytes ||
        static_cast<uint64_t>(width) * height > SIZE_MAX / (4 * sizeof(float))) return false;

    CacheHeader header{};
    memcpy(header.magic, "MHGUE2C", 7);
    header.version = CACHE_VERSION;
    header.kind = static_cast<uint32_t>(kind);
    header.width = width;
    header.height = height;
    header.srgb = srgb ? 1u : 0u;
    header.format = kind == BCN_BC1 ? ETC2_RGBA1 : ETC2_RGBA8;
    header.output_bytes = expected_bytes;
    make_digest(kind, width, height, srgb, blocks, source_bytes, header.digest);

    char path[160] = {};
    const int path_len = snprintf(path, sizeof(path), "cache/textranscode/%s.bin", header.digest);
    const bool have_path = path_len >= 0 && path_len < static_cast<int>(sizeof(path));
    if (have_path && cache_read(path, header, out, expected_bytes)) return true;

    std::lock_guard<std::mutex> one_encoder(encode_mutex);
    // Another worker may have completed the same transcode while this one waited for the memory-limited encoder slot.
    if (have_path && cache_read(path, header, out, expected_bytes)) return true;
    const size_t pixels = static_cast<size_t>(width) * height;
    float *rgba = static_cast<float *>(malloc(pixels * 4 * sizeof(float)));
    if (!rgba) {
        memset(out, 0, expected_bytes);
        return false;
    }
    bcn_decode_float_rgba(kind, blocks, width, height, rgba);

    bool ok = false;
    unsigned char *bits = nullptr;
    try {
        Etc::Image image(rgba, width, height, Etc::RGBA);
        const Etc::Image::Format format = kind == BCN_BC1
            ? (srgb ? Etc::Image::Format::SRGB8A1 : Etc::Image::Format::RGB8A1)
            : (srgb ? Etc::Image::Format::SRGBA8 : Etc::Image::Format::RGBA8);
        const Etc::Image::EncodingStatus status = image.Encode(format, Etc::RGBA, 0.0f, 1, 1);
        bits = image.GetEncodingBits();
        const bool has_error =
            (static_cast<unsigned>(status) & static_cast<unsigned>(Etc::Image::ERROR_THRESHOLD)) != 0;
        ok = !has_error && bits && image.GetEncodingBitsBytes() == expected_bytes;
        if (ok) memcpy(out, bits, expected_bytes);
    } catch (...) {
        ok = false;
    }
    delete[] bits; // Etc::Image's destructor intentionally does not own m_paucEncodingBits.
    free(rgba);

    if (!ok) {
        memset(out, 0, expected_bytes);
        return false;
    }
    if (have_path) cache_write(path, header, out, expected_bytes);
    return true;
}
