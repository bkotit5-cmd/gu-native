// BC1/2/3 -> ETC2, for GPUs that expose ETC2 but not BC. Results are cached by SHA-256 of
// the linear BC blocks and exact conversion parameters; the cache may be deleted at any time.
#include "etc2_transcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define ETC2_MKDIR(p) _mkdir(p)
#else
#define ETC2_MKDIR(p) mkdir(p, 0755)
#endif

#include "bcn.h"
#include "logwriter.h"
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

struct CacheWriteJob {
    char path[160];
    CacheHeader header;
    size_t bytes;
    uint8_t *data;
    CacheWriteJob *next;
};

std::mutex cache_queue_mutex;
std::condition_variable cache_queue_cv;
CacheWriteJob *cache_queue_head, *cache_queue_tail;
size_t cache_queue_pending_bytes;
bool cache_writer_started;
constexpr size_t CACHE_WRITE_QUEUE_LIMIT = 16u << 20;
std::mutex encode_mutex;
std::condition_variable encode_cv;
unsigned int active_encoders;
std::atomic<uint64_t> temp_serial{0};
std::atomic<unsigned int> transcode_log_count{0};
int cache_dir_state;

unsigned int encoder_slots() {
    static const unsigned int slots = std::thread::hardware_concurrency() >= 6 ? 2u : 1u;
    return slots;
}

unsigned int encoder_jobs(uint64_t blocks) {
#ifdef __ANDROID__
    (void)blocks;
    // Upload workers already provide concurrency. Avoid nested ETC encoder threads competing with the game and renderer.
    return 1u;
#else
    static const unsigned int cores = std::thread::hardware_concurrency();
    return cores >= 8 && blocks >= 4096 ? 2u : 1u;
#endif
}

class EncoderSlot {
public:
    EncoderSlot() {
        std::unique_lock<std::mutex> lock(encode_mutex);
        encode_cv.wait(lock, [] { return active_encoders < encoder_slots(); });
        ++active_encoders;
    }
    ~EncoderSlot() {
        std::lock_guard<std::mutex> lock(encode_mutex);
        --active_encoders;
        encode_cv.notify_one();
    }
    EncoderSlot(const EncoderSlot &) = delete;
    EncoderSlot &operator=(const EncoderSlot &) = delete;
};

void log_transcode(int kind, uint32_t width, uint32_t height, bool cache_hit,
                   std::chrono::steady_clock::time_point start) {
    const unsigned int n = transcode_log_count.fetch_add(1, std::memory_order_relaxed);
    if (n >= 24) return;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    rt_logf("[tex] ETC2 BC%d %ux%u: %s in %.1f ms\n", kind, width, height,
            cache_hit ? "cache hit" : "encoded", ms);
}

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
    // Cache files are immutable after publication: writers use a temporary file and rename it into place.
    // Reads therefore need no global lock and can proceed concurrently on independent upload workers.
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    CacheHeader h{};
    const bool ok = fread(&h, sizeof(h), 1, f) == 1 &&
        !memcmp(h.magic, expected.magic, sizeof(h.magic)) &&
        h.version == expected.version && h.kind == expected.kind &&
        h.width == expected.width && h.height == expected.height &&
        h.srgb == expected.srgb && h.format == expected.format &&
        h.output_bytes == expected.output_bytes && h.digest[64] == 0 &&
        !strcmp(h.digest, expected.digest) &&
        fread(out, 1, bytes, f) == bytes && fgetc(f) == EOF && !ferror(f);
    fclose(f);
    // Do not unlink a bad entry here: another worker may have replaced it after this reader opened it.
    // A successful cache write will replace stale/corrupt entries safely.
    return ok;
}

void cache_write_file(const char *path, const CacheHeader &header, const uint8_t *data, size_t bytes) {
    // Called only by the dedicated cache writer, never by a texture upload worker.
    if (!cache_dirs()) return;
    char tmp[256];
    const uint64_t serial = temp_serial.fetch_add(1, std::memory_order_relaxed);
    if (snprintf(tmp, sizeof(tmp), "%s.tmp.%llu", path, static_cast<unsigned long long>(serial)) >=
        static_cast<int>(sizeof(tmp))) return;
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    bool ok = fwrite(&header, sizeof(header), 1, f) == 1 &&
              fwrite(data, 1, bytes, f) == bytes && fflush(f) == 0;
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        remove(tmp);
        return;
    }
#ifdef _WIN32
    // The C runtime's rename does not replace an existing destination on Windows.
    // Cache entries are disposable and can always be regenerated.
    remove(path);
#endif
    if (rename(tmp, path) != 0) remove(tmp);
}

void cache_writer_loop() {
    for (;;) {
        CacheWriteJob *job;
        {
            std::unique_lock<std::mutex> lock(cache_queue_mutex);
            cache_queue_cv.wait(lock, [] { return cache_queue_head != nullptr; });
            job = cache_queue_head;
            cache_queue_head = job->next;
            if (!cache_queue_head) cache_queue_tail = nullptr;
        }

        cache_write_file(job->path, job->header, job->data, job->bytes);
        const size_t bytes = job->bytes;
        free(job->data);
        free(job);

        {
            std::lock_guard<std::mutex> lock(cache_queue_mutex);
            cache_queue_pending_bytes -= bytes;
        }
    }
}

