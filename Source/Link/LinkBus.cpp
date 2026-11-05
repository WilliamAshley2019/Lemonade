#include "LinkBus.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <new>

#ifdef _WIN32
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <unistd.h>
#endif

namespace lemon::link
{
namespace
{
std::atomic<int> g_refs { 0 };
Segment* g_seg = nullptr;
#ifdef _WIN32
HANDLE g_map = nullptr;
#else
const char* kName = "/LemonLink_v1";
#endif
constexpr uint32_t kMagic = 0x4C4D4C31;   // "LML1"

uint64_t nowMs() noexcept
{
    return (uint64_t) std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

Segment* openSegment() noexcept
{
    if (g_refs.fetch_add(1) > 0) return g_seg;
    const size_t sz = sizeof(Segment);
#ifdef _WIN32
    g_map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, (DWORD)((uint64_t) sz >> 32), (DWORD)(sz & 0xffffffffu), "Local\\LemonLink_v1");
    if (!g_map) { g_refs = 0; return nullptr; }
    g_seg = static_cast<Segment*>(MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sz));
    if (!g_seg) { CloseHandle(g_map); g_map = nullptr; g_refs = 0; return nullptr; }
#else
    int fd = shm_open(kName, O_CREAT | O_RDWR, 0600);
    if (fd < 0) { g_refs = 0; return nullptr; }
    if (ftruncate(fd, (off_t) sz) != 0) { close(fd); g_refs = 0; return nullptr; }
    void* p = mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) { g_refs = 0; return nullptr; }
    g_seg = static_cast<Segment*>(p);
#endif
    // Zero-filled by the OS; first mapper stamps the magic (all-zero atomics are valid initial state).
    uint32_t z = 0;
    g_seg->magic.compare_exchange_strong(z, kMagic);
    return g_seg;
}

void closeSegment() noexcept
{
    if (g_refs.fetch_sub(1) != 1) return;
#ifdef _WIN32
    if (g_seg) UnmapViewOfFile(g_seg);
    if (g_map) CloseHandle(g_map);
    g_map = nullptr;
#else
    if (g_seg) munmap(g_seg, sizeof(Segment));
#endif
    g_seg = nullptr;
}

bool Writer::attach(Segment* s, int group, int lane, Dir d, uint32_t id, double sr) noexcept
{
    release();
    if (!s || group < 0 || group >= kGroups || lane < 0 || lane >= kLanes || id == 0) return false;
    Ring& r = s->ring[group][lane][d];
    uint32_t expected = 0;
    if (!r.owner.compare_exchange_strong(expected, id))
    {
        // taken: steal only if the owner has been silent for >2 s (crashed instance)
        const uint64_t hb = r.heartbeat.load(std::memory_order_relaxed);
        if (expected != id && nowMs() - hb < 2000) return false;
        r.owner.store(id);
    }
    r.sampleRate.store((uint32_t) sr, std::memory_order_relaxed);
    r.heartbeat.store(nowMs(), std::memory_order_relaxed);
    ring_ = &r; id_ = id;
    return true;
}

void Writer::release() noexcept
{
    if (!ring_) return;
    uint32_t me = id_;
    ring_->owner.compare_exchange_strong(me, 0);
    ring_ = nullptr;
}

void Writer::write(const float* l, const float* r, int n) noexcept
{
    if (!ring_ || n <= 0) return;
    uint64_t w = ring_->writePos.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        const size_t k = (size_t)((w + (uint64_t) i) & (kRing - 1));
        ring_->l[k] = l[i];
        ring_->r[k] = r[i];
    }
    ring_->heartbeat.store(nowMs(), std::memory_order_relaxed);
    ring_->writePos.store(w + (uint64_t) n, std::memory_order_release);
}

void Reader::attach(Segment* s, int group, int lane, Dir d, double sr, int blockHint) noexcept
{
    ring_ = (s && group >= 0 && group < kGroups && lane >= 0 && lane < kLanes) ? &s->ring[group][lane][d] : nullptr;
    sr_ = sr; hint_ = std::max(1, blockHint);
    primed_ = false; live_ = false; underruns_ = 0; calm_ = 0;
    margin_ = std::min(hint_ * 2, kRing / 4);       // starting cushion; grows on underrun, shrinks when calm
    minMargin_ = std::min(hint_, kRing / 8);
}

bool Reader::read(float* l, float* r, int n) noexcept
{
    auto silence = [&] { std::fill(l, l + n, 0.0f); std::fill(r, r + n, 0.0f); live_ = false; return false; };
    if (!ring_ || n <= 0 || n > kRing / 4) return silence();
    if (ring_->owner.load(std::memory_order_relaxed) == 0) { primed_ = false; return silence(); }
    if (std::abs((double) ring_->sampleRate.load(std::memory_order_relaxed) - sr_) > 1.0) { primed_ = false; return silence(); }

    const uint64_t w = ring_->writePos.load(std::memory_order_acquire);
    if (!primed_ || w < readPos_ || w - readPos_ > (uint64_t)(kRing - n))
    {
        // (re)sync: sit `margin_` behind the writer
        readPos_ = w > (uint64_t) margin_ ? w - (uint64_t) margin_ : 0;
        primed_ = true;
    }
    if (w < readPos_ + (uint64_t) n)
    {
        // underrun: not enough data yet. Output silence, grow the cushion, re-sync next call.
        ++underruns_; calm_ = 0;
        margin_ = std::min(margin_ + hint_, kRing / 4);
        primed_ = false;
        return silence();
    }
    for (int i = 0; i < n; ++i)
    {
        const size_t k = (size_t)((readPos_ + (uint64_t) i) & (kRing - 1));
        l[i] = ring_->l[k];
        r[i] = ring_->r[k];
    }
    readPos_ += (uint64_t) n;
    // the actual cushion left after this read tells us if margin_ is larger than needed
    if (++calm_ > 2000 && margin_ > minMargin_) { margin_ -= std::min(hint_ / 2 + 1, margin_ - minMargin_); calm_ = 0; primed_ = false; }
    live_ = true;
    return true;
}
}
