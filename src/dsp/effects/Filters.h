#pragma once
// Filter pedals (wah family) and equalisers.

#include "../EffectProcessor.h"

namespace pf
{
//==============================================================================
/** Wah: pedal-position controlled resonant band-pass (TPT SVF) with a little low-pass bleed, optional pre-drive. */
class Wah final : public Effect
{
public:
    enum { Position, Resonance, Drive, Range };
    void setParameter (int i, float v) noexcept override
    {
        if (i == Position)  pos.setTarget (v);
        if (i == Resonance) q = 1.5f + v * 11.0f;
        if (i == Drive)     drive = v;
        if (i == Range)     range = (int) v;
    }
    /** MIDI expression pedal override (0..1), or -1 to release. */
    void setExpression (float v) noexcept { expression = v; }
    void reset() noexcept override { pos.snap(); svf.reset(); }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        const float lo = range == 0 ? 350.0f : (range == 1 ? 250.0f : 450.0f);
        const float hi = range == 0 ? 2200.0f : (range == 1 ? 1600.0f : 3200.0f);
        const float dg = 1.0f + drive * 12.0f;
        for (int i = 0; i < n; i += 16)
        {
            if (expression >= 0.0f) pos.setTarget (expression);
            const int m = std::min (16, n - i);
            float p = 0.0f;
            for (int k = 0; k < m; ++k) p = pos.next();
            svf.set ((float) sr, expMap (p, lo, hi), q);
            for (int k = i; k < i + m; ++k)
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* s = ch == 0 ? L : R;
                    float x = s[k];
                    if (drive > 0.001f) x = fastTanh (x * dg) / std::sqrt (dg);
                    svf.tick (x, ch);
                    s[k] = (svf.bp * 2.2f + svf.lp * 0.25f) * 0.9f;
                }
        }
    }
protected:
    void prepare (double, int) override { pos.prepare (sr, 12); }
private:
    Smoother pos; float q = 6, drive = 0, expression = -1.0f; int range = 0; SVF svf;
};

//==============================================================================
/** Auto-wah: LFO swept (free or tempo synced) resonant filter, with envelope added on top. */
class AutoWah final : public Effect
{
public:
    enum { Rate, Sync, Depth, Resonance, Sensitivity, Centre, Mode };
    void setParameter (int i, float v) noexcept override
    {
        switch (i)
        {
            case Rate: rate = v; break;
            case Sync: sync = (int) v; break;
            case Depth: depth = v; break;
            case Resonance: q = 0.8f + v * 12.0f; break;
            case Sensitivity: sens = v; break;
            case Centre: centre = v; break;
            case Mode: mode = (int) v; break;
            default: break;
        }
    }
    void reset() noexcept override { svf.reset(); env.reset(); lfo.phase = 0; }
    void process (float* L, float* R, int n, const ProcessContext& ctx) noexcept override
    {
        double hz = rate;
        if (sync > 0) hz = ctx.bpm / 60.0 / syncBeats (sync);
        const double inc = hz / sr;
        for (int i = 0; i < n; i += 16)
        {
            const int m = std::min (16, n - i);
            float e = 0.0f;
            for (int k = i; k < i + m; ++k) e = env.process (std::max (std::abs (L[k]), std::abs (R[k])));
            lfo.advance (inc * m);
            const float l = 0.5f + 0.5f * lfo.value (LfoShape::Sine, -0.25);
            float t = centre + depth * (l - 0.5f) * 1.6f + sens * std::min (1.0f, e * 6.0f);
            svf.set ((float) sr, expMap (clampf (t, 0.0f, 1.0f), 180.0f, 4200.0f), q);
            for (int k = i; k < i + m; ++k)
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* s = ch == 0 ? L : R;
                    svf.tick (s[k], ch);
                    s[k] = mode == 0 ? svf.bp * 2.0f : (mode == 1 ? svf.lp : svf.lp + svf.bp);
                }
        }
    }
protected:
    void prepare (double, int) override { env.set (sr, 4.0f, 120.0f); }
private:
    float rate = 1.5f, depth = 0.7f, q = 5, sens = 0.2f, centre = 0.45f; int sync = 0, mode = 0;
    SVF svf; EnvelopeFollower env; Lfo lfo;
};

