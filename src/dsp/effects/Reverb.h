#pragma once
// Reverbs: spring (dispersive allpass loop), plate (Dattorro figure-eight tank), hall (8-line FDN with
// Householder mixing, modulation and optional octave-up shimmer inside the feedback path).

#include "../EffectProcessor.h"
#include "Pitch.h"

namespace pf
{
//==============================================================================
class SpringReverb final : public Effect
{
public:
    enum { Decay, Tone, Character, Dwell };
    void setParameter (int i, float v) noexcept override
    {
        if (i == Decay)     decay = v;
        if (i == Tone)      { tone = v; dirty = true; }
        if (i == Character) character = v;
        if (i == Dwell)     dwell = v;
    }
    void reset() noexcept override
    {
        for (auto& s : springs) { s.loop.reset(); for (auto& a : s.disp) a.reset(); s.fb = 0; s.damp.reset(); }
        inHp.reset(); outLp.reset(); outHp.reset();
    }
    bool hasTail() const noexcept override { return true; }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        if (dirty) { dirty = false; outLp.lowpass (sr, expMap (tone, 1800.0f, 7000.0f), 0.7); }
        const float a = -0.55f - character * 0.25f;
        const float dg = 1.0f + dwell * 6.0f;
        float gk[2];
        for (int k = 0; k < 2; ++k) gk[k] = std::pow (10.0f, -3.0f * (float) springs[k].length / (float) sr / std::max (0.2f, decay));
        for (int i = 0; i < n; ++i)
        {
            const float x = fastTanh (inHp.process (0.5f * (L[i] + R[i]), 0) * dg) / std::sqrt (dg);
            float out[2];
            for (int k = 0; k < 2; ++k)
            {
                auto& s = springs[k];
                float y = x + s.fb;
                for (int st = 0; st < kStages; ++st) y = s.disp[st].process (y, a, 0);
                s.loop.push (undenormal (y));
                const float tap = s.loop.readInt (s.length);
                s.fb = undenormal (s.damp.lowpass (tap, 0) * gk[k]);
                // the "drip": a second, shorter reflection that grows with character
                out[k] = tap + character * 0.4f * s.loop.readInt (s.length / 3);
            }
            L[i] = outHp.process (outLp.process (out[0], 0), 0) * 0.8f;
            R[i] = outHp.process (outLp.process (out[1], 1), 1) * 0.8f;
        }
    }
protected:
    void prepare (double, int) override
    {
        springs[0].length = (int) (0.0331 * sr); springs[1].length = (int) (0.0417 * sr);
        for (auto& s : springs) { s.loop.setMaxDelay (s.length + 8); s.damp.setCutoff (sr, 4200); }
        inHp.highpass (sr, 250, 0.7); outHp.highpass (sr, 180, 0.7); dirty = true;
    }
private:
    static constexpr int kStages = 14;
    struct Spring { DelayLine loop; FirstOrderAllpass disp[kStages]; OnePole damp; int length = 1000; float fb = 0; };
    Spring springs[2];
    float decay = 2, tone = 0.5f, character = 0.5f, dwell = 0.3f; bool dirty = true;
    Biquad inHp, outLp, outHp;
};

