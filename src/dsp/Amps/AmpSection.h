#pragma once
// Amp section: oversampling (1x / 2x / 4x by quality mode), click-free model switching (crossfade),
// latency-compensated bypass and output trim.

#include "AmpModels.h"
#include <juce_dsp/juce_dsp.h>

namespace pf
{
class AmpSection
{
public:
    AmpSection() { trim.snapTo (1.0f); enabledS.snapTo (1.0f); }
    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;

    void setType (int type) noexcept        { requestedType = std::clamp (type, 0, (int) AmpType::Count - 1); }
    void setControls (const AmpControls& c) noexcept { target = c; }
    void setTrimDb (float db) noexcept      { trim.setTarget (dbToGain (db)); }
    void setEnabled (bool e) noexcept       { enabledS.setTarget (e ? 1.0f : 0.0f); }
    void setQuality (int q) noexcept        { requestedQuality = std::clamp (q, 0, 2); }

    void process (float* L, float* R, int n) noexcept;

    /** Integer latency in samples for a given quality mode (0 for Eco). */
    int latencyFor (int quality) const noexcept { return latency[(size_t) std::clamp (quality, 0, 2)]; }
    int activeQuality() const noexcept { return quality; }

private:
    void runModels (float* L, float* R, int n) noexcept;
    void smoothControls() noexcept;

    double sr = 44100.0; int maxBlock = 512;
    std::unique_ptr<juce::dsp::Oversampling<float>> os[3];
    std::unique_ptr<AmpModel> models[3][(int) AmpType::Count];
    int latency[3] { 0, 0, 0 };

    int quality = 1, requestedQuality = 1;
    int type = 0, requestedType = 0, fadeFrom = -1;
    float fadePos = 1.0f, fadeInc = 0.01f;
    int qualityRamp = 0; // >0: fading out before switching quality, <0: fading back in
    AmpControls current, target;
    bool first = true;

    Smoother trim, enabledS;
    std::vector<float> scratchL, scratchR, dryL, dryR;
    DelayLine bypassDelay[2];
};
} // namespace pf
