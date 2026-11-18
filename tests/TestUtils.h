#pragma once
// Shared helpers for the test runner: synthetic guitar DI, rendering and simple audio metrics.

#include "PluginProcessor.h"
#include "dsp/EffectRegistry.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace pft
{
using namespace pf;

/** Karplus-Strong plucked-string performance: single notes, a power chord and an open chord. Peak ~ -6 dBFS. */
inline juce::AudioBuffer<float> makeGuitarDI (double sr, double seconds = 4.0, unsigned seed = 1)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> buf (2, n);
    buf.clear();
    juce::Random rng ((juce::int64) seed);
    struct Note { double t; float hz; float vel; };
    std::vector<Note> notes {
        { 0.00, 82.41f, 1.0f }, { 0.35, 110.0f, 0.8f }, { 0.70, 146.83f, 0.8f }, { 1.05, 196.0f, 0.7f },
        { 1.50, 82.41f, 1.0f }, { 1.50, 123.47f, 0.9f }, { 1.50, 164.81f, 0.8f },                           // power chord
        { 2.40, 110.0f, 0.8f }, { 2.40, 164.81f, 0.7f }, { 2.40, 220.0f, 0.7f }, { 2.40, 277.18f, 0.6f }, { 2.40, 329.63f, 0.6f },
        { 3.30, 329.63f, 0.9f }, { 3.55, 392.0f, 0.8f }, { 3.75, 440.0f, 0.9f } };
    std::vector<float> mix ((size_t) n, 0.0f);
    for (auto& note : notes)
    {
        const int start = (int) (note.t * sr);
        if (start >= n) continue;
        const int period = std::max (2, (int) std::round (sr / note.hz));
        std::vector<float> line ((size_t) period);
        for (auto& s : line) s = rng.nextFloat() * 2.0f - 1.0f;
        float prev = 0.0f;
        for (int i = start, k = 0; i < n; ++i, ++k)
        {
            const size_t idx = (size_t) (k % period);
            const float cur = line[idx];
            const float next = line[(idx + 1) % (size_t) period];
            line[idx] = 0.4985f * (cur + next) * 0.999f + 0.0f * prev;
            prev = cur;
            mix[(size_t) i] += cur * note.vel * 0.18f;
        }
    }
    float peak = 0.0f;
    for (auto v : mix) peak = std::max (peak, std::abs (v));
    const float g = peak > 0 ? 0.5f / peak : 1.0f;
    for (int i = 0; i < n; ++i) { buf.setSample (0, i, mix[(size_t) i] * g); buf.setSample (1, i, mix[(size_t) i] * g); }
    return buf;
}

inline juce::AudioBuffer<float> makeSine (double sr, float hz, float amp, double seconds, int channels = 2)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> b (channels, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = amp * std::sin (2.0f * juce::MathConstants<float>::pi * hz * (float) i / (float) sr);
        for (int c = 0; c < channels; ++c) b.setSample (c, i, v);
    }
    return b;
}

inline float rmsDb (const juce::AudioBuffer<float>& b, int start = 0, int len = -1)
{
    if (len < 0) len = b.getNumSamples() - start;
    double acc = 0; int count = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = start; i < start + len; ++i) { const double v = b.getSample (c, i); acc += v * v; ++count; }
    return (float) (10.0 * std::log10 (std::max (1.0e-20, acc / std::max (1, count))));
}

inline float peakAbs (const juce::AudioBuffer<float>& b)
{
    float p = 0; for (int c = 0; c < b.getNumChannels(); ++c) p = std::max (p, b.getMagnitude (c, 0, b.getNumSamples()));
    return p;
}

inline bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

/** Magnitude spectrum (dB) in 1/3-octave-ish log bands of channel 0 via a single large DFT-free Goertzel sweep. */
inline std::vector<float> bandProfile (const juce::AudioBuffer<float>& b, double sr, int start = 0)
{
    static const float centres[] { 60, 90, 130, 180, 250, 350, 500, 700, 1000, 1400, 2000, 2800, 4000, 5600, 8000, 11000 };
    std::vector<float> out;
    const int len = std::min (b.getNumSamples() - start, (int) sr);
    for (float f : centres)
    {
        // sum of Goertzel powers at a few frequencies around the band centre
        double power = 0;
        for (float m : { 0.85f, 1.0f, 1.15f })
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * f * m / sr, coeff = 2.0 * std::cos (w);
            double s1 = 0, s2 = 0;
            for (int i = 0; i < len; ++i)
            {
                const double win = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / (len - 1));
                const double s0 = b.getSample (0, start + i) * win + coeff * s1 - s2;
                s2 = s1; s1 = s0;
            }
            power += s1 * s1 + s2 * s2 - coeff * s1 * s2;
        }
        out.push_back ((float) (10.0 * std::log10 (power + 1.0e-12)));
    }
    return out;
}

