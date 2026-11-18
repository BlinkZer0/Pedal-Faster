#pragma once
// Pitch effects: dual-tap granular shifter, analogue-style octave divider, and a pitch-tracking harmonizer.

#include "../EffectProcessor.h"

namespace pf
{
//==============================================================================
/** Two crossfaded read taps rotating through a delay buffer (Hann-weighted, sums to unity). */
struct GranularShifter
{
    DelayLine line;
    double phase = 0.0;
    void setMax (int samples) { line.setMaxDelay (samples); }
    void reset() noexcept { line.reset(); phase = 0.0; }

    inline float process (float x, float ratio, float window) noexcept
    {
        line.push (x);
        phase += (1.0 - ratio) / window;
        phase -= std::floor (phase);
        const float p1 = (float) phase;
        float p2 = p1 + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
        const float g1 = std::sin (kPi * p1), g2 = std::sin (kPi * p2);
        return line.readLinear (3.0f + p1 * window) * g1 * g1 + line.readLinear (3.0f + p2 * window) * g2 * g2;
    }
};

class PitchShifter final : public Effect
{
public:
    enum { Semitones, Cents, Grain };
    void setParameter (int i, float v) noexcept override
    {
        if (i == Semitones) semis = v;
        if (i == Cents)     cents = v;
        if (i == Grain)     grainMs.setTarget (v);
    }
    void reset() noexcept override { sh[0].reset(); sh[1].reset(); grainMs.snap(); }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        const float ratio = std::pow (2.0f, (semis + cents / 100.0f) / 12.0f);
        for (int i = 0; i < n; ++i)
        {
            const float w = grainMs.next() * 0.001f * (float) sr;
            L[i] = sh[0].process (L[i], ratio, w);
            R[i] = sh[1].process (R[i], ratio, w);
        }
    }
protected:
    void prepare (double, int) override
    {
        const int maxS = (int) (0.12 * sr) + 16;
        sh[0].setMax (maxS); sh[1].setMax (maxS); grainMs.prepare (sr, 80);
    }
private:
    float semis = 12, cents = 0; Smoother grainMs; GranularShifter sh[2];
};

//==============================================================================
/** Analogue-style octaver: low-passed zero-crossing flip-flops (1 and 2 octaves down)
    and a full-wave rectifier (octave up), tracked by an envelope follower. */
class Octaver final : public Effect
{
public:
    enum { Sub1, Sub2, Up, Direct, Tracking };
    void setParameter (int i, float v) noexcept override
    {
        switch (i)
        {
            case Sub1: sub1.setTarget (v); break;
            case Sub2: sub2.setTarget (v); break;
            case Up: up.setTarget (v); break;
            case Direct: direct.setTarget (v); break;
            case Tracking: tracking = v; dirty = true; break;
            default: break;
        }
    }
    void reset() noexcept override
    {
        sub1.snap(); sub2.snap(); up.snap(); direct.snap();
        lpA.reset(); lpB.reset(); subLp.reset(); sub2Lp.reset(); upHp.reset(); dc.reset(); env.reset();
        flip1 = flip2 = 1.0f; armed = true;
    }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        if (dirty)
        {
            dirty = false;
            const double fc = expMap (1.0f - tracking, 180.0f, 1100.0f);
            lpA.lowpass (sr, fc, 0.6); lpB.lowpass (sr, fc, 0.6);
        }
        for (int i = 0; i < n; ++i)
        {
            const float x = 0.5f * (L[i] + R[i]);
            const float e = env.process (x);
            const float t = lpB.process (lpA.process (x, 0), 0);
            const float hyst = e * 0.08f + 1.0e-4f;
            if (armed && t > hyst)       { armed = false; flip1 = -flip1; if (flip1 > 0) flip2 = -flip2; }
            else if (! armed && t < -hyst) armed = true;

            const float gate = e > 0.002f ? 1.0f : e / 0.002f;
            const float s1 = subLp.lowpass (flip1 * std::abs (t) * 1.6f * gate, 0);
            const float s2 = sub2Lp.lowpass (flip2 * std::abs (t) * 1.6f * gate, 0);
            const float u  = upHp.process (dc.process (std::abs (x) * 2.0f, 0), 0);
            const float wet = s1 * sub1.next() + s2 * sub2.next() + u * up.next();
            const float d = direct.next();
            L[i] = L[i] * d + wet;
            R[i] = R[i] * d + wet;
        }
    }
protected:
    void prepare (double, int) override
    {
        for (auto* s : { &sub1, &sub2, &up, &direct }) s->prepare (sr, 25);
        env.set (sr, 1.0f, 60.0f); subLp.setCutoff (sr, 700); sub2Lp.setCutoff (sr, 450);
        upHp.highpass (sr, 160, 0.7); dc.prepare (sr); dirty = true;
    }
private:
    Smoother sub1, sub2, up, direct; float tracking = 0.5f, flip1 = 1, flip2 = 1; bool armed = true, dirty = true;
    Biquad lpA, lpB, upHp; OnePole subLp, sub2Lp; DCBlocker dc; EnvelopeFollower env;
};

