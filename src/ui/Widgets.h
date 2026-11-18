#pragma once
// Parameter-bound controls. All bindings use juce::ParameterAttachment so host automation,
// gestures and undo work, and the controls follow host-side changes.

#include "../PluginProcessor.h"
#include "Theme.h"

namespace pf::ui
{
constexpr int kTargetAmp = 100, kTargetCab = 101, kTargetMaster = 102;

/** Shared editor state handed to every panel. */
struct EditorContext
{
    PedalFasterProcessor& proc;
    int selected = kTargetAmp;
    std::function<void (int)> select;        // select a module for the parameter editor
    std::function<void()> rigChanged;        // routing/type changed: refresh views
    std::optional<EffectState> clipboard;
};

//==============================================================================
/** Rotary knob: drag, wheel, double-click reset, shift-drag fine adjust, numeric entry, value read-out. */
class Knob : public juce::Component
{
public:
    explicit Knob (bool compact = false);
    ~Knob() override;

    /** Bind to a host parameter; text uses the parameter's own formatting. */
    void bind (juce::RangedAudioParameter& p, const juce::String& label);
    /** Bind to a slot parameter, displayed in the pedal's real units. */
    void bindSpec (juce::RangedAudioParameter& p, const ParamSpec& spec);
    void setAccent (juce::Colour c) { slider.setColour (juce::Slider::rotarySliderFillColourId, c); }

    void resized() override;
    void parentHierarchyChanged() override { slider.sendLookAndFeelChange(); }

    class FineSlider : public juce::Slider
    {
    public:
        void mouseDown (const juce::MouseEvent& e) override
        {
            setMouseDragSensitivity (e.mods.isShiftDown() || e.mods.isCtrlDown() ? 1600 : 220);
            juce::Slider::mouseDown (e);
        }
    };
    FineSlider slider;
    juce::Label caption;

private:
    void attach (juce::RangedAudioParameter& p, float defaultNorm);
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::RangedAudioParameter* param = nullptr;
    bool compact = false, dragging = false;
};

/** Toggle button bound to a boolean parameter. */
class ParamToggle : public juce::TextButton
{
public:
    ParamToggle (juce::RangedAudioParameter& p, const juce::String& text);
private:
    juce::ParameterAttachment attachment;
};

/** ComboBox bound to a choice parameter, or to a choice-type slot parameter (normalised storage). */
class ParamCombo : public juce::ComboBox
{
public:
    ParamCombo (juce::RangedAudioParameter& p, const juce::StringArray& items, int numChoices);
private:
    juce::ParameterAttachment attachment;
    int numChoices;
    bool normalisedStorage;
};

/** Horizontal level meter with peak decay; call update() from a UI timer. */
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (std::atomic<float>& src) : source (src) {}
    void update();
    void paint (juce::Graphics&) override;
private:
    std::atomic<float>& source;
    float level = 0.0f, peakHold = 0.0f; int holdFrames = 0;
};

class Led : public juce::Component
{
public:
    void setOn (bool b) { if (b != on) { on = b; repaint(); } }
    void setColour (juce::Colour c) { colour = c; }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const float d = std::min (r.getWidth(), r.getHeight());
        auto e = r.withSizeKeepingCentre (d, d);
        g.setColour (on ? colour : colour.withAlpha (0.15f));
        g.fillEllipse (e);
    }
private:
    bool on = false; juce::Colour colour { colours::danger };
};

/** Small section header label. */
inline void styleCaption (juce::Label& l, float size = 11.0f, juce::Colour c = colours::textDim)
{
    l.setFont (Theme::font (size));
    l.setColour (juce::Label::textColourId, c);
    l.setJustificationType (juce::Justification::centred);
    l.setInterceptsMouseClicks (false, false);
}

/** Populates a ComboBox with the registry grouped by category (item id = type index + 1). */
void fillEffectTypeCombo (juce::ComboBox& box);
} // namespace pf::ui
