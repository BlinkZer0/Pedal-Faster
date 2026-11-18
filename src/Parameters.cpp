#include "Parameters.h"
#include "dsp/Amps/AmpModels.h"
#include "dsp/Cabinets/Cabinet.h"
#include "dsp/EffectRegistry.h"

namespace pf
{
namespace
{
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;

juce::StringArray toStringArray (const std::vector<std::string>& v)
{
    juce::StringArray a; for (auto& s : v) a.add (s); return a;
}

std::unique_ptr<APF> dbParam (const juce::String& id, const juce::String& name, float lo, float hi, float def)
{
    return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi, 0.01f), def,
                                  juce::AudioParameterFloatAttributes().withLabel ("dB")
                                      .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; }));
}
std::unique_ptr<APF> pctParam (const juce::String& id, const juce::String& name, float def)
{
    return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def,
                                  juce::AudioParameterFloatAttributes()
                                      .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
}
std::unique_ptr<APF> freqParam (const juce::String& id, const juce::String& name, float lo, float hi, float def)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (std::sqrt (lo * hi));
    return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, r, def,
                                  juce::AudioParameterFloatAttributes().withLabel ("Hz")
                                      .withStringFromValueFunction ([] (float v, int) { return v >= 1000 ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }));
}
std::unique_ptr<APB> boolParam (const juce::String& id, const juce::String& name, bool def)
{
    return std::make_unique<APB> (juce::ParameterID { id, 1 }, name, def);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (SlotTypeQuery slotType)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto nonAuto = juce::AudioParameterChoiceAttributes().withAutomatable (false);

    // ---- global I/O
    auto io = std::make_unique<juce::AudioProcessorParameterGroup> ("io", "Input / Output", "|");
    io->addChild (dbParam (ids::inGain, "Input Gain", -24.0f, 24.0f, 0.0f));
    io->addChild (std::make_unique<APC> (juce::ParameterID { ids::inMode, 1 }, "Input Mode",
                                         juce::StringArray { "Stereo", "Left (Mono)", "Right (Mono)", "Sum to Mono" }, 0, nonAuto));
    io->addChild (boolParam (ids::bypass, "Global Bypass", false));
    io->addChild (boolParam (ids::gateOn, "Gate On", false));
    io->addChild (std::make_unique<APF> (juce::ParameterID { ids::gateThresh, 1 }, "Gate Threshold", juce::NormalisableRange<float> (-90.0f, -20.0f, 0.1f), -60.0f,
                                         juce::AudioParameterFloatAttributes().withLabel ("dB")));
    io->addChild (std::make_unique<APF> (juce::ParameterID { ids::gateRelease, 1 }, "Gate Release",
                                         juce::NormalisableRange<float> (5.0f, 500.0f, 0.1f, 0.5f), 80.0f, juce::AudioParameterFloatAttributes().withLabel ("ms")));
    io->addChild (dbParam (ids::outGain, "Preset Output", -24.0f, 12.0f, 0.0f));
    io->addChild (boolParam (ids::limOn, "Limiter On", true));
    io->addChild (dbParam (ids::limCeiling, "Limiter Ceiling", -12.0f, 0.0f, -0.5f));
    io->addChild (dbParam (ids::masterVol, "Master Volume", -60.0f, 6.0f, 0.0f));
    io->addChild (std::make_unique<APC> (juce::ParameterID { ids::outMode, 1 }, "Output Mode", juce::StringArray { "Stereo", "Mono" }, 0, nonAuto));
    io->addChild (std::make_unique<APC> (juce::ParameterID { ids::quality, 1 }, "Quality", juce::StringArray { "Eco", "Normal", "High" }, 1, nonAuto));
    io->addChild (std::make_unique<APF> (juce::ParameterID { ids::tempo, 1 }, "Tempo", juce::NormalisableRange<float> (40.0f, 240.0f, 0.1f), 120.0f,
                                         juce::AudioParameterFloatAttributes().withLabel ("BPM")));
    io->addChild (boolParam (ids::tempoHost, "Follow Host Tempo", true));
    io->addChild (boolParam (ids::tunerMute, "Tuner Mute", false));
    layout.add (std::move (io));

    // ---- master EQ
    auto eq = std::make_unique<juce::AudioProcessorParameterGroup> ("eq", "Master EQ", "|");
    eq->addChild (boolParam (ids::eqOn, "EQ On", true));
    eq->addChild (dbParam (ids::eqLow, "EQ Low", -12.0f, 12.0f, 0.0f));
    eq->addChild (dbParam (ids::eqMid, "EQ Mid", -12.0f, 12.0f, 0.0f));
    eq->addChild (freqParam (ids::eqMidFreq, "EQ Mid Freq", 200.0f, 5000.0f, 800.0f));
    eq->addChild (dbParam (ids::eqHigh, "EQ High", -12.0f, 12.0f, 0.0f));
    layout.add (std::move (eq));

    // ---- macros
    auto mac = std::make_unique<juce::AudioProcessorParameterGroup> ("macros", "Macros", "|");
    for (int i = 0; i < 4; ++i) mac->addChild (pctParam (ids::macro (i), "Macro " + macroNames()[i], 0.5f));
    layout.add (std::move (mac));

    // ---- amp
    auto amp = std::make_unique<juce::AudioProcessorParameterGroup> ("amp", "Amplifier", "|");
    amp->addChild (boolParam (ids::ampOn, "Amp On", true));
    amp->addChild (std::make_unique<APC> (juce::ParameterID { ids::ampType, 1 }, "Amp Model", toStringArray (ampTypeNames()), 0, nonAuto));
    amp->addChild (pctParam (ids::ampGain, "Amp Gain", 0.4f));
    amp->addChild (pctParam (ids::ampBass, "Amp Bass", 0.5f));
    amp->addChild (pctParam (ids::ampMid, "Amp Middle", 0.5f));
    amp->addChild (pctParam (ids::ampTreble, "Amp Treble", 0.5f));
    amp->addChild (pctParam (ids::ampPresence, "Amp Presence", 0.5f));
    amp->addChild (pctParam (ids::ampMaster, "Amp Master", 0.5f));
    amp->addChild (pctParam (ids::ampSag, "Amp Sag", 0.3f));
    amp->addChild (dbParam (ids::ampTrim, "Amp Output Trim", -24.0f, 12.0f, 0.0f));
    layout.add (std::move (amp));

    // ---- cabinet
    auto cab = std::make_unique<juce::AudioProcessorParameterGroup> ("cab", "Cabinet", "|");
    cab->addChild (boolParam (ids::cabOn, "Cab On", true));
    cab->addChild (std::make_unique<APC> (juce::ParameterID { ids::cabType, 1 }, "Cabinet", toStringArray (cabTypeNames()), (int) CabType::Closed4x12, nonAuto));
    cab->addChild (pctParam (ids::cabMic, "Mic Position", 0.3f));
    cab->addChild (freqParam (ids::cabLowCut, "Cab Low Cut", 20.0f, 400.0f, 70.0f));
    cab->addChild (freqParam (ids::cabHighCut, "Cab High Cut", 2000.0f, 20000.0f, 9000.0f));
    cab->addChild (dbParam (ids::cabLevel, "Cab Level", -24.0f, 12.0f, 0.0f));
    cab->addChild (pctParam (ids::cabMix, "Cab Mix", 1.0f));
    layout.add (std::move (cab));

    // ---- slots
    juce::StringArray typeNames;
    for (auto& e : EffectRegistry::get().all()) typeNames.add (e.name);
    for (int s = 0; s < kNumSlots; ++s)
    {
        const juce::String prefix = "Slot " + juce::String (s + 1) + " ";
        auto g = std::make_unique<juce::AudioProcessorParameterGroup> ("slot" + juce::String (s + 1), "Slot " + juce::String (s + 1), "|");
        g->addChild (std::make_unique<APC> (juce::ParameterID { slotTypeId (s), 1 }, prefix + "Type", typeNames, 0, nonAuto));
        g->addChild (boolParam (slotOnId (s), prefix + "On", true));
        g->addChild (pctParam (slotMixId (s), prefix + "Mix", 1.0f));
        for (int p = 0; p < kMaxEffectParams; ++p)
        {
            auto text = [slotType, s, p] (float v, int) -> juce::String
            {
                const int type = slotType ? slotType (s) : 0;
                const auto& info = EffectRegistry::get().at (type);
                if (p < (int) info.params.size()) return info.params[(size_t) p].format (info.params[(size_t) p].toReal (v));
                return juce::String (juce::roundToInt (v * 100)) + "%";
            };
            g->addChild (std::make_unique<APF> (juce::ParameterID { slotParamId (s, p), 1 }, prefix + "Param " + juce::String (p + 1),
                                                juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (text)));
        }
        layout.add (std::move (g));
    }
    return layout;
}
} // namespace pf
