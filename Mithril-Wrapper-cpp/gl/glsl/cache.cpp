// Mithril-Wrapper - gl/glsl/cache.cpp
// SHA-256 LRU persistent cache for translated SPIR-V.
//
// Architecture mirrors MobileGlues gl/glsl/cache.cpp:
//   * Inline FIPS 180-4 SHA-256 (no external crypto dependency; identical
//     digest to OpenSSL/libcrypto so caches stay portable).
//   * LRU via std::list + unordered_map<digest, list::iterator>; splice-on-hit
//     keeps the MRU entry at the back, evict-from-front on overflow.
//   * Disk persistence: one `count, then entries` blob, written to a `.new`
//     sibling and rename(2)'d over the live file so a crash mid-write cannot
//     leave a half-written cache that load() would have to discard.
//   * Incremental flush: a save is deferred until kPendingEntriesBeforeSave
//     entries are pending or kSaveIntervalNs have passed since the last one,
//     checked on every get()/put() so a run of hits still flushes a tail left
//     by earlier misses. Exposure is bounded: a crash loses at most the 15
//     most recently translated shaders.
//   * thread_local digest memo: get() and put() are called back to back with
//     the same key material on every miss, and shader sources run to tens of
//     kilobytes, so a miss parks its digest + source here and put() reuses it
//     when the source matches byte for byte. The memcmp that rules out a false
//     match is one to two orders of magnitude cheaper than the SHA-256 it
//     replaces.
//
// Persistence target: the constructor reads MITHRIL_GLSL_CACHE (a file path)
// and loads from it if set; load(path)/save(path) override the target. With no
// env var and no explicit load/save, the cache stays in-memory only (still
// correct, just not cross-restart). Mithril has no global settings struct yet
// (Phase 2), so the env var is the lightweight stand-in for MobileGlues'
// glsl_cache_file_path global.
#include "cache.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <utility>

