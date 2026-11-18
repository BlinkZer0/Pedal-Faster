#pragma once
// Delay pedals: slapback (single short tap), digital (clean stereo / ping-pong, filtered feedback),
// tape (wow/flutter modulated read head, saturating feedback loop).

#include "../EffectProcessor.h"

namespace pf
{
inline double syncedDelaySeconds (int sync, double freeSeconds, const ProcessContext& ctx) noexcept
{
    if (sync <= 0) return freeSeconds;
    return syncBeats (sync) * 60.0 / std::clamp (ctx.bpm, 20.0, 400.0);
}

//==============================================================================
class SlapbackDelay final : public Effect
{
public:
    enum { Time, Feedback, Tone };
    void setParameter (int i, float v) noexcept override
    {
        if (i == Time)     timeMs.setTarget (v);
        if (i == Feedback) feedback = clampf (v, 0.0f, 0.7f);
        if (i == Tone)     { tone = v; dirty = true; }
    }
    void reset() noexcept override { line[0].reset(); line[1].reset(); timeMs.snap(); lp.reset(); hp.reset(); }
    bool hasTail() const noexcept override { return true; }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        if (dirty) { dirty = false; lp.lowpass (sr, expMap (tone, 1500.0f, 12000.0f), 0.7); hp.highpass (sr, 120, 0.7); }
        for (int i = 0; i < n; ++i)
        {
            const float d = timeMs.next() * 0.001f * (float) sr;
            for (int ch = 0; ch < 2; ++ch)
            {
                float* s = ch == 0 ? L : R;
                const float y = hp.process (lp.process (line[ch].readLinear (d), ch), ch);
                line[ch].push (undenormal (s[i] + y * feedback));
                s[i] = y;
            }
        }
    }
protected:
    void prepare (double, int) override
    {
        const int maxS = (int) (0.3 * sr) + 8;
        line[0].setMaxDelay (maxS); line[1].setMaxDelay (maxS); timeMs.prepare (sr, 120); dirty = true;
    }
private:
    Smoother timeMs; float feedback = 0.1f, tone = 0.6f; bool dirty = true;
    DelayLine line[2]; Biquad lp, hp;
};

//==============================================================================
class DigitalDelay final : public Effect
{
public:
    enum { Time, Sync, Feedback, Mode, Spread, LowCut, HighCut };
    void setParameter (int i, float v) noexcept override
    {
        switch (i)
        {
            case Time: timeMs = v; break;
            case Sync: sync = (int) v; break;
            case Feedback: feedback = clampf (v, 0.0f, 0.95f); break;
            case Mode: mode = (int) v; break;
            case Spread: spread = v; break;
            case LowCut: lowCut = v; dirty = true; break;
            case HighCut: highCut = v; dirty = true; break;
            default: break;
        }
    }
    void reset() noexcept override { line[0].reset(); line[1].reset(); hp.reset(); lp.reset(); first = true; }
    bool hasTail() const noexcept override { return true; }
    void process (float* L, float* R, int n, const ProcessContext& ctx) noexcept override
    {
        if (dirty) { dirty = false; hp.highpass (sr, lowCut, 0.7); lp.lowpass (sr, highCut, 0.7); }
        const float target = (float) (syncedDelaySeconds (sync, timeMs * 0.001, ctx) * sr);
        delayS.setTarget (std::min (target, (float) line[0].capacity() * 0.8f));
        if (first) { delayS.snap(); first = false; }
        for (int i = 0; i < n; ++i)
        {
            const float d = delayS.next();
            const float dR = mode == 1 ? d * (1.0f + 0.25f * spread) : d;
            const float yl = line[0].readLinear (d), yr = line[1].readLinear (dR);
            const float fl = lp.process (hp.process (yl, 0), 0), fr = lp.process (hp.process (yr, 1), 1);
            if (mode == 2) // ping-pong: mono in -> left, left -> right, right -> left
            {
                const float mono = 0.5f * (L[i] + R[i]);
                line[0].push (undenormal (mono + fr * feedback));
                line[1].push (undenormal (fl * feedback));
            }
            else if (mode == 0)
            {
                const float mono = 0.5f * (L[i] + R[i]);
                line[0].push (undenormal (mono + fl * feedback));
                line[1].push (undenormal (mono + fl * feedback));
            }
            else
            {
                line[0].push (undenormal (L[i] + fl * feedback));
                line[1].push (undenormal (R[i] + fr * feedback));
            }
            L[i] = fl;
            R[i] = mode == 0 ? fl : fr;
        }
    }
protected:
    void prepare (double, int) override
    {
        const int maxS = (int) (2.6 * sr) + 8;
        line[0].setMaxDelay (maxS); line[1].setMaxDelay (maxS); delayS.prepare (sr, 160); dirty = true; first = true;
    }
private:
    float timeMs = 400, feedback = 0.35f, spread = 0.5f, lowCut = 80, highCut = 9000;
    int sync = 0, mode = 1; bool dirty = true, first = true;
    Smoother delayS; DelayLine line[2]; Biquad hp, lp;
};

