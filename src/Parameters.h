#pragma once
// Host-visible parameters. IDs are part of saved projects: never rename or remove one.
//
// Automation follows the SLOT, not the effect: "s3_p1" is "slot 3, parameter 1" whatever pedal is
// loaded there. Slot parameters are stored normalised (0..1) and mapped to the loaded pedal's real
// range through the effect registry; their host text shows the pedal's real value and unit.

#include "dsp/EffectProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace pf
{
namespace ids
{
    inline constexpr const char* inGain = "in_gain";       inline constexpr const char* inMode = "in_mode";
    inline constexpr const char* bypass = "bypass";
    inline constexpr const char* gateOn = "gate_on";       inline constexpr const char* gateThresh = "gate_thresh";
    inline constexpr const char* gateRelease = "gate_release";
    inline constexpr const char* ampOn = "amp_on";         inline constexpr const char* ampType = "amp_type";
    inline constexpr const char* ampGain = "amp_gain";     inline constexpr const char* ampBass = "amp_bass";
    inline constexpr const char* ampMid = "amp_mid";       inline constexpr const char* ampTreble = "amp_treble";
    inline constexpr const char* ampPresence = "amp_presence"; inline constexpr const char* ampMaster = "amp_master";
    inline constexpr const char* ampSag = "amp_sag";       inline constexpr const char* ampTrim = "amp_trim";
    inline constexpr const char* cabOn = "cab_on";         inline constexpr const char* cabType = "cab_type";
    inline constexpr const char* cabMic = "cab_mic";       inline constexpr const char* cabLowCut = "cab_lowcut";
    inline constexpr const char* cabHighCut = "cab_highcut"; inline constexpr const char* cabLevel = "cab_level";
    inline constexpr const char* cabMix = "cab_mix";
    inline constexpr const char* eqOn = "eq_on";           inline constexpr const char* eqLow = "eq_low";
    inline constexpr const char* eqMid = "eq_mid";         inline constexpr const char* eqMidFreq = "eq_midfreq";
    inline constexpr const char* eqHigh = "eq_high";
    inline constexpr const char* outGain = "out_gain";     inline constexpr const char* limOn = "lim_on";
    inline constexpr const char* limCeiling = "lim_ceiling"; inline constexpr const char* masterVol = "master_vol";
    inline constexpr const char* outMode = "out_mode";     inline constexpr const char* quality = "quality";
    inline constexpr const char* tempo = "tempo";          inline constexpr const char* tempoHost = "tempo_host";
    inline constexpr const char* tunerMute = "tuner_mute";
    inline const char* macro (int i) { static const char* m[] { "macro_drive", "macro_tone", "macro_space", "macro_output" }; return m[std::clamp (i, 0, 3)]; }
}

inline juce::String slotTypeId (int slot)            { return "s" + juce::String (slot + 1) + "_type"; }
inline juce::String slotOnId (int slot)              { return "s" + juce::String (slot + 1) + "_on"; }
inline juce::String slotMixId (int slot)             { return "s" + juce::String (slot + 1) + "_mix"; }
inline juce::String slotParamId (int slot, int p)    { return "s" + juce::String (slot + 1) + "_p" + juce::String (p + 1); }

inline const juce::StringArray& macroNames() { static const juce::StringArray n { "Drive", "Tone", "Space", "Output" }; return n; }

/** Callback used to render slot parameter text according to the pedal currently loaded in the slot. */
using SlotTypeQuery = std::function<int (int slot)>;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (SlotTypeQuery slotType);
} // namespace pf
