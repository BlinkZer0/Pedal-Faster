#pragma once
#include "Parameters.h"
#include "dsp/AudioEngine.h"
#include "dsp/Tuner.h"
#include "presets/PresetManager.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace pf
{
constexpr const char* kProductName = "Pedal Faster";

/** A macro mapping resolved to a host parameter index (audio-thread friendly). */
struct ResolvedMacro { int macro = 0; int param = -1; float amount = 0.0f; };

class PedalFasterProcessor final : public juce::AudioProcessor,
                                   private juce::Timer,
                                   private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit PedalFasterProcessor (juce::File presetRoot = {});
    ~PedalFasterProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return kProductName; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return currentPresetName; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- rig API (message thread)
    juce::AudioProcessorValueTreeState apvts;
    PresetManager& presets() noexcept { return presetManager; }
    AudioEngine& getEngine() noexcept { return engine; }

    Routing getRouting() const noexcept { return engine.getRouting(); }
    void setRouting (const Routing& r);
    int slotType (int slot) const;
    void setSlotType (int slot, int type);
    void resetSlot (int slot);
    void moveSlot (int slot, int newPosition, bool pre);
    EffectState captureSlot (int slot) const;
    void applySlot (int slot, const EffectState& e);
    void setParamReal (const juce::String& id, float realValue);
    void setParamNorm (const juce::String& id, float norm);
    float getParamReal (const juce::String& id) const;
    juce::RangedAudioParameter* param (const juce::String& id) const { return apvts.getParameter (id); }

    void applyPreset (const Preset& p);
    Preset captureState() const;
    bool loadPresetById (const std::string& id);
    void stepPreset (int delta, const std::vector<const Preset*>& list);
    std::string currentPresetId;
    juce::String currentPresetName { "Init" };
    bool isDirty() const noexcept { return dirty.load(); }
    void clearDirty() { dirty = false; }
    std::vector<MacroMapping> getMacroMappings() const { return macroMappings; }
    void setMacroMappings (const std::vector<MacroMapping>& m);
    static std::vector<MacroMapping> deriveMacros (const Preset& p);

    juce::Result loadImpulseResponse (const juce::File& file);
    juce::String getImpulseResponseName() const { return irName; }
    void clearImpulseResponse();

    void tapTempo();
    float currentBpm() const noexcept { return lastBpm.load(); }
    float cpuLoad() const noexcept { return (float) loadMeasurer.getLoadAsProportion(); }
    int latencyForCurrentQuality() const;

    Tuner tuner;                        // UI-side analysis; call updateTuner() from a UI timer
    void updateTuner();

    /** Runs the periodic housekeeping normally driven by the message-thread timer
        (slot building, garbage collection, MIDI-triggered actions, latency updates). */
    void runMaintenance();

    /** Bumped whenever a preset or saved state is applied; the editor polls it to refresh. */
    std::atomic<int> stateVersion { 0 };
    std::vector<std::string> lastPresetWarnings;

    std::atomic<int> midiProgramRequest { -1 }, midiNavRequest { 0 };

private:
    void timerCallback() override { runMaintenance(); }
    void parameterChanged (const juce::String&, float) override;
    void buildEngineParams (EngineParams& p) const noexcept;
    void publishMacros();
    void rebuildParamCache();
    void reloadIrIntoEngine();
    void applySlotNoSync (int slot, const EffectState& e);

    PresetManager presetManager;
    AudioEngine engine;
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<std::atomic<float>*> raw;
    std::unordered_map<juce::String, int> paramIndex;
    struct Indices
    {
        int inGain, inMode, bypass, gateOn, gateThresh, gateRelease, ampOn, ampType, ampGain, ampBass, ampMid, ampTreble,
            ampPresence, ampMaster, ampSag, ampTrim, cabOn, cabType, cabMic, cabLowCut, cabHighCut, cabLevel, cabMix,
            eqOn, eqLow, eqMid, eqMidFreq, eqHigh, outGain, limOn, limCeiling, masterVol, outMode, quality, tempo, tempoHost, tunerMute;
        int macro[4];
        int slotType[kNumSlots], slotOn[kNumSlots], slotMix[kNumSlots], slotP[kNumSlots][kMaxEffectParams];
    } ix {};

    std::vector<MacroMapping> macroMappings;
    std::array<ResolvedMacro, 32> macroBuf[2];
    std::atomic<int> macroCount[2] { 0, 0 };
    std::atomic<int> macroActive { 0 };

    std::atomic<bool> dirty { false };
    bool applyingPreset = false;
    std::atomic<float> midiExpression { -1.0f }, midiVolume { 1.0f };
    std::atomic<float> midiMacro[4] { -1.0f, -1.0f, -1.0f, -1.0f };
    std::atomic<float> lastBpm { 120.0f };
    std::atomic<int> latencyQuality { -1 };

    juce::AudioProcessLoadMeasurer loadMeasurer;
    std::vector<float> scratchR, tunerScratch;
    std::vector<double> tapTimes;
    std::atomic<double> sampleRateCache { 44100.0 };
    int preparedBlock = 512;
    double tunerRate = 0.0;

    juce::AudioBuffer<float> irData; double irRate = 48000.0; juce::String irName, irPath;
    juce::AudioFormatManager formatManager;
    mutable EngineParams scratchParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalFasterProcessor)
};
} // namespace pf
