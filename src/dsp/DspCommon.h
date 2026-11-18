#pragma once
// Shared, JUCE-free DSP building blocks used by every effect, amp and cabinet.
// Everything here is allocation-free after prepare()/setMaxDelay() and safe on the audio thread.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace pf
{
constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float dbToGain (float db) noexcept      { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept       { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float clampf (float x, float lo, float hi) noexcept { return std::min (std::max (x, lo), hi); }
inline float lerpf (float a, float b, float t) noexcept    { return a + (b - a) * t; }
inline float undenormal (float x) noexcept     { return (std::abs (x) < 1.0e-20f) ? 0.0f : x; }
inline bool  isSane (float x) noexcept         { return std::isfinite (x) && std::abs (x) < 1.0e6f; }

/** Exponential map: t in [0,1] -> [lo,hi] logarithmically. */
inline float expMap (float t, float lo, float hi) noexcept { return lo * std::pow (hi / lo, clampf (t, 0.0f, 1.0f)); }

//==============================================================================
/** Fast rational tanh approximation (accurate to ~1e-4 for |x|<4, saturates beyond). */
inline float fastTanh (float x) noexcept
{
    if (x > 4.97f)  return 1.0f;
    if (x < -4.97f) return -1.0f;
    const float x2 = x * x;
    const float a  = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float b  = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return clampf (a / b, -1.0f, 1.0f);
}

/** log(cosh(x)), numerically safe: the antiderivative of tanh. */
inline double logCosh (double x) noexcept
{
    const double ax = std::abs (x);
    return ax + std::log1p (std::exp (-2.0 * ax)) - 0.69314718055994530942;
}

//==============================================================================
/** One-pole parameter smoother. */
struct Smoother
{
    float current = 0.0f, target = 0.0f, coeff = 1.0f;

    void prepare (double sampleRate, float timeMs) noexcept
    {
        const double n = std::max (1.0, timeMs * 0.001 * sampleRate);
        coeff = (float) (1.0 - std::exp (-1.0 / n));
    }
    void setTarget (float t) noexcept { target = t; }
    void snap() noexcept              { current = target; }
    void snapTo (float v) noexcept    { current = target = v; }
    float next() noexcept             { current += (target - current) * coeff; return current; }
    bool smoothing() const noexcept   { return std::abs (target - current) > 1.0e-6f * (1.0f + std::abs (target)); }
};

//==============================================================================
/** RBJ biquad (transposed direct form II), two channels of state. */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float s1[2] {}, s2[2] {};

    void reset() noexcept { s1[0] = s1[1] = s2[0] = s2[1] = 0.0f; }

    inline float process (float x, int ch) noexcept
    {
        const float y = b0 * x + s1[ch];
        s1[ch] = undenormal (b1 * x - a1 * y + s2[ch]);
        s2[ch] = undenormal (b2 * x - a2 * y);
        return y;
    }

    void setNormalised (double B0, double B1, double B2, double A0, double A1, double A2) noexcept
    {
        const double inv = 1.0 / A0;
        b0 = (float) (B0 * inv); b1 = (float) (B1 * inv); b2 = (float) (B2 * inv);
        a1 = (float) (A1 * inv); a2 = (float) (A2 * inv);
    }

    static double w0 (double f, double sr) { return 2.0 * 3.14159265358979323846 * std::clamp (f, 5.0, sr * 0.49) / sr; }

    void lowpass (double sr, double f, double q)
    {
        const double w = w0 (f, sr), c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNormalised ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass (double sr, double f, double q)
    {
        const double w = w0 (f, sr), c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNormalised ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void bandpass (double sr, double f, double q) // 0 dB peak
    {
        const double w = w0 (f, sr), c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNormalised (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void notch (double sr, double f, double q)
    {
        const double w = w0 (f, sr), c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNormalised (1, -2 * c, 1, 1 + al, -2 * c, 1 - al);
    }
    void peak (double sr, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = w0 (f, sr), c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNormalised (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (double sr, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = w0 (f, sr), c = std::cos (w), s = std::sin (w);
        const double al = s / (2.0 * q), sq = 2.0 * std::sqrt (A) * al;
        setNormalised (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                       (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void highShelf (double sr, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = w0 (f, sr), c = std::cos (w), s = std::sin (w);
        const double al = s / (2.0 * q), sq = 2.0 * std::sqrt (A) * al;
        setNormalised (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                       (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
    void bypass() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
};

//==============================================================================
/** Topology-preserving-transform state variable filter (Simper/Zavalishin), stable under fast modulation. */
struct SVF
{
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    float ic1[2] {}, ic2[2] {};
    float lp = 0, bp = 0, hp = 0;

    void reset() noexcept { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }

    void set (float sampleRate, float freq, float q) noexcept
    {
        freq = clampf (freq, 10.0f, sampleRate * 0.45f);
        g  = std::tan (kPi * freq / sampleRate);
        k  = 1.0f / std::max (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline void tick (float x, int ch) noexcept
    {
        const float v3 = x - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = undenormal (2.0f * v1 - ic1[ch]);
        ic2[ch] = undenormal (2.0f * v2 - ic2[ch]);
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

//==============================================================================
/** One-pole low/high-pass. */
struct OnePole
{
    float a = 0.0f, z[2] {};
    void reset() noexcept { z[0] = z[1] = 0.0f; }
    void setCutoff (double sr, double f) noexcept { a = (float) std::exp (-2.0 * 3.14159265358979323846 * std::clamp (f, 1.0, sr * 0.49) / sr); }
    inline float lowpass (float x, int ch) noexcept  { z[ch] = undenormal (x + a * (z[ch] - x)); return z[ch]; }
    inline float highpass (float x, int ch) noexcept { return x - lowpass (x, ch); }
};

/** DC blocker (leaky differentiator). */
struct DCBlocker
{
    float R = 0.995f, x1[2] {}, y1[2] {};
    void prepare (double sr) noexcept { R = (float) std::exp (-2.0 * 3.14159265358979323846 * 12.0 / sr); }
    void reset() noexcept { x1[0] = x1[1] = y1[0] = y1[1] = 0.0f; }
    inline float process (float x, int ch) noexcept
    {
        const float y = x - x1[ch] + R * y1[ch];
        x1[ch] = x; y1[ch] = undenormal (y);
        return y;
    }
};

//==============================================================================
/** Power-of-two circular delay line with fractional reads. Single channel. */
class DelayLine
{
public:
    void setMaxDelay (int samples)
    {
        int size = 1;
        while (size < samples + 4) size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        write = 0;
    }
    void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); write = 0; }
    int capacity() const noexcept { return mask - 3; }

    inline void push (float x) noexcept { buffer[(size_t) write] = x; write = (write + 1) & mask; }

    /** Integer read, delay >= 1 (delay 1 == the most recently pushed sample). */
    inline float readInt (int delay) const noexcept { return buffer[(size_t) ((write - delay) & mask)]; }

    /** Linear-interpolated read. */
    inline float readLinear (float delay) const noexcept
    {
        delay = clampf (delay, 1.0f, (float) capacity());
        const int   i = (int) delay;
        const float f = delay - (float) i;
        const float a = readInt (i), b = readInt (i + 1);
        return a + (b - a) * f;
    }

    /** 4-point Hermite read, smoother for modulated delays. */
    inline float readHermite (float delay) const noexcept
    {
        delay = clampf (delay, 2.0f, (float) capacity() - 2.0f);
        const int   i = (int) delay;
        const float f = delay - (float) i;
        const float xm1 = readInt (i - 1), x0 = readInt (i), x1 = readInt (i + 1), x2 = readInt (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

private:
    std::vector<float> buffer { std::vector<float> (8, 0.0f) };
    int mask = 7, write = 0;
};

//==============================================================================
/** Schroeder allpass built on a DelayLine (used for reverb diffusion). */
struct DiffuserAllpass
{
    DelayLine line;
    float delay = 100.0f, gain = 0.6f;
    void setMax (int samples) { line.setMaxDelay (samples); }
    void reset() noexcept { line.reset(); }
    inline float process (float x, float modOffset = 0.0f) noexcept
    {
        const float d = (modOffset == 0.0f) ? line.readInt ((int) delay) : line.readLinear (delay + modOffset);
        const float v = x + gain * d;
        line.push (undenormal (v));
        return d - gain * v;
    }
};

/** First-order allpass (phaser stage). */
struct FirstOrderAllpass
{
    float z[2] {};
    void reset() noexcept { z[0] = z[1] = 0.0f; }
    inline float process (float x, float a, int ch) noexcept
    {
        const float y = a * x + z[ch];
        z[ch] = undenormal (x - a * y);
        return y;
    }
};

//==============================================================================
enum class LfoShape { Sine = 0, Triangle, Square, SawDown, SawUp };

struct Lfo
{
    double phase = 0.0; // [0,1)
    inline void advance (double inc) noexcept { phase += inc; if (phase >= 1.0) phase -= std::floor (phase); }

    static inline float shape (double p, LfoShape s) noexcept
    {
        p -= std::floor (p);
        switch (s)
        {
            case LfoShape::Sine:     return std::sin ((float) (p * 6.283185307179586));
            case LfoShape::Triangle: return (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
            case LfoShape::Square:   { // softened square to avoid clicks
                const float s1 = std::sin ((float) (p * 6.283185307179586));
                return fastTanh (s1 * 6.0f); }
            case LfoShape::SawDown:  return (float) (1.0 - 2.0 * p);
            case LfoShape::SawUp:    return (float) (2.0 * p - 1.0);
        }
        return 0.0f;
    }
    inline float value (LfoShape s, double offset = 0.0) const noexcept { return shape (phase + offset, s); }
};

//==============================================================================
/** Attack/release envelope follower on |x|. */
struct EnvelopeFollower
{
    float att = 0.0f, rel = 0.0f, env = 0.0f;
    void set (double sr, float attackMs, float releaseMs) noexcept
    {
        att = (float) std::exp (-1.0 / (std::max (0.01f, attackMs) * 0.001 * sr));
        rel = (float) std::exp (-1.0 / (std::max (0.01f, releaseMs) * 0.001 * sr));
    }
    void reset() noexcept { env = 0.0f; }
    inline float process (float x) noexcept
    {
        const float a = std::abs (x);
        const float c = a > env ? att : rel;
        env = undenormal (a + c * (env - a));
        return env;
    }
};

//==============================================================================
/** First-order antiderivative anti-aliasing (ADAA) waveshapers. Zero latency (half-sample group delay). */
struct AdaaTanh
{
    double x1[2] {}, F1[2] {};
    float bias = 0.0f, biasOut = 0.0f;
    bool enabled = true;

    void setBias (float b) noexcept { bias = b; biasOut = std::tanh (b); }
    void reset() noexcept { x1[0] = x1[1] = bias; F1[0] = F1[1] = logCosh (bias); }

    inline float process (float xIn, int ch) noexcept
    {
        const double x = (double) xIn + bias;
        if (! enabled) { x1[ch] = x; return (float) std::tanh (x) - biasOut; }
        const double F = logCosh (x);
        const double dx = x - x1[ch];
        double y;
        if (std::abs (dx) < 1.0e-5) y = std::tanh (0.5 * (x + x1[ch]));
        else                        y = (F - F1[ch]) / dx;
        x1[ch] = x; F1[ch] = F;
        return (float) y - biasOut;
    }
};

struct AdaaHardClip
{
    double x1[2] {}, F1[2] {};
    float bias = 0.0f, biasOut = 0.0f;
    bool enabled = true;

    static double clip (double u) noexcept { return std::clamp (u, -1.0, 1.0); }
    static double F (double u) noexcept    { return std::abs (u) <= 1.0 ? 0.5 * u * u : std::abs (u) - 0.5; }

    void setBias (float b) noexcept { bias = b; biasOut = (float) clip (b); }
    void reset() noexcept { x1[0] = x1[1] = bias; F1[0] = F1[1] = F (bias); }

    inline float process (float xIn, int ch) noexcept
    {
        const double x = (double) xIn + bias;
        if (! enabled) { x1[ch] = x; return (float) clip (x) - biasOut; }
        const double Fx = F (x);
        const double dx = x - x1[ch];
        double y;
        if (std::abs (dx) < 1.0e-5) y = clip (0.5 * (x + x1[ch]));
        else                        y = (Fx - F1[ch]) / dx;
        x1[ch] = x; F1[ch] = Fx;
        return (float) y - biasOut;
    }
};

//==============================================================================
/** Simple xorshift RNG for noise/jitter (deterministic, allocation free). */
struct Random
{
    uint32_t s = 0x9E3779B9u;
    inline uint32_t nextU() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float bipolar() noexcept { return (float) ((double) nextU() / 2147483648.0 - 1.0); }
};

//==============================================================================
/** Tempo-sync divisions shared by delays and LFOs. Index 0 == "Off" (free running). */
inline const std::vector<std::string>& syncChoices()
{
    static const std::vector<std::string> c { "Off", "1/1", "1/2", "1/2 D", "1/4", "1/4 D", "1/4 T",
                                              "1/8", "1/8 D", "1/8 T", "1/16", "1/16 T", "1/32" };
    return c;
}
/** Length of a sync division in quarter-note beats. */
inline double syncBeats (int index) noexcept
{
    static const double b[] { 0.0, 4.0, 2.0, 3.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return (index > 0 && index < 13) ? b[index] : 0.0;
}
} // namespace pf
