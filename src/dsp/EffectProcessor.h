#pragma once
// Base interface for every pedal algorithm plus the parameter description model used by
// the registry, the host-automation layer, the UI and the preset serializer.

#include "DspCommon.h"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace pf
{
constexpr int kMaxEffectParams = 10;
constexpr int kNumSlots        = 8;

/** Information an effect may need from the host each block. */
struct ProcessContext
{
    double bpm        = 120.0;
    bool   playing    = false;
    double ppqPosition = 0.0;
    int    quality    = 1;        // 0 = Eco, 1 = Normal, 2 = High
};

/** Describes one effect parameter in real (display) units. */
struct ParamSpec
{
    std::string key;          // stable JSON key, e.g. "thresholdDb"
    std::string name;         // UI label
    float minValue = 0.0f, maxValue = 1.0f, defaultValue = 0.5f;
    std::string unit;         // "dB", "ms", "Hz", "%", "st", "x", "" ...
    bool logarithmic = false; // exponential mapping between min/max (both must be > 0)
    bool integer = false;     // rounds to whole numbers
    std::vector<std::string> choices; // non-empty => discrete choice, values are indices

    bool isChoice() const noexcept { return ! choices.empty(); }

    float toReal (float norm) const noexcept
    {
        norm = clampf (norm, 0.0f, 1.0f);
        if (isChoice())
            return (float) std::min ((int) choices.size() - 1, (int) std::lround (norm * (float) (choices.size() - 1)));
        float v = logarithmic ? minValue * std::pow (maxValue / minValue, norm)
                              : minValue + (maxValue - minValue) * norm;
        if (integer) v = std::round (v);
        return clampf (v, minValue, maxValue);
    }

    float toNorm (float real) const noexcept
    {
        if (isChoice())
            return choices.size() > 1 ? clampf (real, 0.0f, (float) choices.size() - 1) / (float) (choices.size() - 1) : 0.0f;
        real = clampf (real, minValue, maxValue);
        if (logarithmic) return clampf (std::log (real / minValue) / std::log (maxValue / minValue), 0.0f, 1.0f);
        return maxValue > minValue ? (real - minValue) / (maxValue - minValue) : 0.0f;
    }

    float realMin() const noexcept { return isChoice() ? 0.0f : minValue; }
    float realMax() const noexcept { return isChoice() ? (float) choices.size() - 1 : maxValue; }

    std::string format (float real) const
    {
        if (isChoice())
        {
            const int i = std::clamp ((int) std::lround (real), 0, (int) choices.size() - 1);
            return choices[(size_t) i];
        }
        char buf[64];
        if (unit == "%")
            std::snprintf (buf, sizeof (buf), "%d%%", (int) std::lround (real * 100.0f));
        else if (integer || std::abs (real) >= 100.0f)
            std::snprintf (buf, sizeof (buf), "%d%s%s", (int) std::lround (real), unit.empty() ? "" : " ", unit.c_str());
        else if (std::abs (real) >= 10.0f)
            std::snprintf (buf, sizeof (buf), "%.1f%s%s", real, unit.empty() ? "" : " ", unit.c_str());
        else
            std::snprintf (buf, sizeof (buf), "%.2f%s%s", real, unit.empty() ? "" : " ", unit.c_str());
        return buf;
    }
};

/** Convenience constructors used by the registry. */
inline ParamSpec pLin (const char* key, const char* name, float lo, float hi, float def, const char* unit = "")
{ ParamSpec p; p.key = key; p.name = name; p.minValue = lo; p.maxValue = hi; p.defaultValue = def; p.unit = unit; return p; }
inline ParamSpec pLog (const char* key, const char* name, float lo, float hi, float def, const char* unit = "")
{ auto p = pLin (key, name, lo, hi, def, unit); p.logarithmic = true; return p; }
inline ParamSpec pInt (const char* key, const char* name, float lo, float hi, float def, const char* unit = "")
{ auto p = pLin (key, name, lo, hi, def, unit); p.integer = true; return p; }
inline ParamSpec pPct (const char* key, const char* name, float def)
{ return pLin (key, name, 0.0f, 1.0f, def, "%"); }
inline ParamSpec pChoice (const char* key, const char* name, std::vector<std::string> choices, int def)
{ ParamSpec p; p.key = key; p.name = name; p.choices = std::move (choices); p.minValue = 0; p.maxValue = (float) p.choices.size() - 1; p.defaultValue = (float) def; return p; }

//==============================================================================
/**
    A pedal algorithm. Processes a stereo pair in place, 100% wet.
    Wet/dry mixing and bypass ramps are handled by the owning slot.

    Threading: prepare() runs off the audio thread; reset(), setParameter() and process() run on the
    audio thread and must not allocate, lock or block.
*/
class Effect
{
public:
    virtual ~Effect() = default;

    void prepareEffect (double sampleRate, int maxBlockSize)
    {
        sr = sampleRate;
        maxBlock = maxBlockSize;
        prepare (sampleRate, maxBlockSize);
    }

    /** Called with real-unit values; the effect smooths internally. */
    virtual void setParameter (int index, float value) noexcept = 0;

    /** Clears internal state and snaps all smoothers to their targets. */
    virtual void reset() noexcept = 0;

    virtual void process (float* left, float* right, int numSamples, const ProcessContext& ctx) noexcept = 0;

    /** True for effects whose output keeps ringing (delays, reverbs): bypassing lets their tail spill over. */
    virtual bool hasTail() const noexcept { return false; }

protected:
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    double sr = 44100.0;
    int maxBlock = 512;
};
} // namespace pf
