#pragma once
// Effect slots and the configurable pre/post routing.
//
// Real-time safety model
//  * Effect instances are created and prepared on a non-audio thread (the "builder": message-thread
//    timer, prepareToPlay, state restore, preset load) and handed to the audio thread through a
//    single atomic pointer per slot.
//  * The audio thread adopts the new instance and crossfades from the old one over ~15 ms.
//  * Instances the audio thread no longer needs are pushed into a lock-free SPSC queue and deleted
//    by the builder. The audio thread never allocates, frees, locks or blocks.

#include "EffectRegistry.h"
#include <atomic>
#include <mutex>

namespace pf
{
/** Fixed-capacity single-producer/single-consumer pointer queue. */
template <typename T, int Capacity>
class SpscQueue
{
public:
    bool push (T v) noexcept
    {
        const int w = write.load (std::memory_order_relaxed), next = (w + 1) % Capacity;
        if (next == read.load (std::memory_order_acquire)) return false;
        items[(size_t) w] = v; write.store (next, std::memory_order_release); return true;
    }
    bool pop (T& v) noexcept
    {
        const int r = read.load (std::memory_order_relaxed);
        if (r == write.load (std::memory_order_acquire)) return false;
        v = items[(size_t) r]; read.store ((r + 1) % Capacity, std::memory_order_release); return true;
    }
private:
    std::array<T, Capacity> items {};
    std::atomic<int> read { 0 }, write { 0 };
};

/** Per-block slot settings, already resolved from host parameters (normalised values). */
struct SlotSettings
{
    int type = 0;
    bool enabled = true;
    float mix = 1.0f;
    float norm[kMaxEffectParams] {};
};

class EffectSlot
{
public:
    EffectSlot() = default;
    ~EffectSlot();

    // ---- builder side (never the audio thread)
    void prepare (double sampleRate, int maxBlock);        // audio must be stopped
    void buildNow (int type, const SlotSettings& s);        // audio must be stopped (prepare/reset paths)
    void publish (int type, const SlotSettings& s);         // safe while audio runs
    void collectGarbage();
    int  builderType() const noexcept { return publishedType; }

    // ---- audio side
    void process (float* L, float* R, int n, const SlotSettings& s, const ProcessContext& ctx) noexcept;
    void resetAudio() noexcept;
    int  activeType() const noexcept { return currentType; }

private:
    struct Swap { Effect* incoming = nullptr; int type = 0; Effect* outgoing = nullptr; };
    std::unique_ptr<Effect> makeEffect (int type, const SlotSettings& s) const;
    void applyParams (Effect* e, int type, const SlotSettings& s, bool force) noexcept;
    void finishFade() noexcept;

    double sr = 44100.0; int maxBlock = 512;
    std::atomic<Swap*> pending { nullptr };
    Swap* activeShell = nullptr; // audio-thread owned until pushed to 'retired'
    SpscQueue<Swap*, 128> retired;
    int publishedType = 0;

    Effect* current = nullptr; int currentType = 0;
    Effect* fading = nullptr;  int fadingType = 0;
    float fadePos = 1.0f, fadeInc = 0.01f;
    float lastNorm[kMaxEffectParams] {};
    Smoother enabledRamp, mixS;
    bool needsReset = false;
    int silentSamples = 0;
    std::vector<float> dryL, dryR, fadeL, fadeR;
};

//==============================================================================
/** Order of the eight slots and how many of them run before the amp. Packed into one atomic word. */
struct Routing
{
    std::array<int, kNumSlots> order { 0, 1, 2, 3, 4, 5, 6, 7 };
    int numPre = 4;

    uint32_t pack() const noexcept
    {
        uint32_t v = 0;
        for (int i = 0; i < kNumSlots; ++i) v |= (uint32_t) (order[(size_t) i] & 7) << (3 * i);
        return v | ((uint32_t) (numPre & 15) << 24) | 0x80000000u;
    }
    static Routing unpack (uint32_t v) noexcept
    {
        Routing r;
        if ((v & 0x80000000u) == 0) return r;
        for (int i = 0; i < kNumSlots; ++i) r.order[(size_t) i] = (int) ((v >> (3 * i)) & 7);
        r.numPre = (int) ((v >> 24) & 15);
        return r;
    }
    bool isValid() const noexcept
    {
        if (numPre < 0 || numPre > kNumSlots) return false;
        int seen = 0;
        for (int s : order) { if (s < 0 || s >= kNumSlots) return false; seen |= 1 << s; }
        return seen == 0xFF;
    }
    bool isPre (int slot) const noexcept
    {
        for (int i = 0; i < numPre; ++i) if (order[(size_t) i] == slot) return true;
        return false;
    }
    int positionOf (int slot) const noexcept
    {
        for (int i = 0; i < kNumSlots; ++i) if (order[(size_t) i] == slot) return i;
        return -1;
    }
    std::string toString() const;
    static Routing fromString (const std::string& s);
};
} // namespace pf
