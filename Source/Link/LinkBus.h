#pragma once
// LinkBus - named shared-memory audio lanes between plugin instances in ONE host process
// (or several, via the OS object). No JUCE. Each lane is a single-producer/single-consumer
// ring of interleaved-free stereo floats. Lemon Mixer Rack tracks and LemonSlice In/Out
// plugins attach to a lane by (group, lane number).
//
//   LemonSlice In  : DAW track audio  -> lane -> Lemon Mixer Rack track ("FROM DAW n")
//   LemonSlice Out : Lemon Mixer Rack track -> lane -> DAW track          ("TO DAW n")
//
// Each lane carries its direction in a separate ring: toRack (DAW->rack) and toDaw (rack->DAW).
#include <atomic>
#include <cstdint>

namespace lemon::link
{
constexpr int kGroups = 4;
constexpr int kLanes  = 16;
constexpr int kRing   = 16384;            // frames, power of two
constexpr int kMaxBlock = 8192;

enum Dir { toRack = 0, toDaw = 1 };

struct Ring
{
    std::atomic<uint64_t> writePos { 0 };       // frames written, ever
    std::atomic<uint32_t> sampleRate { 0 };
    std::atomic<uint32_t> owner { 0 };          // 0 = free, else writer id
    std::atomic<uint64_t> heartbeat { 0 };      // ms tick of the last write
    float l[kRing];
    float r[kRing];
};

struct Segment
{
    std::atomic<uint32_t> magic { 0 };
    Ring ring[kGroups][kLanes][2];
};

// Opens (creating if needed) the process-shared segment. Returns nullptr on failure.
Segment* openSegment() noexcept;
void closeSegment() noexcept;                    // ref-counted with openSegment()

class Writer
{
public:
    ~Writer() { release(); }
    bool attach(Segment* s, int group, int lane, Dir d, uint32_t id, double sr) noexcept;   // false if lane taken by another live writer
    void release() noexcept;
    // Appends n frames. Never blocks.
    void write(const float* l, const float* r, int n) noexcept;
    bool attached() const noexcept { return ring_ != nullptr; }
private:
    Ring* ring_ = nullptr;
    uint32_t id_ = 0;
};

class Reader
{
public:
    void attach(Segment* s, int group, int lane, Dir d, double sr, int blockHint) noexcept;
    void release() noexcept { ring_ = nullptr; primed_ = false; }
    // Fills n frames. Silence when no live writer / rate mismatch / underrun. Returns true if real audio.
    bool read(float* l, float* r, int n) noexcept;
    int  latencyFrames() const noexcept { return margin_; }   // steady-state delay added by the link
    int  underruns() const noexcept { return underruns_; }
    bool active() const noexcept { return live_; }
private:
    Ring* ring_ = nullptr;
    uint64_t readPos_ = 0;
    bool primed_ = false, live_ = false;
    int margin_ = 0, minMargin_ = 0, hint_ = 0, underruns_ = 0, calm_ = 0;
    double sr_ = 48000.0;
};
}