namespace mithril::glsl {

// ---------------------------------------------------------------------------
// Inline SHA-256 (FIPS 180-4). Verbatim from MobileGlues cache.cpp; the digest
// is bit-for-bit identical to OpenSSL's SHA256(), so a cache written by one
// implementation loads in the other. Inlined here to avoid a libcrypto link
// dependency (the iOS toolchain does not ship OpenSSL by default).
// ---------------------------------------------------------------------------
static const uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

namespace {
    inline uint32_t rotr(uint32_t x, uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }
    inline uint32_t sigma0(uint32_t x) {
        return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
    }
    inline uint32_t sigma1(uint32_t x) {
        return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
    }
    inline uint32_t Sigma0(uint32_t x) {
        return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
    }
    inline uint32_t Sigma1(uint32_t x) {
        return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
    }
    inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (~x & z);
    }
    inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (x & z) ^ (y & z);
    }

    void sha256_compress(uint32_t h[8], const uint8_t* block) {
        uint32_t w[64];
        for (int t = 0; t < 16; ++t) {
            // Widen before shifting: a uint8_t promotes to int, and 0xff << 24
            // overflows a signed int.
            w[t] = (static_cast<uint32_t>(block[t * 4]) << 24) | (static_cast<uint32_t>(block[t * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[t * 4 + 2]) << 8) | static_cast<uint32_t>(block[t * 4 + 3]);
        }
        for (int t = 16; t < 64; ++t) {
            w[t] = sigma1(w[t - 2]) + w[t - 7] + sigma0(w[t - 15]) + w[t - 16];
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int t = 0; t < 64; ++t) {
            uint32_t T1 = hh + Sigma1(e) + ch(e, f, g) + k[t] + w[t];
            uint32_t T2 = Sigma0(a) + maj(a, b, c);
            hh = g; g = f; f = e; e = d + T1; d = c; c = b; b = a; a = T1 + T2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    int64_t monotonic_now_ns() {
        // CLOCK_MONOTONIC, so a wall-clock jump cannot stall the flush nor
        // force one.
        timespec ts{};
        if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
        return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + static_cast<int64_t>(ts.tv_nsec);
    }

    // thread_local digest memo. See cache.h for the rationale. thread_local,
    // not members, because get() and put() are always issued as a pair from
    // one thread while the memo is per-call-pair; a member would be clobbered
    // by concurrent translators. The mutex guards the shared map/list, not
    // this scratch.
    thread_local std::string g_memo_source;
    thread_local std::array<uint8_t, 32> g_memo_digest{};
    thread_local bool g_memo_valid = false;
} // namespace

std::array<uint8_t, 32> SpirvCache::computeSHA256(const uint8_t* data, size_t length) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    size_t offset = 0;
    for (; length - offset >= 64; offset += 64) {
        sha256_compress(h, data + offset);
    }

    const size_t remainder = length - offset;
    uint8_t tail[128] = {0};
    if (remainder > 0) memcpy(tail, data + offset, remainder);
    tail[remainder] = 0x80;
    const size_t tail_size = (remainder < 56) ? 64 : 128;

    const uint64_t bit_length = static_cast<uint64_t>(length) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[tail_size - 8 + i] = static_cast<uint8_t>(bit_length >> (56 - i * 8));
    }
    for (size_t i = 0; i < tail_size; i += 64) {
        sha256_compress(h, tail + i);
    }

    std::array<uint8_t, 32> hash{};
    for (int i = 0; i < 8; ++i) {
        hash[i * 4]     = static_cast<uint8_t>(h[i] >> 24);
        hash[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        hash[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        hash[i * 4 + 3] = static_cast<uint8_t>(h[i]);
    }
    return hash;
}

size_t SpirvCache::SHA256Hash::operator()(const std::array<uint8_t, 32>& key) const {
    size_t hash = 0;
    for (int i = 0; i < 4; ++i) {
        hash = (hash << 8) | key[i];
    }
    for (int i = 28; i < 32; ++i) {
        hash = (hash << 8) | key[i];
    }
    return hash;
}

SpirvCache::SpirvCache() {
    // Persistence target: MITHRIL_GLSL_CACHE env var (a file path). If set,
    // load immediately so a warm cache short-circuits the first compile. If
    // unset, the cache is in-memory only for this process.
    if (const char* env = std::getenv("MITHRIL_GLSL_CACHE")) {
        if (env[0] != '\0') {
            path_ = env;
            load(path_);
        }
    }
    lastSaveNs_ = monotonic_now_ns();
}

SpirvCache::~SpirvCache() {
    // Best-effort flush on orderly unload/exit. The singleton is a
    // function-local static, so this does not run on an Android/process kill;
    // the incremental-flush policy bounds the loss to at most 15 shaders.
    if (pending_ > 0 && !path_.empty()) {
        // No lock: at static-destruction time all other threads are gone.
        saveLocked(path_);
    }
}

void SpirvCache::flushIfDueLocked() {
    if (pending_ == 0) return;
    if (pending_ < kPendingEntriesBeforeSave &&
        (monotonic_now_ns() - lastSaveNs_) < kSaveIntervalNs) {
        return;
    }
    if (!path_.empty()) saveLocked(path_);
}

void SpirvCache::flush() {
    std::lock_guard<std::mutex> lk(mu_);
    if (pending_ > 0 && !path_.empty()) {
        saveLocked(path_);
    }
}

const std::vector<uint32_t>* SpirvCache::get(const std::string& key_material) {
    std::lock_guard<std::mutex> lk(mu_);
    flushIfDueLocked();

    std::array<uint8_t, 32> hash = computeSHA256(
        reinterpret_cast<const uint8_t*>(key_material.data()), key_material.size());
    auto it = map_.find(hash);
    if (it == map_.end()) {
        // A miss is what put() follows; a hit ends the translation here, so
        // only the miss is worth remembering.
        g_memo_valid = false;
        g_memo_source.assign(key_material);
        g_memo_digest = hash;
        g_memo_valid = true;
        return nullptr;
    }
    // MRU -> back of the list.
    list_.splice(list_.end(), list_, it->second);
    return &it->second->spirv;
}

void SpirvCache::put(const std::string& key_material, std::vector<uint32_t> value) {
    std::lock_guard<std::mutex> lk(mu_);

    std::array<uint8_t, 32> hash;
    if (g_memo_valid && g_memo_source.size() == key_material.size() &&
        memcmp(g_memo_source.data(), key_material.data(), key_material.size()) == 0) {
        hash = g_memo_digest;
    } else {
        hash = computeSHA256(
            reinterpret_cast<const uint8_t*>(key_material.data()), key_material.size());
    }

    if (auto it = map_.find(hash); it != map_.end()) {
        // Replace: drop the existing entry, then re-insert at MRU.
        list_.erase(it->second);
        map_.erase(it);
    }

    list_.push_back(Entry{hash, std::move(value)});
    map_[hash] = std::prev(list_.end());

    maintainSizeLocked();
    ++pending_;
    flushIfDueLocked();
}

void SpirvCache::maintainSizeLocked() {
    while (list_.size() > kMaxEntries && !list_.empty()) {
        const auto& oldEntry = list_.front();
        map_.erase(oldEntry.sha256);
        list_.pop_front();
    }
}

bool SpirvCache::load(const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    path_ = path;
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;

        size_t count;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));

        while (count--) {
            std::array<uint8_t, 32> hash{};
            size_t word_count;

            file.read(reinterpret_cast<char*>(hash.data()), hash.size());
            file.read(reinterpret_cast<char*>(&word_count), sizeof(word_count));

            std::vector<uint32_t> spirv(word_count);
            file.read(reinterpret_cast<char*>(spirv.data()),
                      static_cast<std::streamsize>(word_count * sizeof(uint32_t)));

            if (map_.count(hash)) continue;

            list_.push_back(Entry{hash, std::move(spirv)});
            map_[hash] = std::prev(list_.end());
        }

        maintainSizeLocked();
        return true;
    } catch (...) {
        // Corrupted cache: clear and rewrite on the next save so a bad file
        // does not poison every subsequent run.
        map_.clear();
        list_.clear();
        if (!path_.empty()) saveLocked(path_);
        return false;
    }
}

