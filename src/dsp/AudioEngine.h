#pragma once
// The complete rig:
//   Input gain -> Noise gate -> Pre-amp slots -> Amp -> Cabinet/IR -> Post-amp slots
//   -> Master EQ -> Preset output level -> Limiter -> Master volume -> Mono/Stereo -> out

#include "Amps/AmpSection.h"
#include "Cabinets/Cabinet.h"
#include "SignalChain.h"
#include "effects/Dynamics.h"
#include <juce_core/juce_core.h>

namespace pf
{
struct EngineParams
{
    float inGainDb = 0.0f; int inMode = 0; bool bypass = false;
    bool gateOn = false; float gateThreshDb = -60.0f, gateReleaseMs = 80.0f;
    bool ampOn = true; int ampType = 0; AmpControls amp; float ampTrimDb = 0.0f;
    bool cabOn = true; int cabType = (int) CabType::Closed4x12;
    float cabMic = 0.3f, cabLowCut = 70.0f, cabHighCut = 9000.0f, cabLevelDb = 0.0f, cabMix = 1.0f;
    bool eqOn = true; float eqLow = 0.0f, eqMid = 0.0f, eqMidFreq = 800.0f, eqHigh = 0.0f;
    float outGainDb = 0.0f; bool limOn = true; float limCeilingDb = -0.5f; float masterDb = 0.0f;
    int outMode = 0; int quality = 1;
    float midiVolume = 1.0f;
    SlotSettings slots[kNumSlots];
};

class AudioEngine
{
public:
    AudioEngine();

    /** Audio must be stopped. Builds every slot synchronously from 'initial'. */
    void prepare (double sampleRate, int maxBlock, const EngineParams& initial);

    /** Stereo in-place processing. 'numInputs' tells whether the right buffer carries real input. */
    void process (float* L, float* R, int n, int numInputs, const EngineParams& p, const ProcessContext& ctx) noexcept;

    /** Builder (non-audio thread): publish slots whose type changed, free retired instances. */
    void syncSlots (const EngineParams& p);

    void setRouting (const Routing& r) noexcept { routingWord.store (r.pack()); }
    Routing getRouting() const noexcept         { return Routing::unpack (routingWord.load()); }

    int latencySamples (int quality) const noexcept { return amp.latencyFor (quality); }
    bool isPrepared() const noexcept { return prepared; }

    CabinetSection& cabinet() noexcept { return cab; }
    int activeSlotType (int slot) const noexcept { return slots[(size_t) slot].activeType(); }

    // ---- metering (written on the audio thread, read by the UI)
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, limiterGrDb { 0.0f }, gateGain { 1.0f };

    /** Tuner input: mono post-input-gain signal. Read with readTuner() from one consumer thread. */
    int readTuner (float* dest, int maxSamples) noexcept;
    double getSampleRate() const noexcept { return sr; }

private:
    void processSlots (int from, int to, const Routing& r, float* L, float* R, int n, const EngineParams& p, const ProcessContext& ctx) noexcept;

    double sr = 44100.0; int maxBlock = 512; bool prepared = false;
    std::mutex builderMutex;
    std::array<EffectSlot, kNumSlots> slots;
    std::atomic<uint32_t> routingWord { Routing().pack() };
    uint32_t activeRouting = 0, pendingRouting = 0;
    float duck = 1.0f; int duckState = 0; // 0 idle, 1 fading out, 2 fading in

    NoiseGate gate; Smoother gateMix;
    AmpSection amp; CabinetSection cab;
    Biquad eqLow, eqMid, eqHigh; float eqState[4] { 1.0e9f, 0, 0, 0 };
    Smoother inGain, outGain, master, eqMix, bypassMix;
    float limGain = 1.0f, limRelease = 0.999f;
    DelayLine bypassDelay[2];
    std::vector<float> dryL, dryR;

    juce::AbstractFifo tunerFifo { 16384 };
    std::vector<float> tunerBuffer;
    int tunerDecim = 0;
};
} // namespace pf
