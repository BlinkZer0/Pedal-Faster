#pragma once
// Chromatic tuner: YIN pitch estimation on a decimated analysis window. Runs on the UI thread,
// fed from the engine's lock-free tuner FIFO.

#include "DspCommon.h"

namespace pf
{
class Tuner
{
public:
    struct Reading { bool valid = false; float hz = 0.0f; int midiNote = 0; float cents = 0.0f; const char* name = "-"; int octave = 0; };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        decim = std::max (1, (int) std::round (sr / 11025.0));
        dsr = sr / decim;
        window.assign (kWindow, 0.0f);
        diff.assign (kWindow / 2, 0.0f);
        aa.setCutoff (sr, dsr * 0.4);
        pos = 0; count = 0;
    }

    /** Feed raw input samples; analysis runs whenever enough new data has arrived. */
    void push (const float* x, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float y = aa.lowpass (x[i], 0);
            if (++count >= decim) { count = 0; window[(size_t) pos] = y; pos = (pos + 1) % kWindow; ++fresh; }
        }
        if (fresh >= kWindow / 4) { fresh = 0; analyse(); }
    }

    Reading reading() const noexcept { return last; }

private:
    void analyse()
    {
        std::vector<float> w ((size_t) kWindow);
        float energy = 0.0f;
        for (int i = 0; i < kWindow; ++i) { w[(size_t) i] = window[(size_t) ((pos + i) % kWindow)]; energy += w[(size_t) i] * w[(size_t) i]; }
        if (energy / kWindow < 1.0e-7f) { last.valid = false; return; }

        const int half = kWindow / 2;
        const int minTau = std::max (2, (int) (dsr / 1400.0)), maxTau = std::min (half - 1, (int) (dsr / 30.0));
        diff[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau < half; ++tau)
        {
            float d = 0.0f;
            for (int i = 0; i < half; ++i) { const float v = w[(size_t) i] - w[(size_t) (i + tau)]; d += v * v; }
            running += d;
            diff[(size_t) tau] = running > 0.0f ? d * (float) tau / running : 1.0f;
        }
        int tau = -1;
        for (int t = minTau; t <= maxTau; ++t)
            if (diff[(size_t) t] < 0.15f) { while (t + 1 <= maxTau && diff[(size_t) (t + 1)] < diff[(size_t) t]) ++t; tau = t; break; }
        if (tau < 0) { last.valid = false; return; }

        // parabolic interpolation
        float betterTau = (float) tau;
        if (tau > 1 && tau < half - 1)
        {
            const float s0 = diff[(size_t) (tau - 1)], s1 = diff[(size_t) tau], s2 = diff[(size_t) (tau + 1)];
            const float denom = 2.0f * (2.0f * s1 - s2 - s0);
            if (std::abs (denom) > 1.0e-9f) betterTau += (s2 - s0) / denom;
        }
        const float hz = (float) (dsr / betterTau);
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const int note = (int) std::lround (midi);
        last.valid = true; last.hz = hz; last.midiNote = note; last.cents = (midi - (float) note) * 100.0f;
        last.name = names[((note % 12) + 12) % 12]; last.octave = note / 12 - 1;
    }

    static constexpr int kWindow = 1024;
    double sr = 44100.0, dsr = 11025.0; int decim = 4;
    std::vector<float> window, diff; int pos = 0, count = 0, fresh = 0;
    OnePole aa; Reading last;
};
} // namespace pf