void SpirvCache::saveLocked(const std::string& path) {
    // Cleared before the attempt, not after: save() serialises the whole list
    // every time, so the counter is a trigger, not a record of what is
    // missing. Clearing here stops a device with a full/unwritable disk from
    // retrying a whole-file write on every single compile.
    pending_ = 0;
    lastSaveNs_ = monotonic_now_ns();

    const std::string temp_path = path + ".new";
    {
        std::ofstream file(temp_path, std::ios::binary);
        if (!file) return;

        size_t count = list_.size();
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));

        for (const auto& entry : list_) {
            file.write(reinterpret_cast<const char*>(entry.sha256.data()),
                       static_cast<std::streamsize>(entry.sha256.size()));
            size_t word_count = entry.spirv.size();
            file.write(reinterpret_cast<const char*>(&word_count), sizeof(word_count));
            file.write(reinterpret_cast<const char*>(entry.spirv.data()),
                       static_cast<std::streamsize>(word_count * sizeof(uint32_t)));
        }

        file.flush();
        if (!file) {
            // A short write must not become the cache: keep whatever is
            // already in place and drop the partial file.
            file.close();
            std::remove(temp_path.c_str());
            return;
        }
    }

    if (std::rename(temp_path.c_str(), path.c_str()) != 0) {
        std::remove(temp_path.c_str());
    }
}

void SpirvCache::save(const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    path_ = path;
    saveLocked(path);
}

SpirvCache& SpirvCache::instance() {
    static SpirvCache s_cache;
    return s_cache;
}

// ---- Free-function API ----
const std::vector<uint32_t>* cache_get(const std::string& key_material) {
    return SpirvCache::instance().get(key_material);
}

void cache_put(const std::string& key_material, std::vector<uint32_t> value) {
    SpirvCache::instance().put(key_material, std::move(value));
}

void cache_flush() {
    SpirvCache::instance().flush();
}

bool cache_load(const std::string& path) {
    return SpirvCache::instance().load(path);
}

void cache_save(const std::string& path) {
    SpirvCache::instance().save(path);
}

} // namespace mithril::glsl
