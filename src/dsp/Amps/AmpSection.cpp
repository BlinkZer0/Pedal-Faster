#include "AmpSection.h"

namespace pf
{
void AmpSection::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate; maxBlock = maxBlockSize;
    using OS = juce::dsp::Oversampling<float>;
    for (int q = 0; q < 3; ++q)
    {
        const int factorLog2 = q; // Eco 1x, Normal 2x, High 4x
        double osRate = sr;
        if (factorLog2 > 0)
        {
            os[q] = std::make_unique<OS> (2, (size_t) factorLog2, OS::filterHalfBandPolyphaseIIR, true, true);
            os[q]->initProcessing ((size_t) maxBlock);
            latency[q] = (int) std::lround (os[q]->getLatencyInSamples());
            osRate = sr * (1 << factorLog2);
        }
        else { os[q].reset(); latency[q] = 0; }

        for (int t = 0; t < (int) AmpType::Count; ++t)
        {
            models[q][t] = AmpModel::create ((AmpType) t);
            models[q][t]->prepare (osRate);
        }
    }
    const size_t osMax = (size_t) maxBlock * 4 + 16;
    scratchL.assign (osMax, 0.0f); scratchR.assign (osMax, 0.0f);
    dryL.assign ((size_t) maxBlock + 16, 0.0f); dryR.assign ((size_t) maxBlock + 16, 0.0f);
    for (auto& d : bypassDelay) d.setMaxDelay (64);
    trim.prepare (sr, 20); enabledS.prepare (sr, 15);
    fadeInc = (float) (1.0 / (0.02 * sr));
    first = true;
    reset();
}

void AmpSection::reset() noexcept
{
    quality = requestedQuality; type = requestedType; fadeFrom = -1; fadePos = 1.0f; qualityRamp = 0;
    current = target;
    for (int q = 0; q < 3; ++q)
    {
        if (os[q]) os[q]->reset();
        for (auto& m : models[q]) if (m) { m->setControls (current); m->reset(); }
    }
    for (auto& d : bypassDelay) d.reset();
    trim.snap(); enabledS.snap();
}

void AmpSection::smoothControls() noexcept
{
    if (first) { current = target; first = false; }
    else
    {
        auto sm = [] (float& c, float t) { c += (t - c) * 0.35f; if (std::abs (t - c) < 1.0e-4f) c = t; };
        sm (current.gain, target.gain); sm (current.bass, target.bass); sm (current.mid, target.mid);
        sm (current.treble, target.treble); sm (current.presence, target.presence); sm (current.master, target.master);
        sm (current.sag, target.sag);
    }
}

void AmpSection::runModels (float* L, float* R, int n) noexcept
{
    auto& set = models[quality];
    set[type]->setControls (current);
    if (fadeFrom >= 0)
    {
        set[fadeFrom]->setControls (current);
        std::copy (L, L + n, scratchL.begin()); std::copy (R, R + n, scratchR.begin());
        set[fadeFrom]->process (scratchL.data(), scratchR.data(), n);
        set[type]->process (L, R, n);
        const float inc = fadeInc / (float) (1 << quality);
        for (int i = 0; i < n; ++i)
        {
            fadePos = std::min (1.0f, fadePos + inc);
            const float a = std::sin (fadePos * kPi * 0.5f), b = std::cos (fadePos * kPi * 0.5f);
            L[i] = L[i] * a + scratchL[(size_t) i] * b;
            R[i] = R[i] * a + scratchR[(size_t) i] * b;
        }
        if (fadePos >= 1.0f) fadeFrom = -1;
    }
    else
        set[type]->process (L, R, n);
}

void AmpSection::process (float* L, float* R, int n) noexcept
{
    smoothControls();

    // quality switch: fade out on the old path, switch, fade in on the new one
    if (requestedQuality != quality && qualityRamp == 0) qualityRamp = 1;

    if (requestedType != type && fadeFrom < 0)
    {
        fadeFrom = type; type = requestedType; fadePos = 0.0f;
        models[quality][type]->setControls (current);
        models[quality][type]->reset();
    }

    std::copy (L, L + n, dryL.begin()); std::copy (R, R + n, dryR.begin());

    const bool active = enabledS.current > 0.0001f || enabledS.target > 0.0f;
    if (active)
    {
        if (os[quality])
        {
            float* chans[2] { L, R };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
            auto up = os[quality]->processSamplesUp (block);
            runModels (up.getChannelPointer (0), up.getChannelPointer (1), (int) up.getNumSamples());
            os[quality]->processSamplesDown (block);
        }
        else
            runModels (L, R, n);
    }

    // latency-compensated dry path for bypass
    const int lat = latency[quality];
    for (int i = 0; i < n; ++i)
    {
        float dl = dryL[(size_t) i], dr = dryR[(size_t) i];
        if (lat > 0)
        {
            bypassDelay[0].push (dl); bypassDelay[1].push (dr);
            dl = bypassDelay[0].readInt (lat + 1); dr = bypassDelay[1].readInt (lat + 1);
        }
        const float e = enabledS.next(), t = trim.next();
        float wl = active ? L[i] * t : 0.0f, wr = active ? R[i] * t : 0.0f;
        L[i] = dl + (wl - dl) * e;
        R[i] = dr + (wr - dr) * e;
    }

    if (qualityRamp != 0)
    {
        // short linear fade over this block
        for (int i = 0; i < n; ++i)
        {
            const float g = qualityRamp > 0 ? 1.0f - (float) i / (float) n : (float) i / (float) n;
            L[i] *= g; R[i] *= g;
        }
        if (qualityRamp > 0)
        {
            quality = requestedQuality;
            if (os[quality]) os[quality]->reset();
            for (auto& m : models[quality]) { m->setControls (current); m->reset(); }
            for (auto& d : bypassDelay) d.reset();
            fadeFrom = -1; fadePos = 1.0f;
            qualityRamp = -1;
        }
        else qualityRamp = 0;
    }
}
} // namespace pf
