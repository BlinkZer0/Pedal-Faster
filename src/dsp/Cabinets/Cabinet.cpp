#include "Cabinet.h"

namespace pf
{
const std::vector<std::string>& cabTypeIds()
{
    static const std::vector<std::string> ids { "open_1x12", "open_2x12", "closed_4x12", "bass_4x10", "bass_1x15", "direct", "custom_ir" };
    return ids;
}
const std::vector<std::string>& cabTypeNames()
{
    static const std::vector<std::string> n { "1x12 Open-Back", "2x12 Open-Back", "4x12 Closed-Back", "4x10 Bass", "1x15 Bass", "Direct (No Cab)", "Custom IR" };
    return n;
}
int cabTypeIndex (const std::string& id)
{
    const auto& ids = cabTypeIds();
    for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) return (int) i;
    return -1;
}

void CabinetSection::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate; maxBlock = maxBlockSize;
    for (auto& c : combCur) c.setMaxDelay ((int) (0.002 * sr) + 8);
    for (auto& c : combPrev) c.setMaxDelay ((int) (0.002 * sr) + 8);
    dryL.assign ((size_t) maxBlock + 8, 0.0f); dryR = dryL; tmpL = dryL; tmpR = dryL;
    level.prepare (sr, 20); mixS.prepare (sr, 20); enabledS.prepare (sr, 15);
    fadeInc = (float) (1.0 / (0.02 * sr));
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };
    convolution.prepare (spec);
    dirty = true;
    reset();
}

void CabinetSection::reset() noexcept
{
    type = requestedType; fadeFrom = -1; fadePos = 1.0f;
    design (cur, type);
    cur.reset(); prev.reset();
    for (auto& c : combCur) c.reset();
    for (auto& c : combPrev) c.reset();
    lowCutF.reset(); highCutF.reset();
    convolution.reset();
    level.snap(); mixS.snap(); enabledS.snap();
}

void CabinetSection::loadImpulseResponse (juce::AudioBuffer<float>&& ir, double irSampleRate)
{
    const bool stereo = ir.getNumChannels() > 1;
    convolution.loadImpulseResponse (std::move (ir), irSampleRate,
                                     stereo ? juce::dsp::Convolution::Stereo::yes : juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::yes, juce::dsp::Convolution::Normalise::yes);
    irLoaded = true;
}

void CabinetSection::design (Voicing& v, int t) const
{
    const double m = mic;
    int c = 0;
    auto hp = [&] (double f, double q) { v.f[c++].highpass (sr, f, q); };
    auto lp = [&] (double f, double q) { v.f[c++].lowpass (sr, f, q); };
    auto pk = [&] (double f, double q, double g) { v.f[c++].peak (sr, f, q, g); };
    v.combGain = 0.0f; v.combDelay = 1;
    switch ((CabType) t)
    {
        case CabType::Open1x12:
            hp (85, 0.9); pk (120, 1.0, 1.5); pk (400, 0.8, -2.0); pk (2200, 1.2, 3.0 - 3.0 * m);
            pk (4200, 3.0, -4.0); lp (5800 - 1800 * m, 0.8); lp (7500 - 2000 * m, 0.6);
            v.combGain = -0.22f; v.combDelay = (int) (0.00035 * sr); break;
        case CabType::Open2x12:
            hp (75, 1.0); pk (110, 1.1, 2.5); pk (500, 0.7, -2.5); pk (3000, 1.5, 3.5 - 4.0 * m);
            pk (5000, 3.0, -5.0); lp (5600 - 1600 * m, 0.8); lp (7200 - 1800 * m, 0.6);
            v.combGain = -0.18f; v.combDelay = (int) (0.0005 * sr); break;
        case CabType::Closed4x12:
            hp (70, 1.4); pk (105, 1.2, 3.5); pk (420, 0.9, -3.5); pk (2300, 1.2, 3.0);
            pk (3400, 2.0, 3.0 - 4.0 * m); pk (4600, 3.0, -6.0); lp (5200 - 1500 * m, 0.9); lp (6500, 0.6); break;
        case CabType::Bass4x10:
            hp (45, 1.0); pk (80, 1.0, 2.0); pk (350, 0.8, -2.0); pk (1500, 1.0, 2.0);
            pk (3000, 1.5, 2.0 - 3.0 * m); lp (6500 - 1500 * m, 0.7); lp (9000, 0.6); break;
        case CabType::Bass1x15:
            hp (38, 1.1); pk (70, 1.1, 3.0); pk (600, 0.7, -2.0); pk (1200, 1.0, 1.5);
            lp (3500 - 900 * m, 0.8); lp (5000, 0.6); break;
        case CabType::CustomIR:
            v.f[c++].highShelf (sr, 3000, 0.7, -8.0 * m); break; // mic acts as off-axis darkening on IRs
        case CabType::Direct:
        default: break;
    }
    v.count = c;
}