inline float profileDistance (const std::vector<float>& a, const std::vector<float>& b)
{
    // shape distance (level-independent): compare profiles after removing their means
    float ma = 0, mb = 0;
    for (size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }
    ma /= (float) a.size(); mb /= (float) b.size();
    float d = 0;
    for (size_t i = 0; i < a.size(); ++i) d += std::abs ((a[i] - ma) - (b[i] - mb));
    return d / (float) a.size();
}

/** Total harmonic distortion (ratio, 0..1) of a sine response at fundamental f0. */
inline float thd (const juce::AudioBuffer<float>& b, double sr, float f0, int start)
{
    const int len = std::min (b.getNumSamples() - start, (int) (sr * 0.5));
    auto goertzel = [&] (double f)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, coeff = 2.0 * std::cos (w);
        double s1 = 0, s2 = 0;
        for (int i = 0; i < len; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / (len - 1));
            const double s0 = b.getSample (0, start + i) * win + coeff * s1 - s2; s2 = s1; s1 = s0;
        }
        return std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2));
    };
    const double fund = goertzel (f0);
    double harm = 0;
    for (int h = 2; h <= 8; ++h) if (f0 * h < sr * 0.45) { const double a = goertzel (f0 * h); harm += a * a; }
    return fund > 0 ? (float) (std::sqrt (harm) / fund) : 0.0f;
}

/** Processes a buffer through a processor in host-sized blocks (copy in, returns output). */
inline juce::AudioBuffer<float> render (juce::AudioProcessor& p, const juce::AudioBuffer<float>& in, int block)
{
    juce::AudioBuffer<float> out (in);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < out.getNumSamples(); pos += block)
    {
        const int n = std::min (block, out.getNumSamples() - pos);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), pos, n);
        p.processBlock (view, midi);
    }
    return out;
}

/** Renders a single effect algorithm (100% wet) at real-unit parameters keyed by spec key. */
inline juce::AudioBuffer<float> renderEffect (const std::string& id, const std::map<std::string, float>& params,
                                              const juce::AudioBuffer<float>& in, double sr, int block, int quality = 1)
{
    const auto* info = EffectRegistry::get().find (id);
    auto fx = info->create();
    fx->prepareEffect (sr, block);
    for (size_t k = 0; k < info->params.size(); ++k)
    {
        auto it = params.find (info->params[k].key);
        fx->setParameter ((int) k, it != params.end() ? it->second : info->params[k].defaultValue);
    }
    fx->reset();
    juce::AudioBuffer<float> out (in);
    ProcessContext ctx; ctx.quality = quality; ctx.bpm = 120;
    for (int pos = 0; pos < out.getNumSamples(); pos += block)
    {
        const int n = std::min (block, out.getNumSamples() - pos);
        fx->process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), n, ctx);
    }
    return out;
}

/** A processor whose preset folder is an isolated temp directory. */
inline std::unique_ptr<PedalFasterProcessor> makeProcessor (double sr = 48000.0, int block = 256)
{
    static juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PedalFasterTests");
    auto p = std::make_unique<PedalFasterProcessor> (dir);
    p->setPlayConfigDetails (2, 2, sr, block);
    p->prepareToPlay (sr, block);
    return p;
}

/** Applies a preset and lets the engine adopt every new slot (runs maintenance + a few silent blocks). */
inline void loadAndSettle (PedalFasterProcessor& p, const Preset& preset, int block = 256)
{
    p.applyPreset (preset);
    p.runMaintenance();
    juce::AudioBuffer<float> silence (2, block);
    juce::MidiBuffer midi;
    for (int i = 0; i < 4; ++i) { silence.clear(); p.processBlock (silence, midi); }
    p.prepareToPlay (p.getSampleRate(), block); // fresh, deterministic state built from the preset's parameters
}
} // namespace pft
