#pragma once
// Modulation effects. Each uses its own structure: allpass cascade (phaser), short feedback delay
// (flanger), multi-voice modulated delays (chorus), pure pitch modulation (vibrato),
// amplitude modulation (tremolo) and a two-rotor Doppler/AM simulation (rotary).

#include "../EffectProcessor.h"

namespace pf
{
//==============================================================================
class Phaser final : public Effect
{
public:
    enum { Rate, Depth, Feedback, Stages, Centre, Stereo };
    void setParameter (int i, float v) noexcept override
    {
        switch (i)
        {
            case Rate: rate = v; break;
            case Depth: depth = v; break;
            case Feedback: feedback = clampf (v, -0.95f, 0.95f); break;
            case Stages: stages = 2 + 2 * std::clamp ((int) v, 0, 5); break;
            case Centre: centre = v; break;
            case Stereo: stereo = v; break;
            default: break;
        }
    }
    void reset() noexcept override { for (auto& a : ap) a.reset(); fb[0] = fb[1] = 0; }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        const double inc = rate / sr;
        for (int i = 0; i < n; i += 8)
        {
            const int m = std::min (8, n - i);
            lfo.advance (inc * m);
            float coeff[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                const float l = lfo.value (LfoShape::Triangle, ch == 0 ? 0.0 : stereo * 0.25);
                const float f = expMap (clampf (centre + depth * 0.5f * l, 0.0f, 1.0f), 120.0f, 6000.0f);
                const float t = std::tan (kPi * f / (float) sr);
                coeff[ch] = (t - 1.0f) / (t + 1.0f);
            }
            for (int k = i; k < i + m; ++k)
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* s = ch == 0 ? L : R;
                    float y = s[k] + fb[ch] * feedback;
                    for (int st = 0; st < stages; ++st) y = ap[st].process (y, coeff[ch], ch);
                    fb[ch] = undenormal (clampf (y, -4.0f, 4.0f));
                    s[k] = y;
                }
        }
    }
protected:
    void prepare (double, int) override {}
private:
    float rate = 0.5f, depth = 0.7f, feedback = 0.3f, centre = 0.5f, stereo = 0.5f, fb[2] {};
    int stages = 4; FirstOrderAllpass ap[12]; Lfo lfo;
};

//==============================================================================
class Flanger final : public Effect
{
public:
    enum { Rate, Depth, Feedback, Delay, Stereo };
    void setParameter (int i, float v) noexcept override
    {
        switch (i)
        {
            case Rate: rate = v; break;
            case Depth: depth = v; break;
            case Feedback: feedback = clampf (v, -0.95f, 0.95f); break;
            case Delay: delayMs.setTarget (v); break;
            case Stereo: stereo = v; break;
            default: break;
        }
    }
    void reset() noexcept override { line[0].reset(); line[1].reset(); delayMs.snap(); last[0] = last[1] = 0; }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        const double inc = rate / sr;
        for (int i = 0; i < n; ++i)
        {
            lfo.advance (inc);
            const float base = delayMs.next() * 0.001f * (float) sr;
            for (int ch = 0; ch < 2; ++ch)
            {
                float* s = ch == 0 ? L : R;
                const float l = lfo.value (LfoShape::Triangle, ch == 0 ? 0.0 : stereo * 0.5);
                const float d = std::max (2.0f, base * (1.0f + depth * 0.95f * l));
                const float y = line[ch].readHermite (d);
                line[ch].push (undenormal (s[i] + feedback * y));
                s[i] = y;
            }
        }
    }
protected:
    void prepare (double, int) override
    {
        const int maxS = (int) (0.025 * sr) + 8;
        line[0].setMaxDelay (maxS); line[1].setMaxDelay (maxS); delayMs.prepare (sr, 50);
    }
private:
    float rate = 0.3f, depth = 0.7f, feedback = 0.5f, stereo = 0.5f, last[2] {};
    Smoother delayMs; DelayLine line[2]; Lfo lfo;
};

