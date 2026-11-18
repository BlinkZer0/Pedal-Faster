#pragma once
// Gain pedals. Each pedal has its own topology; waveshapers use first-order ADAA
// (disabled in Eco quality) so drive pedals stay low-aliasing with zero added latency.

#include "../EffectProcessor.h"

namespace pf
{
/** Helper base: tracks a "coefficients dirty" flag and the Eco-quality ADAA switch. */
class GainPedalBase : public Effect
{
protected:
    bool dirty = true;
    int  lastQuality = -1;
    template <typename... Shapers>
    void applyQuality (const ProcessContext& ctx, Shapers&... s) noexcept
    {
        if (ctx.quality != lastQuality)
        {
            lastQuality = ctx.quality;
            ((s.enabled = ctx.quality > 0), ...);
        }
    }
};

//==============================================================================
/** Clean boost: high-headroom JFET-style gain stage with tilt tone control. */
class CleanBoost final : public GainPedalBase
{
public:
    enum { Gain, Tone, Output };
    void setParameter (int i, float v) noexcept override
    {
        if (i == Gain)   gain.setTarget (dbToGain (v));
        if (i == Tone)   { tone = v; dirty = true; }
        if (i == Output) out.setTarget (dbToGain (v));
    }
    void reset() noexcept override { gain.snap(); out.snap(); shaper.setBias (0.08f); shaper.reset(); lowS.reset(); highS.reset(); }
    void process (float* L, float* R, int n, const ProcessContext& ctx) noexcept override
    {
        applyQuality (ctx, shaper);
        if (dirty) { dirty = false; lowS.lowShelf (sr, 300, 0.7, -(tone - 0.5) * 12.0); highS.highShelf (sr, 2200, 0.7, (tone - 0.5) * 12.0); }
        for (int i = 0; i < n; ++i)
        {
            const float g = gain.next(), o = out.next();
            for (int ch = 0; ch < 2; ++ch)
            {
                float* d = ch == 0 ? L : R;
                float y = d[i] * g;
                y = 2.0f * shaper.process (0.5f * y, ch); // clean up to ~ +/-1, then gently rounds
                y = highS.process (lowS.process (y, ch), ch);
                d[i] = y * o;
            }
        }
    }
protected:
    void prepare (double, int) override { gain.prepare (sr, 20); out.prepare (sr, 20); dirty = true; }
private:
    Smoother gain, out; float tone = 0.5f;
    AdaaTanh shaper; Biquad lowS, highS;
};

//==============================================================================
/** Transparent overdrive: clean signal blended with a high-passed, diode-clipped path, Baxandall EQ after. */
class TransparentOverdrive final : public GainPedalBase
{
public:
    enum { Drive, Bass, Treble, Level };
    void setParameter (int i, float v) noexcept override
    {
        if (i == Drive)  { drive.setTarget (v); }
        if (i == Bass)   { bass = v; dirty = true; }
        if (i == Treble) { treble = v; dirty = true; }
        if (i == Level)  level.setTarget (dbToGain (-24.0f + 36.0f * v));
    }
    void reset() noexcept override { drive.snap(); level.snap(); clip.reset(); hp.reset(); lowS.reset(); highS.reset(); }
    void process (float* L, float* R, int n, const ProcessContext& ctx) noexcept override
    {
        applyQuality (ctx, clip);
        if (dirty)
        {
            dirty = false;
            hp.highpass (sr, 140, 0.6);
            lowS.lowShelf (sr, 120, 0.7, (bass - 0.5) * 24.0);
            highS.highShelf (sr, 2600, 0.7, (treble - 0.5) * 24.0);
        }
        for (int i = 0; i < n; ++i)
        {
            const float d = drive.next(), lv = level.next();
            const float g = 1.0f + 90.0f * d * d;
            const float cleanAmt = 1.0f - 0.75f * d;
            const float clipAmt  = 0.35f + 0.4f * std::sqrt (d);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* s = ch == 0 ? L : R;
                const float x = s[i];
                const float c = clip.process (hp.process (x, ch) * g * 0.8f, ch);
                float y = x * cleanAmt + c * clipAmt;
                y = highS.process (lowS.process (y, ch), ch);
                s[i] = y * lv;
            }
        }
    }
protected:
    void prepare (double, int) override { drive.prepare (sr, 25); level.prepare (sr, 25); dirty = true; }
private:
    Smoother drive, level; float bass = 0.5f, treble = 0.5f;
    AdaaHardClip clip; Biquad hp, lowS, highS;
};

