#pragma once
#include "Preset.h"
#include <juce_core/juce_core.h>

namespace pf
{
struct ParseResult
{
    bool ok = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

class PresetSerializer
{
public:
    static juce::var toVar (const Preset& p);
    static juce::String toJson (const Preset& p);

    /** Reads (and migrates) a preset. Fails on malformed data, unknown types or future schema versions. */
    static ParseResult fromVar (const juce::var& v, Preset& out);
    static ParseResult fromJson (const juce::String& json, Preset& out);

    /** Bank files hold several presets: { "bankFormat": 1, "name": ..., "presets": [ ... ] } */
    static juce::String bankToJson (const juce::String& bankName, const std::vector<Preset>& presets);
    static ParseResult bankFromJson (const juce::String& json, std::vector<Preset>& out);

    static const char* macroIdFor (int macro);
};

class PresetValidator
{
public:
    /** Strict validation: every type, key and value must be known and in range. */
    static std::vector<std::string> validate (const Preset& p);
};
} // namespace pf
