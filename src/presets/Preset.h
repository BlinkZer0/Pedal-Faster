#pragma once
// In-memory preset model. Serialised as human-readable JSON (see docs/PRESET_FORMAT.md).

#include <map>
#include <string>
#include <vector>

namespace pf
{
constexpr int kPresetSchemaVersion = 1;

struct MacroMapping
{
    int macro = 0;          // 0 Drive, 1 Tone, 2 Space, 3 Output
    std::string target;     // parameter id, e.g. "amp_gain" or "slot:2:mix" (slot index in preset order) / "slot:2:drive"
    float amount = 0.25f;   // normalised offset at the macro's extremes (-1..1)
};

struct EffectState
{
    std::string slotId;               // "slot-1".."slot-8"
    std::string type;                 // registry id
    bool pre = true;
    bool enabled = true;
    float mix = -1.0f;                // < 0: effect default
    std::map<std::string, float> params; // real units keyed by ParamSpec::key
};

struct Preset
{
    int schemaVersion = kPresetSchemaVersion;
    std::string id, name, category, description, author = "Pedal Faster";
    std::vector<std::string> tags;
    bool factory = false;

    float inputGainDb = 0.0f;
    struct Gate { bool enabled = false; float thresholdDb = -60.0f, releaseMs = 80.0f; } gate;
    struct Amp
    {
        std::string type = "studio_clean"; bool enabled = true;
        float gain = 0.4f, bass = 0.5f, mid = 0.5f, treble = 0.5f, presence = 0.5f, master = 0.5f, sag = 0.3f, trimDb = 0.0f;
    } amp;
    struct Cab
    {
        std::string type = "closed_4x12"; bool enabled = true;
        float mic = 0.3f, lowCutHz = 70.0f, highCutHz = 9000.0f, levelDb = 0.0f, mix = 1.0f;
        std::string irFile;            // only for custom_ir
    } cab;
    std::vector<EffectState> effects;  // signal order
    struct Eq { bool enabled = true; float lowDb = 0, midDb = 0, midFreqHz = 800, highDb = 0; } eq;
    struct Limiter { bool enabled = true; float ceilingDb = -0.5f; } limiter;
    std::vector<MacroMapping> macros;  // empty => derived automatically from the chain
    float outputGainDb = 0.0f;
    float tempoBpm = 0.0f;             // 0 => keep current tempo

    /** 0..10 estimate of how much distortion the preset produces (for sorting). */
    int gainLevel() const;
    bool hasTag (const std::string& t) const;
};
} // namespace pf
