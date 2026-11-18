#include "Theme.h"

namespace pf::ui
{
juce::Colour categoryColour (const juce::String& c)
{
    if (c == "Dynamics")     return juce::Colour (0xff4fa3d9);
    if (c == "Gain")         return juce::Colour (0xffe8743b);
    if (c == "Experimental") return juce::Colour (0xffd65db1);
    if (c == "Filter")       return juce::Colour (0xffa47de0);
    if (c == "EQ")           return juce::Colour (0xff9aa7b4);
    if (c == "Modulation")   return juce::Colour (0xff4f7fe0);
    if (c == "Pitch")        return juce::Colour (0xffe06b8f);
    if (c == "Delay")        return juce::Colour (0xff5cbf6e);
    if (c == "Reverb")       return juce::Colour (0xff3fc6d0);
    return colours::outline;
}

juce::Font Theme::font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

Theme::Theme()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, teal);
    setColour (juce::Slider::rotarySliderOutlineColourId, outline);
    setColour (juce::TextButton::buttonColourId, panelRaised);
    setColour (juce::TextButton::buttonOnColourId, teal.withAlpha (0.85f));
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, background);
    setColour (juce::ComboBox::backgroundColourId, panelRaised);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panelRaised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, amber);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, teal.withAlpha (0.3f));
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::TextEditor::backgroundColourId, panelRaised);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, outline);
    setColour (juce::TextEditor::focusedOutlineColourId, teal);
    setColour (juce::TextEditor::highlightColourId, teal.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, teal);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ListBox::outlineColourId, outline);
    setColour (juce::ScrollBar::thumbColourId, outline.brighter (0.2f));
    setColour (juce::AlertWindow::backgroundColourId, panelRaised);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, outline);
    setColour (juce::TooltipWindow::backgroundColourId, panelRaised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, outline);
}

void Theme::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float r = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);

    // knob body
    g.setColour (colours::panelRaised.brighter (0.05f));
    g.fillEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f);
    g.setColour (colours::outline.brighter (0.15f));
    g.drawEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f, 1.0f);

    // track + value arc
    juce::Path track; track.addCentredArc (c.x, c.y, r * 0.9f, r * 0.9f, 0, start, end, true);
    g.setColour (colours::outline);
    g.strokePath (track, juce::PathStrokeType (r * 0.12f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc (c.x, c.y, r * 0.9f, r * 0.9f, 0, start, angle, true);
    g.setColour (s.isEnabled() ? accent : colours::textDim);
    g.strokePath (val, juce::PathStrokeType (r * 0.12f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // pointer
    juce::Path p; p.addRoundedRectangle (-r * 0.05f, -r * 0.68f, r * 0.1f, r * 0.36f, r * 0.05f);
    g.setColour (colours::text);
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void Theme::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto col = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
    if (down) col = col.brighter (0.15f); else if (over) col = col.brighter (0.07f);
    g.setColour (col);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (b.hasKeyboardFocus (false) ? colours::teal : colours::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void Theme::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1);
}

void Theme::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (true) ? colours::teal : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font Theme::getComboBoxFont (juce::ComboBox& b) { return font (juce::jlimit (11.0f, 15.0f, (float) b.getHeight() * 0.52f)); }
juce::Font Theme::getTextButtonFont (juce::TextButton&, int h) { return font (juce::jlimit (10.5f, 15.0f, (float) h * 0.5f)); }
juce::Font Theme::getLabelFont (juce::Label& l) { return l.getFont(); }

void Theme::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void Theme::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (colours::panelRaised);
    g.setColour (colours::outline);
    g.drawRect (0, 0, w, h);
}

void Theme::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& te)
{
    g.setColour (te.hasKeyboardFocus (true) ? colours::teal : colours::outline);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 4.0f, 1.0f);
}

void Theme::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (colours::panelRaised);
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 4.0f);
}
} // namespace pf::ui

namespace pf::ui
{
juce::Label* Theme::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    const bool compact = (bool) s.getProperties().getWithDefault ("compact", false);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::textColourId, colours::text);
    l->setColour (juce::Label::outlineWhenEditingColourId, colours::teal);
    l->setColour (juce::Label::backgroundWhenEditingColourId, colours::panelRaised);
    l->setFont (font (compact ? 9.5f : 12.0f));
    l->setMinimumHorizontalScale (0.7f);
    return l;
}
} // namespace pf::ui