//==============================================================================
class PlateReverb final : public Effect
{
public:
    enum { Decay, Predelay, Damping, LowCut, Modulation };
    void setParameter (int i, float v) noexcept override
    {
        if (i == Decay)      decay = v;
        if (i == Predelay)   predelayMs.setTarget (v);
        if (i == Damping)    { damping = v; dirty = true; }
        if (i == LowCut)     { lowCut = v; dirty = true; }
        if (i == Modulation) modulation = v;
    }
    void reset() noexcept override
    {
        pre.reset(); bw.reset(); for (auto& d : inDiff) d.reset();
        for (auto& t : tank) { t.modAp.reset(); t.d1.reset(); t.ap2.reset(); t.d2.reset(); t.damp.reset(); }
        fbL = fbR = 0; outHp.reset(); predelayMs.snap();
    }
    bool hasTail() const noexcept override { return true; }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        if (dirty) { dirty = false; for (auto& t : tank) t.damp.setCutoff (sr, expMap (1.0f - damping, 1500.0f, 16000.0f)); outHp.highpass (sr, lowCut, 0.7); }
        // tank half-loop length ~ 0.36 s at the reference rate
        const float g = std::pow (10.0f, -3.0f * 0.18f / std::max (0.2f, decay));
        const float excursion = 16.0f * scale * modulation;
        for (int i = 0; i < n; ++i)
        {
            lfo.advance (0.9 / sr);
            pre.push (0.5f * (L[i] + R[i]));
            float x = bw.lowpass (pre.readLinear (std::max (1.0f, predelayMs.next() * 0.001f * (float) sr)), 0);
            for (auto& d : inDiff) x = d.process (x);

            // left half of the figure-eight
            auto& a = tank[0];
            float y = a.modAp.process (x + fbR * g, excursion * lfo.value (LfoShape::Sine));
            a.d1.push (undenormal (y));
            y = a.damp.lowpass (a.d1.readInt (a.len1), 0) * g;
            y = a.ap2.process (y);
            a.d2.push (undenormal (y));
            const float leftOut = a.d2.readInt (a.len2);

            auto& b = tank[1];
            float z = b.modAp.process (x + leftOut * g, excursion * lfo.value (LfoShape::Sine, 0.25));
            b.d1.push (undenormal (z));
            z = b.damp.lowpass (b.d1.readInt (b.len1), 0) * g;
            z = b.ap2.process (z);
            b.d2.push (undenormal (z));
            fbR = b.d2.readInt (b.len2);
            fbL = leftOut;

            auto tap = [this] (DelayLine& d, int ref) { return d.readInt (std::max (1, (int) (ref * scale))); };
            float outL = tap (b.d1, 266) + tap (b.d1, 2974) - tap (b.ap2.line, 1913) + tap (b.d2, 1996)
                       - tap (a.d1, 1990) - tap (a.ap2.line, 187) - tap (a.d2, 1066);
            float outR = tap (a.d1, 353) + tap (a.d1, 3627) - tap (a.ap2.line, 1228) + tap (a.d2, 2673)
                       - tap (b.d1, 2111) - tap (b.ap2.line, 335) - tap (b.d2, 121);
            L[i] = outHp.process (outL * 0.6f, 0);
            R[i] = outHp.process (outR * 0.6f, 1);
        }
    }
protected:
    void prepare (double, int) override
    {
        scale = (float) (sr / 29761.0);
        auto S = [this] (int v) { return std::max (2, (int) std::lround (v * scale)); };
        pre.setMaxDelay ((int) (0.26 * sr) + 8);
        bw.setCutoff (sr, 9000);
        const int inLen[4] { 142, 107, 379, 277 }; const float inG[4] { 0.75f, 0.75f, 0.625f, 0.625f };
        for (int k = 0; k < 4; ++k) { inDiff[k].setMax (S (inLen[k]) + 4); inDiff[k].delay = (float) S (inLen[k]); inDiff[k].gain = inG[k]; }
        const int modLen[2] { 672, 908 }, l1[2] { 4453, 4217 }, apl[2] { 1800, 2656 }, l2[2] { 3720, 3163 };
        // tank[0] uses the 4217/2656/3163 set, tank[1] the 4453/1800/3720 set (as in Dattorro's tap table).
        const int order[2] { 1, 0 };
        for (int k = 0; k < 2; ++k)
        {
            const int o = order[k];
            auto& t = tank[k];
            t.modAp.setMax (S (modLen[o]) + 40); t.modAp.delay = (float) S (modLen[o]); t.modAp.gain = -0.7f;
            t.len1 = S (l1[o]); t.d1.setMaxDelay (t.len1 + 4);
            t.ap2.setMax (S (apl[o]) + 4); t.ap2.delay = (float) S (apl[o]); t.ap2.gain = 0.5f;
            t.len2 = S (l2[o]); t.d2.setMaxDelay (t.len2 + 4);
        }
        predelayMs.prepare (sr, 80); dirty = true;
    }
private:
    struct Tank { DiffuserAllpass modAp; DelayLine d1; DiffuserAllpass ap2; DelayLine d2; OnePole damp; int len1 = 1, len2 = 1; };
    float decay = 2.5f, damping = 0.4f, lowCut = 120, modulation = 0.5f, scale = 1, fbL = 0, fbR = 0; bool dirty = true;
    Smoother predelayMs; DelayLine pre; OnePole bw; DiffuserAllpass inDiff[4]; Tank tank[2]; Biquad outHp; Lfo lfo;
};

