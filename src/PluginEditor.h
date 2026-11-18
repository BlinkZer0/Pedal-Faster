#pragma once
#include "PluginProcessor.h"
#include "ui/ParameterEditor.h"
#include "ui/PresetBrowser.h"
#include "ui/SignalChainView.h"

namespace pf
{
class PedalFasterEditor final : public juce::AudioProcessorEditor,
                                public juce::DragAndDropContainer,
                                private juce::Timer
{
public:
    explicit PedalFasterEditor (PedalFasterProcessor&);
    ~PedalFasterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void select (int target);
    void refreshAll();
    void showPresetMenu();
    void showFileMenu();
    void savePreset (bool saveAs);
    void prevNext (int delta);
    void updatePresetLabel();
    void drawTuner (juce::Graphics&, juce::Rectangle<int>);

    PedalFasterProcessor& proc;
    ui::Theme theme;
    ui::EditorContext ctx;
    std::unique_ptr<juce::PropertiesFile> settings;

    // top bar
    juce::TextButton presetButton, prevButton { "<" }, nextButton { ">" }, saveButton { "Save" }, fileButton { "..." },
                     favButton, tunerButton { "Tuner" }, masterButton { "Master" };
    juce::TextEditor searchBox;
    juce::ComboBox categoryBox;
    std::unique_ptr<ui::ParamToggle> bypassButton;
    std::unique_ptr<ui::ParamCombo> qualityBox;
    ui::LevelMeter inMeter, outMeter;
    juce::Label cpuLabel, inLabel { {}, "IN" }, outLabel { {}, "OUT" };

    // centre
    ui::PresetBrowser browser;
    ui::SignalChainView chain;
    ui::ParameterEditor editorPanel;

    // bottom bar
    juce::OwnedArray<ui::Knob> bottomKnobs;
    std::unique_ptr<ui::ParamToggle> gateButton, limiterButton;
    std::unique_ptr<ui::ParamCombo> outModeBox;
    juce::TextButton tapButton { "TAP" };
    juce::Label bpmLabel, limiterLabel { {}, "LIMIT" };
    ui::Led limiterLed;

    bool tunerVisible = false;
    juce::Rectangle<int> tunerArea, topBar, bottomBar;
    int lastStateVersion = -1;
    bool lastDirty = false;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Rectangle<int> brandArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalFasterEditor)
};
} // namespace pf