//==============================================================================
/** Mid-focused overdrive: clipping in a mid-emphasised, high-passed branch summed with the unclipped signal. */
class MidOverdrive final : public GainPedalBase
{
public:
    enum { Drive, Tone, Mid, Level };
    void setParameter (int i, float v) noexcept override
    {
        if (i == Drive) drive.setTarget (v);
        if (i == Tone)  { tone = v; dirty = true; }
        if (i == Mid)   { mid = v; dirty = true; }
        if (i == Level) level.setTarget (dbToGain (-24.0f + 36.0f * v));
    }
    void reset() noexcept override { drive.snap(); level.snap(); clip.reset(); hp.reset(); midPk.reset(); toneLp.reset(); toneLp2.reset(); }
    void process (float* L, float* R, int n, const ProcessContext& ctx) noexcept override
    {
        applyQuality (ctx, clip);
        if (dirty)
        {
            dirty = false;
            hp.highpass (sr, 720, 0.55);
            midPk.peak (sr, 720, 0.8, mid * 10.0);
            const double f = expMap (tone, 900.0f, 7500.0f);
            toneLp.lowpass (sr, f, 0.6);
            toneLp2.lowpass (sr, std::min (f * 2.0, 18000.0), 0.6);
        }
        for (int i = 0; i < n; ++i)
        {
            const float d = drive.next(), lv = level.next();
            const float g = 2.0f + 140.0f * d * d;
            for (int ch = 0; ch < 2; ++ch)
            {
                float* s = ch == 0 ? L : R;
                const float x = s[i];
                const float branch = hp.process (midPk.process (x, ch), ch);
                float y = 0.5f * x + 0.55f * clip.process (branch * g, ch);
                y = toneLp2.process (toneLp.process (y, ch), ch);
                s[i] = y * lv;
            }
        }
    }
protected:
    void prepare (double, int) override { drive.prepare (sr, 25); level.prepare (sr, 25); clip.setBias (0.05f); dirty = true; }
private:
    Smoother drive, level; float tone = 0.5f, mid = 0.5f;
    AdaaTanh clip; Biquad hp, midPk, toneLp, toneLp2;
};

//==============================================================================
/** Hard-clipping distortion: op-amp gain stage with gain-dependent bandwidth, asymmetric hard clipper, LP filter. */
class HardClipDistortion final : public GainPedalBase
{
public:
    enum { Gain, Tone, Bias, Level };
    void setParameter (int i, float v) noexcept override
    {
        if (i == Gain)  { gainKnob = v; dirty = true; }
        if (i == Tone)  { tone = v; dirty = true; }
        if (i == Bias)  { clip.setBias (v * 0.45f); }
        if (i == Level) level.setTarget (dbToGain (-30.0f + 36.0f * v));
    }
    void reset() noexcept override { gainS.snap(); level.snap(); clip.reset(); pre.reset(); gbw.reset(); toneLp.reset(); dc.reset(); hp.reset(); }
    void process (float* L, float* R, int n, const ProcessContext& ctx) noexcept override
    {
        applyQuality (ctx, clip);
        if (dirty)
        {
            dirty = false;
            const float g = 1.0f + 1500.0f * std::pow (gainKnob, 2.4f);
            gainS.setTarget (g);
            hp.highpass (sr, 50, 0.7);
            pre.highShelf (sr, 1500, 0.6, 3.0 + 6.0 * gainKnob);
            gbw.lowpass (sr, std::clamp (2.2e6 / g, 3000.0, 20000.0), 0.7);
            toneLp.lowpass (sr, expMap (tone, 700.0f, 12000.0f), 0.65);
        }
        for (int i = 0; i < n; ++i)
        {
            const float g = gainS.next(), lv = level.next();
            for (int ch = 0; ch < 2; ++ch)
            {
                float* s = ch == 0 ? L : R;
                float y = gbw.process (pre.process (hp.process (s[i], ch), ch) * g, ch);
                y = clip.process (y * 0.5f, ch);
                y = dc.process (toneLp.process (y, ch), ch);
                s[i] = y * lv;
            }
        }
    }
protected:
    void prepare (double, int) override { gainS.prepare (sr, 25); level.prepare (sr, 25); dc.prepare (sr); dirty = true; }
private:
    Smoother gainS, level; float gainKnob = 0.5f, tone = 0.5f;
    AdaaHardClip clip; Biquad hp, pre, gbw, toneLp; DCBlocker dc;
};

