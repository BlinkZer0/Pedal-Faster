#include "PresetBrowser.h"

namespace pf::ui
{
PresetBrowser::PresetBrowser (EditorContext& c) : ctx (c)
{
    source.addItemList ({ "All presets", "Factory", "User", "Favourites" }, 1);
    source.setSelectedItemIndex (0, juce::dontSendNotification);
    source.onChange = [this] { refresh(); };
    source.setTitle ("Preset source");
    sort.addItemList ({ "Sort: Bank", "Sort: Name", "Sort: Category", "Sort: Gain" }, 1);
    sort.setSelectedItemIndex (0, juce::dontSendNotification);
    sort.onChange = [this] { refresh(); };
    sort.setTitle ("Sort presets");
    addAndMakeVisible (source); addAndMakeVisible (sort);
    styleCaption (countLabel, 11.0f);
    countLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (countLabel);
    listBox.setRowHeight (56);
    listBox.setColour (juce::ListBox::backgroundColourId, colours::panel);
    listBox.setTitle ("Preset list");
    addAndMakeVisible (listBox);
    ctx.proc.presets().addChangeListener (this);
    refresh();
}

PresetBrowser::~PresetBrowser() { ctx.proc.presets().removeChangeListener (this); }

void PresetBrowser::setFilter (const juce::String& s, const juce::String& c)
{
    search = s; category = c;
    refresh();
}

void PresetBrowser::refresh()
{
    list = ctx.proc.presets().query (search, category, (PresetManager::Source) std::max (0, source.getSelectedItemIndex()),
                                     (PresetManager::Sort) std::max (0, sort.getSelectedItemIndex()));
    countLabel.setText (juce::String ((int) list.size()) + " presets", juce::dontSendNotification);
    listBox.updateContent();
    showCurrent();
    listBox.repaint();
}

void PresetBrowser::showCurrent()
{
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i]->id == ctx.proc.currentPresetId)
        {
            listBox.selectRow ((int) i, false, true);
            return;
        }
    listBox.deselectAllRows();
}

juce::String PresetBrowser::getNameForRow (int row)
{
    if (row < 0 || row >= (int) list.size()) return {};
    return juce::String (list[(size_t) row]->name) + ", " + list[(size_t) row]->category;
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) list.size()) return;
    const auto& p = *list[(size_t) row];
    const bool current = p.id == ctx.proc.currentPresetId;
    auto r = juce::Rectangle<int> (0, 0, w, h).reduced (4, 2);
    g.setColour (selected || current ? colours::panelRaised.brighter (0.08f) : (row % 2 ? colours::panel : colours::panel.brighter (0.02f)));
    g.fillRoundedRectangle (r.toFloat(), 4.0f);
    if (current) { g.setColour (colours::teal); g.fillRect (r.removeFromLeft (3)); }
    r.reduce (8, 3);

    auto top = r.removeFromTop (18);
    const bool fav = ctx.proc.presets().isFavourite (p.id);
    g.setFont (Theme::font (11.0f));
    g.setColour (p.factory ? colours::textDim : colours::amber);
    auto catArea = top.removeFromRight (std::min (130, w / 2));
    g.drawText ((p.factory ? "" : "USER  ") + juce::String (p.category), catArea, juce::Justification::centredRight, true);
    g.setFont (Theme::font (14.0f, true));
    g.setColour (colours::text);
    g.drawText ((fav ? juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85 ")) : juce::String()) + p.name, top, juce::Justification::centredLeft, true);
    g.setFont (Theme::font (11.5f));
    g.setColour (colours::textDim);
    g.drawText (p.description, r.removeFromTop (16), juce::Justification::centredLeft, true);
    juce::String tags;
    for (auto& t : p.tags) tags << "#" << t << "  ";
    g.setColour (colours::teal.withAlpha (0.75f));
    g.setFont (Theme::font (10.5f));
    g.drawText (tags + " gain " + juce::String (p.gainLevel()), r, juce::Justification::centredLeft, true);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) list.size()) return;
    ctx.proc.applyPreset (*list[(size_t) row]);
    if (ctx.rigChanged) ctx.rigChanged();
}

void PresetBrowser::returnKeyPressed (int row)
{
    if (row < 0 || row >= (int) list.size()) return;
    ctx.proc.applyPreset (*list[(size_t) row]);
    if (ctx.rigChanged) ctx.rigChanged();
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

void PresetBrowser::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto row = r.removeFromTop (26);
    source.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
    row.removeFromLeft (6);
    sort.setBounds (row);
    r.removeFromTop (4);
    countLabel.setBounds (r.removeFromTop (16));
    listBox.setBounds (r);
}
} // namespace pf::ui
