#pragma once
#include "Widgets.h"

namespace pf::ui
{
/** Left panel: source/sort filters and a searchable preset list. Search text and category come from the top bar. */
class PresetBrowser : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener
{
public:
    explicit PresetBrowser (EditorContext& ctx);
    ~PresetBrowser() override;

    void setFilter (const juce::String& search, const juce::String& category);
    void refresh();                                   // re-run the query
    void showCurrent();                               // scroll to / highlight the loaded preset
    const std::vector<const Preset*>& visible() const { return list; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override { return (int) list.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    juce::String getNameForRow (int row) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    EditorContext& ctx;
    juce::ComboBox source, sort;
    juce::Label countLabel;
    juce::ListBox listBox { "Presets", this };
    std::vector<const Preset*> list;
    juce::String search, category;
};
} // namespace pf::ui