//==============================================================================
/** High-gain distortion: three cascaded asymmetric stages with interstage filtering and a 4-band post EQ. */
class HighGainDistortion final : public GainPedalBase
{
public:
    enum { Gain, Bass, Middle, Treble, Presence, Level };
    void setParameter (int i, float v) noexcept override
    {
        switch (i)
        {
            case Gain: gain.setTarget (v); break;
            case Bass: bass = v; dirty = true; break;
            case Middle: middle = v; dirty = true; break;
            case Treble: treble = v; dirty = true; break;
            case Presence: presence = v; dirty = true; break;
            case Level: level.setTarget (dbToGain (-30.0f + 36.0f * v)); break;
            default: break;
        }
    }
    void reset() noexcept override
    {
        gain.snap(); level.snap();
        s1.reset(); s2.reset(); s3.reset();
        for (auto* b : { &tight, &lowS, &midP, &highS, &presP, &fizz }) b->reset();
        inter1.reset(); inter2.reset(); dc1.reset(); dc2.reset();
    }
    void process (float* L, float* R, int n, const ProcessContext& ctx) noexcept override
    {
        applyQuality (ctx, s1, s2, s3);
        if (dirty)
        {
            dirty = false;
            tight.highpass (sr, 115, 0.75);
            lowS.lowShelf (sr, 110, 0.7, (bass - 0.5) * 24.0);
            midP.peak (sr, 650, 0.8, (middle - 0.5) * 20.0);
            highS.highShelf (sr, 2500, 0.7, (treble - 0.5) * 20.0);
            presP.peak (sr, 4500, 1.0, (presence - 0.5) * 16.0);
            fizz.lowpass (sr, 9000, 0.7);
        }
        for (int i = 0; i < n; ++i)
        {
            const float gk = gain.next(), lv = level.next();
            const float g1 = 2.0f + 35.0f * gk, g2 = 2.0f + 22.0f * gk;
            for (int ch = 0; ch < 2; ++ch)
            {
                float* s = ch == 0 ? L : R;
                float y = tight.process (s[i], ch) * g1;
                y = s1.process (y, ch);
                y = dc1.process (inter1.lowpass (y, ch), ch) * g2;
                y = s2.process (y, ch);
                y = dc2.process (inter2.lowpass (y, ch), ch) * 3.0f;
                y = s3.process (y, ch);
                y = fizz.process (presP.process (highS.process (midP.process (lowS.process (y, ch), ch), ch), ch), ch);
                s[i] = y * lv;
            }
        }
    }
protected:
    void prepare (double, int) override
    {
        gain.prepare (sr, 25); level.prepare (sr, 25);
        s1.setBias (0.18f); s2.setBias (-0.12f); s3.setBias (0.05f);
        inter1.setCutoff (sr, 7500); inter2.setCutoff (sr, 6000);
        dc1.prepare (sr); dc2.prepare (sr); dirty = true;
    }
private:
    Smoother gain, level; float bass = .5f, middle = .5f, treble = .5f, presence = .5f;
    AdaaTanh s1, s2, s3; Biquad tight, lowS, midP, highS, presP, fizz;
    OnePole inter1, inter2; DCBlocker dc1, dc2;
};

//==============================================================================
/** Fuzz: very high gain, bias-controlled asymmetric clipping with a starvation dead-zone (sputter),
    second clipping stage and a mid-scooping LP/HP blend tone stack. */
class Fuzz final : public GainPedalBase
{
public:
    enum { Sustain, Tone, Bias, Level };
    void setParameter (int i, float v) noexcept override
    {
        if (i == Sustain) sustain.setTarget (v);
        if (i == Tone)    tone.setTarget (v);
        if (i == Bias)    { bias = v; s1.setBias ((v - 0.5f) * 0.9f); deadZone = std::max (0.0f, 0.5f - v) * 0.7f; }
        if (i == Level)   level.setTarget (dbToGain (-30.0f + 36.0f * v));
    }
    void reset() noexcept override { sustain.snap(); tone.snap(); level.snap(); s1.reset(); s2.reset(); hp.reset(); lp.reset(); hpT.reset(); dc.reset(); }
    void process (float* L, float* R, int n, const ProcessContext& ctx) noexcept override
    {
        applyQuality (ctx, s1, s2);
        for (int i = 0; i < n; ++i)
        {
            const float su = sustain.next(), t = tone.next(), lv = level.next();
            const float g = 8.0f + 500.0f * su * su;
            for (int ch = 0; ch < 2; ++ch)
            {
                float* s = ch == 0 ? L : R;
                float y = s1.process (hp.highpass (s[i], ch) * g * 0.5f, ch);
                const float a = std::abs (y);
                y = (a <= deadZone) ? 0.0f : (y > 0 ? 1.0f : -1.0f) * (a - deadZone) / (1.0f - deadZone);
                y = s2.process (y * 3.0f, ch);
                const float low = lp.lowpass (y, ch), high = hpT.highpass (y, ch);
                y = dc.process (low * (1.0f - t) * 1.4f + high * t * 1.1f, ch);
                s[i] = y * lv;
            }
        }
    }
protected:
    void prepare (double, int) override
    {
        sustain.prepare (sr, 25); tone.prepare (sr, 25); level.prepare (sr, 25);
        hp.setCutoff (sr, 45); lp.setCutoff (sr, 480); hpT.setCutoff (sr, 1150); dc.prepare (sr);
    }
private:
    Smoother sustain, tone, level; float bias = 0.5f, deadZone = 0.0f;
    AdaaTanh s1, s2; OnePole hp, lp, hpT; DCBlocker dc;
};

