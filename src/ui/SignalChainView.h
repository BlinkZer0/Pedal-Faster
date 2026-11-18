#pragma once
#include "Widgets.h"

namespace pf::ui
{
/** One effect slot shown as a pedal card. Drag the header to reorder; drop onto another card or row. */
class PedalCard : public juce::Component, public juce::DragAndDropTarget
{
public:
    PedalCard (EditorContext& ctx, int slot);
    void refresh();
    int getSlot() const noexcept { return slot; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("slot:"); }
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails& d) override;

    static void showSlotMenu (EditorContext& ctx, int slot, juce::Component* target);

private:
    void rebuild();
    EditorContext& ctx;
    const int slot;
    int shownType = -1;
    juce::ComboBox typeBox;
    std::unique_ptr<ParamToggle> power;
    juce::OwnedArray<Knob> knobs;
    juce::TextButton left { "<" }, right { ">" }, swap { "Pre/Post" }, menu { "..." };
    bool dropHover = false;
};

/** Amp or cabinet module card. */
class ModuleCard : public juce::Component
{
public:
    ModuleCard (EditorContext& ctx, bool isAmp);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();
private:
    EditorContext& ctx;
    const bool amp;
    std::unique_ptr<ParamToggle> power;
    std::unique_ptr<ParamCombo> type;
    juce::OwnedArray<Knob> knobs;
    juce::TextButton irButton { "Load IR..." };
    juce::Label irLabel;
    std::unique_ptr<juce::FileChooser> chooser;
};

class SignalChainView : public juce::Component, public juce::DragAndDropTarget
{
public:
    explicit SignalChainView (EditorContext& ctx);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("slot:"); }
    void itemDropped (const SourceDetails& d) override;

private:
    EditorContext& ctx;
    juce::OwnedArray<PedalCard> cards;
    ModuleCard ampCard, cabCard;
    juce::Rectangle<int> preArea, postArea;
    uint32_t shownRouting = 0;
};
} // namespace pf::ui