//==============================================================================
class Chorus final : public Effect
{
public:
    enum { Rate, Depth, Voices, Width, Delay, Tone };
    void setParameter (int i, float v) noexcept override
    {
        switch (i)
        {
            case Rate: rate = v; break;
            case Depth: depth.setTarget (v); break;
            case Voices: voices = 1 + std::clamp ((int) v, 0, 3); break;
            case Width: width = v; break;
            case Delay: delayMs.setTarget (v); break;
            case Tone: tone = v; dirty = true; break;
            default: break;
        }
    }
    void reset() noexcept override { line[0].reset(); line[1].reset(); depth.snap(); delayMs.snap(); lp.reset(); }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        static const double rateMul[4] { 1.0, 1.13, 0.87, 1.31 };
        if (dirty) { dirty = false; lp.lowpass (sr, expMap (tone, 2500.0f, 18000.0f), 0.7); }
        for (int i = 0; i < n; ++i)
        {
            const float base = delayMs.next() * 0.001f * (float) sr;
            const float modDepth = depth.next() * base * 0.45f;
            line[0].push (L[i]); line[1].push (R[i]);
            float outL = 0, outR = 0;
            for (int v = 0; v < voices; ++v)
            {
                lfos[v].advance (rate * rateMul[v] / sr);
                const float m = lfos[v].value (LfoShape::Sine, (double) v / voices);
                const float d = base + modDepth * m;
                // alternate voices left/right; width 0 = mono sum
                const float pan = (v % 2 == 0 ? -1.0f : 1.0f) * width;
                const float yl = line[0].readHermite (d), yr = line[1].readHermite (d);
                const float y = 0.5f * (yl + yr);
                outL += lerpf (y, yl, 0.5f) * (1.0f - std::max (0.0f, pan));
                outR += lerpf (y, yr, 0.5f) * (1.0f + std::min (0.0f, pan));
            }
            const float norm = 1.0f / std::sqrt ((float) voices);
            L[i] = lp.process (outL * norm, 0);
            R[i] = lp.process (outR * norm, 1);
        }
    }
protected:
    void prepare (double, int) override
    {
        const int maxS = (int) (0.06 * sr) + 8;
        line[0].setMaxDelay (maxS); line[1].setMaxDelay (maxS);
        depth.prepare (sr, 40); delayMs.prepare (sr, 60); dirty = true;
        for (int v = 0; v < 4; ++v) lfos[v].phase = 0.25 * v;
    }
private:
    float rate = 0.8f, width = 0.7f, tone = 0.8f; int voices = 2; bool dirty = true;
    Smoother depth, delayMs; DelayLine line[2]; Lfo lfos[4]; Biquad lp;
};

//==============================================================================
class Vibrato final : public Effect
{
public:
    enum { Rate, Depth, Shape };
    void setParameter (int i, float v) noexcept override
    {
        if (i == Rate)  rate = v;
        if (i == Depth) depth.setTarget (v);
        if (i == Shape) shape = (int) v;
    }
    void reset() noexcept override { line[0].reset(); line[1].reset(); depth.snap(); }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        const LfoShape sh = shape == 0 ? LfoShape::Sine : (shape == 1 ? LfoShape::Triangle : LfoShape::Square);
        const float centre = 0.006f * (float) sr;
        for (int i = 0; i < n; ++i)
        {
            lfo.advance (rate / sr);
            // depth scaled so pitch deviation stays roughly constant across rates
            const float dev = depth.next() * std::min (0.005f, 0.0035f * 5.0f / std::max (0.5f, rate)) * (float) sr;
            const float d = centre + dev * lfo.value (sh);
            line[0].push (L[i]); line[1].push (R[i]);
            L[i] = line[0].readHermite (d);
            R[i] = line[1].readHermite (d);
        }
    }
protected:
    void prepare (double, int) override
    {
        const int maxS = (int) (0.02 * sr) + 8;
        line[0].setMaxDelay (maxS); line[1].setMaxDelay (maxS); depth.prepare (sr, 40);
    }
private:
    float rate = 5; int shape = 0; Smoother depth; DelayLine line[2]; Lfo lfo;
};

//==============================================================================
class Tremolo final : public Effect
{
public:
    enum { Rate, Sync, Depth, Wave, Stereo };
    void setParameter (int i, float v) noexcept override
    {
        switch (i)
        {
            case Rate: rate = v; break;
            case Sync: sync = (int) v; break;
            case Depth: depth.setTarget (v); break;
            case Wave: wave = std::clamp ((int) v, 0, 4); break;
            case Stereo: stereo = v; break;
            default: break;
        }
    }
    void reset() noexcept override { depth.snap(); }
    void process (float* L, float* R, int n, const ProcessContext& ctx) noexcept override
    {
        double hz = rate;
        if (sync > 0)
        {
            const double beats = syncBeats (sync);
            hz = ctx.bpm / 60.0 / beats;
            if (ctx.playing) lfo.phase = std::fmod (ctx.ppqPosition / beats, 1.0); // lock to bar grid
        }
        const double inc = hz / sr;
        const auto sh = (LfoShape) wave;
        for (int i = 0; i < n; ++i)
        {
            lfo.advance (inc);
            const float d = depth.next();
            const float gl = 1.0f - d * (0.5f + 0.5f * lfo.value (sh));
            const float gr = 1.0f - d * (0.5f + 0.5f * lfo.value (sh, stereo * 0.5));
            L[i] *= gl; R[i] *= gr;
        }
    }
protected:
    void prepare (double, int) override { depth.prepare (sr, 20); }
private:
    float rate = 5, stereo = 0; int sync = 0, wave = 0; Smoother depth; Lfo lfo;
};