bool cache_enqueue_write(const char *path, const CacheHeader &header, const uint8_t *data, size_t bytes) {
    // Cache persistence is best-effort. Bound the extra copy so slow storage cannot grow RAM usage indefinitely.
    if (!bytes || bytes > CACHE_WRITE_QUEUE_LIMIT) return false;
    {
        std::lock_guard<std::mutex> lock(cache_queue_mutex);
        if (bytes > CACHE_WRITE_QUEUE_LIMIT - cache_queue_pending_bytes) return false;
        cache_queue_pending_bytes += bytes;
        if (!cache_writer_started) {
            try {
                std::thread(cache_writer_loop).detach();
                cache_writer_started = true;
            } catch (...) {
                cache_queue_pending_bytes -= bytes;
                return false;
            }
        }
    }

    CacheWriteJob *job = static_cast<CacheWriteJob *>(malloc(sizeof(*job)));
    uint8_t *copy = static_cast<uint8_t *>(malloc(bytes));
    if (!job || !copy) {
        free(job);
        free(copy);
        std::lock_guard<std::mutex> lock(cache_queue_mutex);
        cache_queue_pending_bytes -= bytes;
        return false;
    }
    memcpy(copy, data, bytes);
    memset(job, 0, sizeof(*job));
    if (snprintf(job->path, sizeof(job->path), "%s", path) >= static_cast<int>(sizeof(job->path))) {
        free(copy);
        free(job);
        std::lock_guard<std::mutex> lock(cache_queue_mutex);
        cache_queue_pending_bytes -= bytes;
        return false;
    }
    job->header = header;
    job->bytes = bytes;
    job->data = copy;

    {
        std::lock_guard<std::mutex> lock(cache_queue_mutex);
        if (cache_queue_tail) cache_queue_tail->next = job;
        else cache_queue_head = job;
        cache_queue_tail = job;
    }
    cache_queue_cv.notify_one();
    return true;
}
} // namespace

extern "C" bool nvn_transcode_bcn_etc2(int kind, const uint8_t *blocks, uint32_t width, uint32_t height,
                                       bool srgb, uint8_t *out, size_t out_size) {
    const auto started = std::chrono::steady_clock::now();
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
    if (have_path && cache_read(path, header, out, expected_bytes)) {
        log_transcode(kind, width, height, true, started);
        return true;
    }

    // The old single global encoder lock serialized every ETC2 texture and each Encode call used one thread.
    // Two bounded tile encoders may now run together; float memory per encoder is still limited to TILE_HEIGHT rows.
    EncoderSlot encoder_slot;
    if (have_path && cache_read(path, header, out, expected_bytes)) {
        log_transcode(kind, width, height, true, started);
        return true;
    }

    // Encode complete ETC2 block-row tiles. Temporary float memory is bounded independently of texture height.
    constexpr uint32_t TILE_HEIGHT = 64;  // whole 4x4 block rows
    const uint32_t block_width = (width + 3) / 4;
    const size_t tile_pixels = static_cast<size_t>(width) * (height < TILE_HEIGHT ? height : TILE_HEIGHT);
    float *rgba = static_cast<float *>(malloc(tile_pixels * 4 * sizeof(float)));
    if (!rgba) {
        memset(out, 0, expected_bytes);
        return false;
    }

    bool ok = true;
    for (uint32_t y = 0; y < height && ok; y += TILE_HEIGHT) {
        const uint32_t tile_height = height - y < TILE_HEIGHT ? height - y : TILE_HEIGHT;
        const uint64_t source_block_offset = static_cast<uint64_t>(y / 4) * block_width * source_bpb;
        const uint64_t output_block_offset = static_cast<uint64_t>(y / 4) * block_width * target_bpb;
        const size_t tile_bytes = static_cast<size_t>(block_width) * ((tile_height + 3) / 4) * target_bpb;

        bcn_decode_float_rgba(kind, blocks + source_block_offset, width, tile_height, rgba);

        unsigned char *bits = nullptr;
        try {
            Etc::Image image(rgba, width, tile_height, Etc::RGBA);
            const Etc::Image::Format format = kind == BCN_BC1
                ? (srgb ? Etc::Image::Format::SRGB8A1 : Etc::Image::Format::RGB8A1)
                : (srgb ? Etc::Image::Format::SRGBA8 : Etc::Image::Format::RGBA8);
            const uint64_t tile_blocks = static_cast<uint64_t>(block_width) * ((tile_height + 3) / 4);
            const unsigned int jobs = encoder_jobs(tile_blocks);
            const Etc::Image::EncodingStatus status = image.Encode(format, Etc::RGBA, 0.0f, jobs, jobs);
            bits = image.GetEncodingBits();
            const bool has_error =
                (static_cast<unsigned>(status) & static_cast<unsigned>(Etc::Image::ERROR_THRESHOLD)) != 0;
            ok = !has_error && bits && image.GetEncodingBitsBytes() == tile_bytes;
            if (ok) memcpy(out + output_block_offset, bits, tile_bytes);
        } catch (...) {
            ok = false;
        }
        // Etc::Image's destructor intentionally does not own m_paucEncodingBits.
        delete[] bits;
    }
    free(rgba);

    if (!ok) {
        memset(out, 0, expected_bytes);
        log_transcode(kind, width, height, false, started);
        return false;
    }
    if (have_path) cache_enqueue_write(path, header, out, expected_bytes);
    log_transcode(kind, width, height, false, started);
    return true;
}
