#include "Widgets.h"

namespace pf::ui
{
Knob::Knob (bool isCompact) : compact (isCompact)
{
    slider.getProperties().set ("compact", compact);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, compact ? 13 : 16);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.setScrollWheelEnabled (true);
    slider.setRange (0.0, 1.0, 0.0);
    slider.setWantsKeyboardFocus (true);
    addAndMakeVisible (slider);
    styleCaption (caption, compact ? 10.5f : 12.0f, colours::textDim);
    addAndMakeVisible (caption);
}

Knob::~Knob() = default;

void Knob::attach (juce::RangedAudioParameter& p, float defaultNorm)
{
    param = &p;
    slider.setDoubleClickReturnValue (true, defaultNorm);
    slider.onDragStart = [this] { dragging = true; if (attachment) attachment->beginGesture(); };
    slider.onDragEnd   = [this] { dragging = false; if (attachment) attachment->endGesture(); };
    slider.onValueChange = [this]
    {
        if (attachment == nullptr || param == nullptr) return;
        const float v = param->convertFrom0to1 ((float) slider.getValue());
        if (dragging) attachment->setValueAsPartOfGesture (v);
        else attachment->setValueAsCompleteGesture (v);
    };
    attachment = std::make_unique<juce::ParameterAttachment> (p, [this] (float denorm)
    {
        slider.setValue (param->convertTo0to1 (denorm), juce::dontSendNotification);
        slider.updateText();
    });
    attachment->sendInitialUpdate();
    slider.setTitle (caption.getText());
    slider.setHelpText (p.getName (64));
}

void Knob::bind (juce::RangedAudioParameter& p, const juce::String& label)
{
    caption.setText (label, juce::dontSendNotification);
    slider.textFromValueFunction = [&p] (double v) { return p.getText ((float) v, 32) + (p.getLabel().isNotEmpty() && ! p.getText ((float) v, 32).contains (p.getLabel()) ? " " + p.getLabel() : juce::String()); };
    slider.valueFromTextFunction = [&p] (const juce::String& t) { return (double) p.convertTo0to1 (t.retainCharacters ("-0123456789.").getFloatValue()); };
    attach (p, p.getDefaultValue());
}

void Knob::bindSpec (juce::RangedAudioParameter& p, const ParamSpec& spec)
{
    caption.setText (spec.name, juce::dontSendNotification);
    slider.textFromValueFunction = [spec] (double v) { return juce::String (spec.format (spec.toReal ((float) v))); };
    slider.valueFromTextFunction = [spec] (const juce::String& t)
    {
        float v = t.retainCharacters ("-0123456789.").getFloatValue();
        if (spec.unit == "%") v /= 100.0f;
        return (double) spec.toNorm (v);
    };
    attach (p, spec.toNorm (spec.defaultValue));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    caption.setBounds (r.removeFromTop (compact ? 13 : 15));
    slider.setBounds (r);
}

//==============================================================================
ParamToggle::ParamToggle (juce::RangedAudioParameter& p, const juce::String& text)
    : attachment (p, [this] (float v) { setToggleState (v > 0.5f, juce::dontSendNotification); })
{
    setButtonText (text);
    setClickingTogglesState (true);
    onClick = [this] { attachment.setValueAsCompleteGesture (getToggleState() ? 1.0f : 0.0f); };
    attachment.sendInitialUpdate();
    setTitle (p.getName (64));
}

ParamCombo::ParamCombo (juce::RangedAudioParameter& p, const juce::StringArray& items, int n)
    : attachment (p, [this, &p] (float v)
      {
          const int idx = normalisedStorage ? juce::roundToInt (p.convertTo0to1 (v) * (float) (numChoices - 1)) : juce::roundToInt (v);
          setSelectedItemIndex (juce::jlimit (0, numChoices - 1, idx), juce::dontSendNotification);
      }),
      numChoices (std::max (1, n)),
      normalisedStorage (dynamic_cast<juce::AudioParameterChoice*> (&p) == nullptr)
{
    addItemList (items, 1);
    onChange = [this, &p]
    {
        const int idx = getSelectedItemIndex();
        if (idx < 0) return;
        const float denorm = normalisedStorage ? p.convertFrom0to1 (numChoices > 1 ? (float) idx / (float) (numChoices - 1) : 0.0f) : (float) idx;
        attachment.setValueAsCompleteGesture (denorm);
    };
    attachment.sendInitialUpdate();
    setTitle (p.getName (64));
}

//==============================================================================
void LevelMeter::update()
{
    const float v = source.exchange (0.0f);
    level = std::max (v, level * 0.82f);
    if (v >= peakHold) { peakHold = v; holdFrames = 30; }
    else if (--holdFrames < 0) peakHold *= 0.92f;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panelRaised);
    g.fillRoundedRectangle (r, 2.0f);
    auto toX = [&r] (float lin) { const float db = juce::Decibels::gainToDecibels (lin, -60.0f); return r.getWidth() * (db + 60.0f) / 60.0f; };
    const float w = toX (level);
    juce::ColourGradient grad (colours::teal, r.getX(), 0, colours::danger, r.getRight(), 0, false);
    grad.addColour (0.8, colours::amber);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r.withWidth (w), 2.0f);
    g.setColour (peakHold >= 0.99f ? colours::danger : colours::text);
    g.fillRect (r.getX() + toX (peakHold) - 1.0f, r.getY(), 2.0f, r.getHeight());
}

//==============================================================================
void fillEffectTypeCombo (juce::ComboBox& box)
{
    box.clear (juce::dontSendNotification);
    const auto& reg = EffectRegistry::get();
    box.addItem ("Empty", 1);
    for (auto& cat : reg.categories())
    {
        box.addSectionHeading (cat);
        for (auto& e : reg.all())
            if (e.index > 0 && e.category == cat) box.addItem (e.name, e.index + 1);
    }
}
} // namespace pf::ui
