#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace pf::ui
{
namespace colours
{
    const juce::Colour background  { 0xff111316 };
    const juce::Colour panel       { 0xff191c20 };
    const juce::Colour panelRaised { 0xff21252a };
    const juce::Colour outline     { 0xff2e3339 };
    const juce::Colour text        { 0xffe4e7ea };
    const juce::Colour textDim     { 0xff8b939c };
    const juce::Colour teal        { 0xff2bbfb0 };
    const juce::Colour amber       { 0xfff0a23b };
    const juce::Colour danger      { 0xffe0605a };
    const juce::Colour good        { 0xff5ccf7a };
}

juce::Colour categoryColour (const juce::String& category);

class Theme : public juce::LookAndFeel_V4
{
public:
    Theme();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    static juce::Font font (float height, bool bold = false);
};
} // namespace pf::ui