//==============================================================================
class HallReverb final : public Effect
{
public:
    enum { Size, Decay, Diffusion, Damping, Predelay, Modulation, Shimmer, LowCut };
    void setParameter (int i, float v) noexcept override
    {
        switch (i)
        {
            case Size: size = v; sizeDirty = true; break;
            case Decay: decay = v; break;
            case Diffusion: diffusion = v; break;
            case Damping: damping = v; dirty = true; break;
            case Predelay: predelayMs.setTarget (v); break;
            case Modulation: modulation = v; break;
            case Shimmer: shimmer.setTarget (v); break;
            case LowCut: lowCut = v; dirty = true; break;
            default: break;
        }
    }
    void reset() noexcept override
    {
        for (auto& l : lines) l.reset();
        for (auto& d : diff) d.reset();
        for (auto& f : damp) f.reset();
        for (auto& s : state) s = 0;
        pre.reset(); shifter.reset(); shimHp.reset(); outHp.reset(); predelayMs.snap(); shimmer.snap(); sizeS.snap(); shimFb = 0;
    }
    bool hasTail() const noexcept override { return true; }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        static const float baseMs[kLines] { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f };
        if (dirty)
        {
            dirty = false;
            const float fc = expMap (1.0f - damping, 1500.0f, 16000.0f);
            for (auto& f : damp) f.setCutoff (sr, fc);
            outHp.highpass (sr, lowCut, 0.7);
        }
        if (sizeDirty) { sizeDirty = false; sizeS.setTarget (0.35f + size * 1.25f); }
        for (int k = 0; k < 4; ++k) diff[k].gain = 0.25f + 0.45f * diffusion;
        const float modDepth = modulation * 0.0025f * (float) sr;
        const float sz = sizeS.current;
        float baseDly[kLines], gk[kLines];
        for (int k = 0; k < kLines; ++k)
        {
            baseDly[k] = baseMs[k] * 0.001f * (float) sr * sz;
            gk[k] = std::pow (10.0f, -3.0f * baseDly[k] / (float) sr / std::max (0.3f, decay));
        }
        for (int i = 0; i < n; ++i)
        {
            lfo.advance (0.31 / sr); lfo2.advance (0.43 / sr);
            sizeS.next();
            pre.push (0.5f * (L[i] + R[i]));
            float x = pre.readLinear (std::max (1.0f, predelayMs.next() * 0.001f * (float) sr));
            for (auto& d : diff) x = d.process (x);
            x += shimFb;

            float outs[kLines];
            float sum = 0.0f;
            for (int k = 0; k < kLines; ++k)
            {
                float dly = baseDly[k];
                if (k == 1) dly += modDepth * lfo.value (LfoShape::Sine);
                if (k == 6) dly += modDepth * lfo2.value (LfoShape::Sine);
                outs[k] = damp[k].lowpass (lines[k].readLinear (dly), 0) * gk[k];
                sum += outs[k];
            }
            const float h = 2.0f / kLines * sum; // Householder reflection: y = x - 2/N * sum
            for (int k = 0; k < kLines; ++k)
                lines[k].push (undenormal (outs[k] - h + ((k & 1) ? -x : x) * 0.5f));

            const float wetL = outs[0] - outs[2] + outs[4] - outs[6];
            const float wetR = outs[1] - outs[3] + outs[5] - outs[7];
            const float sh = shimmer.next();
            shimFb = sh > 0.001f ? 0.5f * fastTanh (2.0f * shimHp.process (shifter.process (0.5f * (wetL + wetR), 2.0f, 0.06f * (float) sr), 0) * sh * 0.25f) : 0.0f;
            L[i] = outHp.process (wetL * 0.45f, 0);
            R[i] = outHp.process (wetR * 0.45f, 1);
        }
    }
protected:
    void prepare (double, int) override
    {
        const int maxS = (int) (0.0731 * 1.6 * sr + 0.003 * sr) + 64;
        for (auto& l : lines) l.setMaxDelay (maxS);
        const float diffMs[4] { 4.7f, 3.6f, 12.7f, 9.3f };
        for (int k = 0; k < 4; ++k) { const int d = (int) (diffMs[k] * 0.001 * sr); diff[k].setMax (d + 4); diff[k].delay = (float) d; }
        pre.setMaxDelay ((int) (0.26 * sr) + 8);
        shifter.setMax ((int) (0.1 * sr) + 16);
        shimHp.highpass (sr, 400, 0.7);
        predelayMs.prepare (sr, 80); shimmer.prepare (sr, 50); sizeS.prepare (sr, 400); sizeS.snapTo (0.35f + size * 1.25f);
        dirty = true;
    }
private:
    static constexpr int kLines = 8;
    float size = 0.6f, decay = 3.5f, diffusion = 0.7f, damping = 0.4f, modulation = 0.4f, lowCut = 100, shimFb = 0, state[kLines] {};
    bool dirty = true, sizeDirty = true;
    Smoother predelayMs, shimmer, sizeS; DelayLine lines[kLines], pre; DiffuserAllpass diff[4]; OnePole damp[kLines];
    GranularShifter shifter; Biquad shimHp, outHp; Lfo lfo, lfo2;
};
} // namespace pf