void CabinetSection::processVoicing (Voicing& v, int ch, float* data, int n, DelayLine& combLine) noexcept
{
    for (int i = 0; i < n; ++i) data[i] = v.process (data[i], ch, &combLine);
}

void CabinetSection::process (float* L, float* R, int n) noexcept
{
    if (requestedType != type && fadeFrom < 0)
    {
        prev = cur; fadeFrom = type;
        for (int c = 0; c < 2; ++c) combPrev[c] = combCur[c];
        type = requestedType; fadePos = 0.0f;
        design (cur, type); cur.reset();
        for (auto& c : combCur) c.reset();
    }
    if (dirty)
    {
        dirty = false;
        // re-design keeps filter state (coefficients only), so mic moves are smooth
        Voicing tmp; design (tmp, type);
        for (int i = 0; i < Voicing::kMax; ++i)
        {
            cur.f[i].b0 = tmp.f[i].b0; cur.f[i].b1 = tmp.f[i].b1; cur.f[i].b2 = tmp.f[i].b2;
            cur.f[i].a1 = tmp.f[i].a1; cur.f[i].a2 = tmp.f[i].a2;
        }
        cur.count = tmp.count; cur.combGain = tmp.combGain; cur.combDelay = tmp.combDelay;
        lowCutF.highpass (sr, lowCut, 0.7);
        highCutF.lowpass (sr, highCut, 0.7);
    }

    std::copy (L, L + n, dryL.begin()); std::copy (R, R + n, dryR.begin());
    const bool fading = fadeFrom >= 0;
    if (fading) { std::copy (L, L + n, tmpL.begin()); std::copy (R, R + n, tmpR.begin()); }

    // current voicing (+ convolution for a loaded custom IR)
    const bool useIR = type == (int) CabType::CustomIR && irLoaded.load();
    if (type == (int) CabType::CustomIR && ! useIR)
    {
        // no IR loaded: fall back to the closed 4x12 voicing so the rig never goes un-cabbed
        Voicing fb; design (fb, (int) CabType::Closed4x12);
        for (int i = 0; i < fb.count; ++i) { cur.f[i].b0 = fb.f[i].b0; cur.f[i].b1 = fb.f[i].b1; cur.f[i].b2 = fb.f[i].b2; cur.f[i].a1 = fb.f[i].a1; cur.f[i].a2 = fb.f[i].a2; }
        cur.count = fb.count; cur.combGain = 0.0f;
    }
    if (useIR)
    {
        float* chans[2] { L, R };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
    }
    processVoicing (cur, 0, L, n, combCur[0]);
    processVoicing (cur, 1, R, n, combCur[1]);

    if (fading)
    {
        processVoicing (prev, 0, tmpL.data(), n, combPrev[0]);
        processVoicing (prev, 1, tmpR.data(), n, combPrev[1]);
        for (int i = 0; i < n; ++i)
        {
            fadePos = std::min (1.0f, fadePos + fadeInc);
            const float a = std::sin (fadePos * kPi * 0.5f), b = std::cos (fadePos * kPi * 0.5f);
            L[i] = L[i] * a + tmpL[(size_t) i] * b;
            R[i] = R[i] * a + tmpR[(size_t) i] * b;
        }
        if (fadePos >= 1.0f) fadeFrom = -1;
    }

    for (int i = 0; i < n; ++i)
    {
        const float lv = level.next(), mx = mixS.next(), e = enabledS.next();
        const float wl = highCutF.process (lowCutF.process (L[i], 0), 0) * lv;
        const float wr = highCutF.process (lowCutF.process (R[i], 1), 1) * lv;
        const float outL = dryL[(size_t) i] + (wl - dryL[(size_t) i]) * mx;
        const float outR = dryR[(size_t) i] + (wr - dryR[(size_t) i]) * mx;
        L[i] = dryL[(size_t) i] + (outL - dryL[(size_t) i]) * e;
        R[i] = dryR[(size_t) i] + (outR - dryR[(size_t) i]) * e;
    }
}
} // namespace pf
