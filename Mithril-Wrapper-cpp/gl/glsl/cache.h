// Mithril-Wrapper - gl/glsl/cache.h
// SHA-256 LRU persistent cache for translated SPIR-V.
//
// Mirrors MobileGlues' gl/glsl/cache.{h,cpp} architecture: a SHA-256 key
// digest, LRU eviction, atomic disk load/save (write `.new` then rename),
// incremental flush (kPendingEntriesBeforeSave entries or kSaveIntervalNs
// since the last save), and a thread_local digest memo so a miss followed by
// a put() with the same key material hashes only once.
//
// Key differences from MobileGlues (deliberate):
//   * Cached value is SPIR-V words (std::vector<uint32_t>) for the
//     Vulkan/MoltenVK backend, not ESSL source for a native GLES driver.
//   * LRU cap is count-based (kMaxEntries, default 256) rather than
//     MobileGlues' byte-size cap (Mithril has no global settings struct yet;
//     config/settings is Phase 2).
//   * A std::mutex guards every mutation (Mithril compiles shaders from
//     multiple threads; MobileGlues' cache is single-threaded and lock-free).
//
// Deep reference: MobileGlues gl/glsl/cache.cpp (SHA-256 LRU + persistence
// policy + thread_local digest reuse).
#ifndef MITHRIL_GLSL_CACHE_H
#define MITHRIL_GLSL_CACHE_H

#include <array>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace mithril::glsl {

class SpirvCache {
public:
    SpirvCache();
    ~SpirvCache();
    SpirvCache(const SpirvCache&) = delete;
    SpirvCache& operator=(const SpirvCache&) = delete;

    // Returns a pointer to the cached SPIR-V for key_material, or nullptr on
    // miss. The pointer is owned by the cache and remains valid only until the
    // next cache mutation (put / eviction / save) -- callers should copy
    // immediately. On a miss the computed digest is stashed in thread_local
    // storage so a subsequent put() with the same key material reuses it
    // instead of hashing twice.
    const std::vector<uint32_t>* get(const std::string& key_material);

    // Inserts (or replaces) the SPIR-V for key_material. Triggers an
    // incremental flush if the pending-entry or time threshold is crossed.
    void put(const std::string& key_material, std::vector<uint32_t> value);

    // Loads the cache from `path` (binary blob: count then entries). Returns
    // false on missing/unreadable file (leaves the cache empty). A corrupted
    // file clears the cache and rewrites it on the next save. `path` becomes
    // the cache's persistence target for subsequent flush()/save() calls.
    bool load(const std::string& path);

    // Serialises the whole cache to `path` immediately (write `.new` then
    // rename for an atomic replace). `path` becomes the cache's persistence
    // target. save() always writes every entry; pending-entry accounting is
    // reset here.
    void save(const std::string& path);

    // Forces a save of any pending entries to the current persistence target
    // (set by load()/save() or the constructor's env-var lookup), regardless
    // of the incremental-flush thresholds. No-op if no target is set or there
    // is nothing pending. Intended as a teardown hook.
    void flush();

    static SpirvCache& instance();

    // Incremental-flush policy (mirrors MobileGlues cache.cpp).
    static constexpr int kPendingEntriesBeforeSave = 16;
    static constexpr int64_t kSaveIntervalNs = 5LL * 1000 * 1000 * 1000;
    // LRU count cap (MobileGlues uses a byte cap via global_settings; Mithril
    // has no settings struct yet, so a fixed count cap is used instead).
    static constexpr size_t kMaxEntries = 256;

private:
    struct Entry {
        std::array<uint8_t, 32> sha256;
        std::vector<uint32_t> spirv;
    };
    struct SHA256Hash {
        size_t operator()(const std::array<uint8_t, 32>& key) const;
    };

    // All private helpers assume mu_ is already held by the caller.
    void maintainSizeLocked();
    void flushIfDueLocked();
    void saveLocked(const std::string& path);
    static std::array<uint8_t, 32> computeSHA256(const uint8_t* data, size_t length);

    std::mutex mu_;
    std::list<Entry> list_;
    using ListIt = std::list<Entry>::iterator;
    std::unordered_map<std::array<uint8_t, 32>, ListIt, SHA256Hash> map_;
    int pending_ = 0;
    int64_t lastSaveNs_ = 0;
    std::string path_;
};

// Free-function API delegating to the SpirvCache singleton. These are the
// entry points glsl_for_vk.cpp calls; keeping them free functions mirrors the
// shape of the existing mithril::shader_translate API.
const std::vector<uint32_t>* cache_get(const std::string& key_material);
void cache_put(const std::string& key_material, std::vector<uint32_t> value);
void cache_flush();
bool cache_load(const std::string& path);
void cache_save(const std::string& path);

} // namespace mithril::glsl

#endif // MITHRIL_GLSL_CACHE_H