//==============================================================================
/** Harmonizer: an AMDF pitch tracker sets the grain window to a whole number of pitch periods,
    so the two shifted voices splice on period boundaries (PSOLA-like, far less warble than a fixed window). */
class Harmonizer final : public Effect
{
public:
    enum { Interval, Voice2, Level, Spread };
    static const std::vector<std::string>& intervalNames()
    {
        static const std::vector<std::string> n { "-12 Oct down", "-7 5th down", "-5 4th down", "-4 M3 down", "-3 m3 down",
                                                  "+3 Minor 3rd", "+4 Major 3rd", "+5 4th", "+7 5th", "+9 Major 6th", "+12 Octave" };
        return n;
    }
    static const std::vector<std::string>& voice2Names()
    {
        static std::vector<std::string> n = [] { auto v = intervalNames(); v.insert (v.begin(), "Off"); return v; }();
        return n;
    }
    static int semitonesFor (int idx) noexcept
    {
        static const int s[] { -12, -7, -5, -4, -3, 3, 4, 5, 7, 9, 12 };
        return s[std::clamp (idx, 0, 10)];
    }

    void setParameter (int i, float v) noexcept override
    {
        if (i == Interval) ratio1 = std::pow (2.0f, semitonesFor ((int) v) / 12.0f);
        if (i == Voice2)   { const int k = (int) v; v2on = k > 0; if (v2on) ratio2 = std::pow (2.0f, semitonesFor (k - 1) / 12.0f); }
        if (i == Level)    level.setTarget (v);
        if (i == Spread)   spread = v;
    }
    void reset() noexcept override
    {
        for (auto& s : voice) s.reset();
        level.snap(); window.snapTo (0.04f * (float) sr);
        std::fill (analysis.begin(), analysis.end(), 0.0f);
        writePos = 0; decimCount = 0; sinceAnalysis = 0;
    }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const float x = 0.5f * (L[i] + R[i]);
            if (++decimCount >= kDecim) { decimCount = 0; analysis[(size_t) writePos] = aa.lowpass (x, 0); writePos = (writePos + 1) % kAnalysis; }
            else aa.lowpass (x, 0);
            if (++sinceAnalysis >= 512) { sinceAnalysis = 0; analyse(); }

            const float w = window.next();
            const float lv = level.next();
            const float a = voice[0].process (x, ratio1, w);
            const float b = v2on ? voice[1].process (x, ratio2, w) : 0.0f;
            const float panA = 0.5f - 0.5f * spread, panB = 0.5f + 0.5f * spread;
            L[i] = L[i] + lv * (a * (1.0f - panA) * 1.4f + b * (1.0f - panB) * 1.4f);
            R[i] = R[i] + lv * (a * panA * 1.4f + b * panB * 1.4f);
        }
    }
protected:
    void prepare (double, int) override
    {
        const int maxS = (int) (0.12 * sr) + 16;
        for (auto& s : voice) s.setMax (maxS);
        analysis.assign (kAnalysis, 0.0f);
        level.prepare (sr, 30); window.prepare (sr, 15);
        aa.setCutoff (sr, sr / (2.5 * kDecim));
    }
private:
    void analyse() noexcept
    {
        const double dsr = sr / kDecim;
        const int minLag = std::max (2, (int) (dsr / 1000.0)), maxLag = std::min (kAnalysis / 2, (int) (dsr / 60.0));
        const int len = kAnalysis - maxLag;
        float energy = 0.0f;
        for (int k = 0; k < kAnalysis; ++k) energy += analysis[(size_t) k] * analysis[(size_t) k];
        if (energy < 1.0e-5f) return;
        float best = 1.0e30f; int bestLag = 0;
        float prevDiff = 0.0f;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            float diff = 0.0f;
            for (int k = 0; k < len; k += 2)
            {
                const int a = (writePos + k) % kAnalysis, b = (writePos + k + lag) % kAnalysis;
                diff += std::abs (analysis[(size_t) a] - analysis[(size_t) b]);
            }
            if (diff < best * 0.9f || (bestLag == 0 && diff < best)) { best = diff; bestLag = lag; }
            prevDiff = diff;
        }
        (void) prevDiff;
        if (bestLag > 0)
        {
            const float period = (float) bestLag * kDecim;
            const float target = 0.035f * (float) sr;
            const float periods = std::max (1.0f, std::round (target / period));
            window.setTarget (clampf (periods * period, 0.012f * (float) sr, 0.09f * (float) sr));
        }
    }

    static constexpr int kDecim = 4, kAnalysis = 1024;
    float ratio1 = 1.5f, ratio2 = 1.26f, spread = 0.5f; bool v2on = false;
    Smoother level, window; GranularShifter voice[2]; OnePole aa;
    std::vector<float> analysis; int writePos = 0, decimCount = 0, sinceAnalysis = 0;
};
} // namespace pf
