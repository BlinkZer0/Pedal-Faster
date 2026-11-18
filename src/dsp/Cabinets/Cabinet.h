#pragma once
// Cabinet section. Factory cabinets are original filter-based voicings (speaker resonance, cone
// break-up peaks/notches, roll-off, open-back comb); a custom WAV impulse response runs through
// zero-latency partitioned convolution. Mic position, low/high cut, level and wet/dry apply to both.

#include "../DspCommon.h"
#include <juce_dsp/juce_dsp.h>

namespace pf
{
enum class CabType { Open1x12 = 0, Open2x12, Closed4x12, Bass4x10, Bass1x15, Direct, CustomIR, Count };

const std::vector<std::string>& cabTypeIds();   // "open_1x12", ...
const std::vector<std::string>& cabTypeNames();
int cabTypeIndex (const std::string& id);

class CabinetSection
{
public:
    CabinetSection() { level.snapTo (1.0f); mixS.snapTo (1.0f); enabledS.snapTo (1.0f); }
    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;

    void setType (int t) noexcept      { requestedType = std::clamp (t, 0, (int) CabType::Count - 1); }
    void setMic (float m) noexcept     { if (m != mic) { mic = m; dirty = true; } }
    void setLowCut (float hz) noexcept { if (hz != lowCut) { lowCut = hz; dirty = true; } }
    void setHighCut (float hz) noexcept{ if (hz != highCut) { highCut = hz; dirty = true; } }
    void setLevelDb (float db) noexcept{ level.setTarget (dbToGain (db)); }
    void setMix (float m) noexcept     { mixS.setTarget (m); }
    void setEnabled (bool e) noexcept  { enabledS.setTarget (e ? 1.0f : 0.0f); }

    /** Message thread: hands an IR to the convolution engine (loaded in the background, wait-free). */
    void loadImpulseResponse (juce::AudioBuffer<float>&& ir, double irSampleRate);
    bool hasCustomIR() const noexcept { return irLoaded.load(); }
    void clearCustomIR() { irLoaded = false; }

    void process (float* L, float* R, int n) noexcept;

private:
    struct Voicing
    {
        static constexpr int kMax = 8;
        Biquad f[kMax]; int count = 0;
        int combDelay = 1; float combGain = 0.0f; // the comb's delay line lives outside (no allocation on copy)
        void reset() noexcept { for (auto& b : f) b.reset(); }
        inline float process (float x, int ch, DelayLine* combLine) noexcept
        {
            if (combLine != nullptr && combGain != 0.0f) { combLine->push (x); x = x + combGain * combLine->readInt (combDelay); }
            for (int i = 0; i < count; ++i) x = f[i].process (x, ch);
            return x;
        }
    };
    void design (Voicing& v, int type) const;
    void processVoicing (Voicing& v, int ch, float* data, int n, DelayLine& combLine) noexcept;

    double sr = 44100.0; int maxBlock = 512;
    int type = (int) CabType::Closed4x12, requestedType = (int) CabType::Closed4x12, fadeFrom = -1;
    float fadePos = 1.0f, fadeInc = 0.01f;
    float mic = 0.3f, lowCut = 70.0f, highCut = 9000.0f; bool dirty = true;
    Voicing cur, prev; DelayLine combCur[2], combPrev[2];
    Biquad lowCutF, highCutF;
    Smoother level, mixS, enabledS;
    juce::dsp::Convolution convolution;
    std::atomic<bool> irLoaded { false };
    std::vector<float> dryL, dryR, tmpL, tmpR;
};
} // namespace pf