//==============================================================================
class TapeDelay final : public Effect
{
public:
    enum { Time, Sync, Feedback, Saturation, Wow, Flutter, Tone };
    void setParameter (int i, float v) noexcept override
    {
        switch (i)
        {
            case Time: timeMs = v; break;
            case Sync: sync = (int) v; break;
            case Feedback: feedback = clampf (v, 0.0f, 1.05f); break;
            case Saturation: saturation = v; break;
            case Wow: wow = v; break;
            case Flutter: flutter = v; break;
            case Tone: tone = v; dirty = true; break;
            default: break;
        }
    }
    void reset() noexcept override { for (int c = 0; c < 2; ++c) { line[c].reset(); fbState[c] = 0; } lp.reset(); hp.reset(); first = true; }
    bool hasTail() const noexcept override { return true; }
    void process (float* L, float* R, int n, const ProcessContext& ctx) noexcept override
    {
        if (dirty) { dirty = false; lp.lowpass (sr, expMap (tone, 1500.0f, 9000.0f), 0.6); hp.highpass (sr, 90, 0.6); }
        const float target = (float) (syncedDelaySeconds (sync, timeMs * 0.001, ctx) * sr);
        delayS.setTarget (std::min (target, (float) line[0].capacity() * 0.8f));
        if (first) { delayS.snap(); first = false; }
        const float drive = 1.0f + saturation * 4.0f;
        for (int i = 0; i < n; ++i)
        {
            wowLfo.advance (0.55 / sr); flutLfo.advance (6.3 / sr);
            drift = 0.9995f * drift + 0.0005f * rng.bipolar();
            const float d = delayS.next();
            for (int ch = 0; ch < 2; ++ch)
            {
                float* s = ch == 0 ? L : R;
                const float mod = 1.0f + wow * 0.006f * wowLfo.value (LfoShape::Sine, ch * 0.13)
                                       + flutter * 0.0012f * flutLfo.value (LfoShape::Sine, ch * 0.29)
                                       + wow * 0.02f * drift;
                const float y = line[ch].readHermite (d * mod);
                float loop = s[i] + fbState[ch] * feedback;
                loop = fastTanh (loop * drive) / drive * (1.0f + 0.15f * saturation);
                line[ch].push (undenormal (loop));
                const float filtered = hp.process (lp.process (y, ch), ch);
                fbState[ch] = filtered;
                s[i] = filtered;
            }
        }
    }
protected:
    void prepare (double, int) override
    {
        const int maxS = (int) (2.2 * sr) + 8;
        line[0].setMaxDelay (maxS); line[1].setMaxDelay (maxS); delayS.prepare (sr, 300); dirty = true; first = true;
    }
private:
    float timeMs = 350, feedback = 0.4f, saturation = 0.4f, wow = 0.3f, flutter = 0.3f, tone = 0.5f, fbState[2] {}, drift = 0;
    int sync = 0; bool dirty = true, first = true;
    Smoother delayS; DelayLine line[2]; Biquad lp, hp; Lfo wowLfo, flutLfo; Random rng;
};
} // namespace pf