//==============================================================================
/** Rotary speaker: crossover into horn and drum rotors, each with inertia, Doppler (modulated delay)
    and amplitude modulation, picked up by two virtual microphones. */
class RotarySpeaker final : public Effect
{
public:
    enum { Speed, Acceleration, Width, Drive, Balance };
    void setParameter (int i, float v) noexcept override
    {
        switch (i)
        {
            case Speed: speed = (int) v; break;
            case Acceleration: accel = v; break;
            case Width: width = v; break;
            case Drive: drive = v; break;
            case Balance: balance = v; break;
            default: break;
        }
    }
    void reset() noexcept override
    {
        for (auto* b : { &lp1, &lp2, &hp1, &hp2 }) b->reset();
        for (auto& d : hornLine) d.reset();
        for (auto& d : drumLine) d.reset();
        hornHz = targetHorn(); drumHz = targetDrum();
    }
    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        const float hornTau = 0.25f + accel * 1.8f, drumTau = 0.9f + accel * 4.5f;
        const float hc = (float) std::exp (-1.0 / (hornTau * sr)), dc = (float) std::exp (-1.0 / (drumTau * sr));
        const float th = targetHorn(), td = targetDrum();
        const float dg = 1.0f + drive * 8.0f;
        const float hornDepth = 0.00055f * (float) sr, drumDepth = 0.00025f * (float) sr;
        const float base = 0.0015f * (float) sr;
        const float hornGain = std::sqrt (balance) * 1.2f, drumGain = std::sqrt (1.0f - balance) * 1.2f;
        for (int i = 0; i < n; ++i)
        {
            hornHz = th + hc * (hornHz - th);
            drumHz = td + dc * (drumHz - td);
            horn.advance (hornHz / sr); drum.advance (drumHz / sr);

            float x = 0.5f * (L[i] + R[i]);
            if (drive > 0.001f) x = fastTanh (x * dg) / std::sqrt (dg);
            const float low = lp2.process (lp1.process (x, 0), 0);
            const float high = hp2.process (hp1.process (x, 0), 0);
            hornLine[0].push (high); drumLine[0].push (low);

            float out[2];
            for (int mic = 0; mic < 2; ++mic)
            {
                const double off = mic == 0 ? 0.0 : 0.5 * width + 0.0;
                const float hs = Lfo::shape (horn.phase + off, LfoShape::Sine);
                const float hcos = Lfo::shape (horn.phase + off + 0.25, LfoShape::Sine);
                const float ds = Lfo::shape (drum.phase + off, LfoShape::Sine);
                const float h = hornLine[0].readLinear (base + hornDepth * (1.0f + hs)) * (1.0f - 0.35f * (0.5f + 0.5f * hcos));
                const float d = drumLine[0].readLinear (base + drumDepth * (1.0f + ds)) * (1.0f - 0.22f * (0.5f + 0.5f * ds));
                out[mic] = h * hornGain + d * drumGain;
            }
            const float mid = 0.5f * (out[0] + out[1]);
            L[i] = lerpf (mid, out[0], width);
            R[i] = lerpf (mid, out[1], width);
        }
    }
protected:
    void prepare (double, int) override
    {
        const int maxS = (int) (0.01 * sr) + 8;
        for (auto& d : hornLine) d.setMaxDelay (maxS);
        for (auto& d : drumLine) d.setMaxDelay (maxS);
        lp1.lowpass (sr, 800, 0.7071); lp2.lowpass (sr, 800, 0.7071);
        hp1.highpass (sr, 800, 0.7071); hp2.highpass (sr, 800, 0.7071);
        drum.phase = 0.3;
    }
private:
    float targetHorn() const noexcept { return speed == 0 ? 0.8f : (speed == 1 ? 6.7f : 0.0f); }
    float targetDrum() const noexcept { return speed == 0 ? 0.67f : (speed == 1 ? 5.9f : 0.0f); }
    int speed = 0; float accel = 0.4f, width = 0.8f, drive = 0.1f, balance = 0.55f;
    float hornHz = 0.8f, drumHz = 0.67f;
    Biquad lp1, lp2, hp1, hp2; DelayLine hornLine[1], drumLine[1]; Lfo horn, drum;
};
} // namespace pf
