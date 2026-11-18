#pragma once
#include "Widgets.h"

namespace pf::ui
{
/** Right panel: every control of the selected module (pedal slot, amp, cabinet or master section). */
class ParameterEditor : public juce::Component
{
public:
    explicit ParameterEditor (EditorContext& ctx);
    void showTarget (int target);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void build();
    void addKnob (juce::RangedAudioParameter& p, const juce::String& label, juce::Colour accent);
    void addSpecKnob (int slot, int k, const ParamSpec& spec, juce::Colour accent);
    void addCombo (std::unique_ptr<juce::Component> c, const juce::String& label);
    void addToggle (juce::RangedAudioParameter& p, const juce::String& label);

    EditorContext& ctx;
    int target = kTargetAmp, builtType = -2;
    juce::String title, subtitle;
    juce::Colour accent { colours::amber };

    struct Item { std::unique_ptr<juce::Component> comp; std::unique_ptr<juce::Label> label; bool wide = false; };
    std::vector<Item> items;
    juce::OwnedArray<juce::TextButton> actions;
    juce::Component content;
    juce::Viewport viewport;
    std::unique_ptr<juce::ComboBox> slotTypeBox;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace pf::ui