//==============================================================================
/** Octave fuzz: soft preamp -> ADAA full-wave rectifier (octave up) blended with the straight signal -> fuzz. */
class OctaveFuzz final : public GainPedalBase
{
public:
    enum { Drive, Octave, Tone, Level };
    void setParameter (int i, float v) noexcept override
    {
        if (i == Drive)  drive.setTarget (v);
        if (i == Octave) octave.setTarget (v);
        if (i == Tone)   { tone = v; dirty = true; }
        if (i == Level)  level.setTarget (dbToGain (-30.0f + 36.0f * v));
    }
    void reset() noexcept override
    {
        drive.snap(); octave.snap(); level.snap(); pre.reset(); post.reset(); dc.reset(); dc2.reset(); toneLp.reset(); hp.reset();
        for (int c = 0; c < 2; ++c) { ax1[c] = 0; aF1[c] = 0; }
    }
    void process (float* L, float* R, int n, const ProcessContext& ctx) noexcept override
    {
        applyQuality (ctx, pre, post);
        if (dirty) { dirty = false; toneLp.lowpass (sr, expMap (tone, 900.0f, 10000.0f), 0.7); hp.highpass (sr, 80, 0.7); }
        const bool adaa = ctx.quality > 0;
        for (int i = 0; i < n; ++i)
        {
            const float dr = drive.next(), oc = octave.next(), lv = level.next();
            const float g = 2.0f + 45.0f * dr * dr;
            for (int ch = 0; ch < 2; ++ch)
            {
                float* s = ch == 0 ? L : R;
                const float v = pre.process (hp.process (s[i], ch) * g, ch);
                // ADAA |x|: antiderivative is x|x|/2
                float r;
                const double F = 0.5 * (double) v * std::abs ((double) v);
                const double dx = (double) v - ax1[ch];
                if (! adaa || std::abs (dx) < 1.0e-6) r = std::abs (v);
                else r = (float) ((F - aF1[ch]) / dx);
                ax1[ch] = v; aF1[ch] = F;
                const float rect = dc.process (r * 2.0f, ch);
                float y = v * (1.0f - oc) + rect * oc;
                y = post.process (y * (1.5f + 3.0f * dr), ch);
                s[i] = dc2.process (toneLp.process (y, ch), ch) * lv;
            }
        }
    }
protected:
    void prepare (double, int) override
    {
        drive.prepare (sr, 25); octave.prepare (sr, 25); level.prepare (sr, 25);
        dc.prepare (sr); dc2.prepare (sr); pre.setBias (0.0f); post.setBias (0.1f); dirty = true;
    }
private:
    Smoother drive, octave, level; float tone = 0.5f;
    AdaaTanh pre, post; DCBlocker dc, dc2; Biquad toneLp, hp;
    double ax1[2] {}, aF1[2] {};
};

//==============================================================================
/** Bitcrusher: fractional bit-depth quantiser + sample-and-hold rate reduction + post low-pass. */
class Bitcrusher final : public Effect
{
public:
    enum { Bits, Downsample, Tone, Output };
    void setParameter (int i, float v) noexcept override
    {
        if (i == Bits)       bits = v;
        if (i == Downsample) downsample = std::max (1.0f, v);
        if (i == Tone)       { tone = v; dirty = true; }
        if (i == Output)     out.setTarget (dbToGain (v));
    }
    void reset() noexcept override { out.snap(); phase = 1.0f; held[0] = held[1] = 0.0f; lp.reset(); }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        if (dirty) { dirty = false; lp.lowpass (sr, expMap (tone, 1200.0f, 20000.0f), 0.7); }
        const float q = std::pow (2.0f, bits - 1.0f);
        const float inc = 1.0f / downsample;
        for (int i = 0; i < n; ++i)
        {
            phase += inc;
            if (phase >= 1.0f)
            {
                phase -= 1.0f;
                held[0] = std::round (clampf (L[i], -1.0f, 1.0f) * q) / q;
                held[1] = std::round (clampf (R[i], -1.0f, 1.0f) * q) / q;
            }
            const float o = out.next();
            L[i] = lp.process (held[0], 0) * o;
            R[i] = lp.process (held[1], 1) * o;
        }
    }
protected:
    void prepare (double, int) override { out.prepare (sr, 20); dirty = true; }
private:
    float bits = 8, downsample = 4, tone = 1, phase = 1, held[2] {};
    bool dirty = true; Smoother out; Biquad lp;
};
} // namespace pf