//==============================================================================
/** Envelope filter: attack/decay envelope follower drives the filter cutoff up or down. */
class EnvelopeFilter final : public Effect
{
public:
    enum { Sensitivity, Attack, Decay, Q, Direction, Mode, Base };
    void setParameter (int i, float v) noexcept override
    {
        switch (i)
        {
            case Sensitivity: sens = v; break;
            case Attack: attack = v; dirty = true; break;
            case Decay: decay = v; dirty = true; break;
            case Q: q = v; break;
            case Direction: up = v < 0.5f; break;
            case Mode: mode = (int) v; break;
            case Base: base = v; break;
            default: break;
        }
    }
    void reset() noexcept override { svf.reset(); env.reset(); }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        if (dirty) { dirty = false; env.set (sr, attack, decay); }
        const float gainIn = 2.0f + sens * 40.0f;
        for (int i = 0; i < n; i += 8)
        {
            const int m = std::min (8, n - i);
            float e = 0.0f;
            for (int k = i; k < i + m; ++k) e = env.process (0.5f * (L[k] + R[k]) * gainIn);
            const float amt = clampf (e, 0.0f, 1.0f);
            const float t = up ? base + (1.0f - base) * amt : base * (1.0f - amt);
            svf.set ((float) sr, expMap (t, 150.0f, 5000.0f), q);
            for (int k = i; k < i + m; ++k)
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* s = ch == 0 ? L : R;
                    svf.tick (s[k], ch);
                    s[k] = mode == 0 ? svf.lp : (mode == 1 ? svf.bp * 1.8f : svf.hp);
                }
        }
    }
protected:
    void prepare (double, int) override { dirty = true; }
private:
    float sens = 0.5f, attack = 8, decay = 250, q = 4, base = 0.1f; bool up = true, dirty = true; int mode = 0;
    SVF svf; EnvelopeFollower env;
};

//==============================================================================
/** 7-band graphic EQ (constant-Q peaking bands one octave apart). */
class GraphicEQ final : public Effect
{
public:
    static constexpr int kBands = 7;
    void setParameter (int i, float v) noexcept override
    {
        if (i < kBands) { gains[i] = v; dirty = true; }
        else out.setTarget (dbToGain (v));
    }
    void reset() noexcept override { for (auto& b : bands) b.reset(); out.snap(); }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        static const double freqs[kBands] { 100, 200, 400, 800, 1600, 3200, 6400 };
        if (dirty) { dirty = false; for (int b = 0; b < kBands; ++b) bands[b].peak (sr, freqs[b], 1.4, gains[b]); }
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            for (auto& b : bands) { l = b.process (l, 0); r = b.process (r, 1); }
            const float o = out.next();
            L[i] = l * o; R[i] = r * o;
        }
    }
protected:
    void prepare (double, int) override { out.prepare (sr, 20); dirty = true; }
private:
    float gains[kBands] {}; bool dirty = true; Biquad bands[kBands]; Smoother out;
};

/** 3-band parametric EQ: low shelf, fully parametric mid, high shelf (Q sets shelf slope). */
class ParametricEQ final : public Effect
{
public:
    enum { LowFreq, LowGain, LowQ, MidFreq, MidGain, MidQ, HighFreq, HighGain, HighQ, Output };
    void setParameter (int i, float v) noexcept override
    {
        if (i == Output) { out.setTarget (dbToGain (v)); return; }
        if (i >= 0 && i < 9) { p[i] = v; dirty = true; }
    }
    void reset() noexcept override { lo.reset(); mid.reset(); hi.reset(); out.snap(); }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        if (dirty)
        {
            dirty = false;
            lo.lowShelf (sr, p[LowFreq], std::max (0.3f, p[LowQ]), p[LowGain]);
            mid.peak (sr, p[MidFreq], p[MidQ], p[MidGain]);
            hi.highShelf (sr, p[HighFreq], std::max (0.3f, p[HighQ]), p[HighGain]);
        }
        for (int i = 0; i < n; ++i)
        {
            const float o = out.next();
            L[i] = hi.process (mid.process (lo.process (L[i], 0), 0), 0) * o;
            R[i] = hi.process (mid.process (lo.process (R[i], 1), 1), 1) * o;
        }
    }
protected:
    void prepare (double, int) override { out.prepare (sr, 20); dirty = true; }
private:
    float p[9] { 120, 0, 0.7f, 800, 0, 1, 4000, 0, 0.7f }; bool dirty = true;
    Biquad lo, mid, hi; Smoother out;
};
} // namespace pf
