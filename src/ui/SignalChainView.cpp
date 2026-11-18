#include "SignalChainView.h"
#include "../dsp/Cabinets/Cabinet.h"

namespace pf::ui
{
namespace
{
juce::String pedalFileJson (const EffectState& e)
{
    Preset p; p.name = "Pedal"; p.id = "pedal"; p.category = "Pedal";
    p.effects.push_back (e);
    return PresetSerializer::toJson (p);
}
} // namespace

//==============================================================================
PedalCard::PedalCard (EditorContext& c, int s) : ctx (c), slot (s)
{
    fillEffectTypeCombo (typeBox);
    typeBox.setTitle ("Slot " + juce::String (slot + 1) + " pedal type");
    typeBox.onChange = [this]
    {
        const int type = typeBox.getSelectedId() - 1;
        if (type >= 0 && type != ctx.proc.slotType (slot))
        {
            ctx.proc.setSlotType (slot, type);
            if (ctx.select) ctx.select (slot);
            if (ctx.rigChanged) ctx.rigChanged();
        }
    };
    addAndMakeVisible (typeBox);
    for (auto* b : { &left, &right, &swap, &menu }) addAndMakeVisible (b);
    left.setTooltip ("Move earlier in the chain"); right.setTooltip ("Move later in the chain");
    swap.setTooltip ("Move between pre-amp and post-amp"); menu.setTooltip ("Reset, copy, paste, save, load");
    left.setTitle ("Move slot " + juce::String (slot + 1) + " left"); right.setTitle ("Move slot " + juce::String (slot + 1) + " right");
    auto move = [this] (int delta)
    {
        auto r = ctx.proc.getRouting();
        const int pos = r.positionOf (slot);
        const bool pre = pos < r.numPre;
        const int inRow = pre ? pos : pos - r.numPre;
        ctx.proc.moveSlot (slot, std::max (0, inRow + delta), pre);
        if (ctx.rigChanged) ctx.rigChanged();
    };
    left.onClick = [move] { move (-1); };
    right.onClick = [move] { move (1); };
    swap.onClick = [this]
    {
        auto r = ctx.proc.getRouting();
        const bool pre = r.isPre (slot);
        ctx.proc.moveSlot (slot, pre ? 0 : 99, ! pre);
        if (ctx.rigChanged) ctx.rigChanged();
    };
    menu.onClick = [this] { showSlotMenu (ctx, slot, &menu); };
    power = std::make_unique<ParamToggle> (*ctx.proc.param (slotOnId (slot)), "ON");
    addAndMakeVisible (*power);
    rebuild();
}

void PedalCard::showSlotMenu (EditorContext& ctx, int slot, juce::Component* target)
{
    juce::PopupMenu m;
    const bool empty = ctx.proc.slotType (slot) <= 0;
    m.addItem (1, "Reset to defaults", ! empty);
    m.addItem (2, "Copy settings", ! empty);
    m.addItem (3, "Paste settings", ctx.clipboard.has_value());
    m.addSeparator();
    m.addItem (4, "Save pedal settings...", ! empty);
    m.addItem (5, "Load pedal settings...");
    m.addSeparator();
    m.addItem (6, "Remove pedal", ! empty);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [&ctx, slot] (int r)
    {
        auto& proc = ctx.proc;
        if (r == 1) proc.resetSlot (slot);
        else if (r == 2) ctx.clipboard = proc.captureSlot (slot);
        else if (r == 3 && ctx.clipboard) proc.applySlot (slot, *ctx.clipboard);
        else if (r == 6) proc.setSlotType (slot, 0);
        else if (r == 4 || r == 5)
        {
            auto chooser = std::make_shared<juce::FileChooser> (r == 4 ? "Save pedal settings" : "Load pedal settings",
                                                                 PresetManager::defaultRootDirectory(), "*.pfpedal");
            const int flags = r == 4 ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                                     : juce::FileBrowserComponent::openMode;
            chooser->launchAsync (flags | juce::FileBrowserComponent::canSelectFiles, [chooser, &ctx, slot, r] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                if (r == 4) f.withFileExtension ("pfpedal").replaceWithText (pedalFileJson (ctx.proc.captureSlot (slot)));
                else
                {
                    Preset p;
                    auto res = PresetSerializer::fromJson (f.loadFileAsString(), p);
                    if (res.ok && ! p.effects.empty()) ctx.proc.applySlot (slot, p.effects.front());
                    else juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Load pedal").withMessage ("That file is not a valid pedal file.").withButton ("OK", 0), nullptr);
                }
                if (ctx.rigChanged) ctx.rigChanged();
            });
            return;
        }
        if (r > 0 && ctx.rigChanged) ctx.rigChanged();
    });
}

void PedalCard::rebuild()
{
    knobs.clear();
    shownType = ctx.proc.slotType (slot);
    typeBox.setSelectedId (shownType + 1, juce::dontSendNotification);
    const auto& info = EffectRegistry::get().at (shownType);
    const int maxKnobs = getWidth() > 0 && getWidth() < 130 ? 2 : 3;
    for (int k = 0; k < (int) info.params.size() && knobs.size() < maxKnobs; ++k)
    {
        if (info.params[(size_t) k].isChoice()) continue;
        auto* kn = knobs.add (new Knob (true));
        kn->bindSpec (*ctx.proc.param (slotParamId (slot, k)), info.params[(size_t) k]);
        kn->setAccent (categoryColour (info.category));
        addAndMakeVisible (kn);
    }
    if (info.hasMix && knobs.size() < maxKnobs)
    {
        auto* kn = knobs.add (new Knob (true));
        kn->bind (*ctx.proc.param (slotMixId (slot)), "Mix");
        kn->setAccent (categoryColour (info.category));
        addAndMakeVisible (kn);
    }
    power->setVisible (shownType > 0);
    resized();
    repaint();
}

void PedalCard::refresh()
{
    if (ctx.proc.slotType (slot) != shownType) rebuild();
    repaint();
}

void PedalCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto& info = EffectRegistry::get().at (shownType);
    const bool selected = ctx.selected == slot;
    const bool enabled = ctx.proc.getParamReal (slotOnId (slot)) > 0.5f;
    g.setColour (shownType > 0 ? colours::panelRaised : colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    if (shownType > 0)
    {
        auto strip = r.withHeight (5.0f);
        g.setColour (categoryColour (info.category).withAlpha (enabled ? 1.0f : 0.3f));
        g.fillRoundedRectangle (strip, 3.0f);
    }
    else
    {
        juce::Path p; p.addRoundedRectangle (r.reduced (2.0f), 6.0f);
        float dashes[] { 5.0f, 4.0f };
        juce::Path dashed; juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (colours::outline.brighter (0.2f)); g.fillPath (dashed);
    }
    g.setColour (dropHover ? colours::amber : selected ? colours::teal : colours::outline);
    g.drawRoundedRectangle (r, 6.0f, selected || dropHover ? 2.0f : 1.0f);

    auto header = getLocalBounds().reduced (6, 0).withTrimmedTop (7).withHeight (16);
    g.setFont (Theme::font (10.5f));
    g.setColour (colours::textDim);
    g.drawText (juce::String (slot + 1) + (shownType > 0 ? "  " + juce::String (info.category).toUpperCase() : juce::String ("  EMPTY")),
                header, juce::Justification::centredLeft, true);
    // drag grip
    g.setColour (colours::outline.brighter (0.3f));
    for (int i = 0; i < 3; ++i) g.fillRect (getWidth() / 2 - 8 + i * 6, 3 + 4, 3, 3);
    if (shownType > 0 && ! enabled)
    {
        g.setColour (colours::background.withAlpha (0.35f));
        g.fillRoundedRectangle (r.withTrimmedTop (48), 6.0f);
    }
}

void PedalCard::resized()
{
    auto r = getLocalBounds().reduced (6, 6);
    auto header = r.removeFromTop (18);
    power->setBounds (header.removeFromRight (34).reduced (0, 1));
    r.removeFromTop (3);
    typeBox.setBounds (r.removeFromTop (22));
    auto buttons = r.removeFromBottom (20);
    const int bw = buttons.getWidth() / 5;
    left.setBounds (buttons.removeFromLeft (bw).reduced (1, 0));
    right.setBounds (buttons.removeFromLeft (bw).reduced (1, 0));
    menu.setBounds (buttons.removeFromRight (bw).reduced (1, 0));
    swap.setBounds (buttons.reduced (1, 0));
    r.removeFromBottom (2);
    if (knobs.size() > 0)
    {
        const int kw = r.getWidth() / knobs.size();
        for (auto* k : knobs) k->setBounds (r.removeFromLeft (kw));
    }
}

void PedalCard::mouseDown (const juce::MouseEvent&) { if (ctx.select) ctx.select (slot); }

void PedalCard::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 6 || e.getMouseDownY() > 30) return;
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! dnd->isDragAndDropActive())
            dnd->startDragging ("slot:" + juce::String (slot), this, juce::ScaledImage (createComponentSnapshot (getLocalBounds()).rescaled (getWidth() / 2, getHeight() / 2)));
}

void PedalCard::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    const int src = d.description.toString().fromFirstOccurrenceOf ("slot:", false, false).getIntValue();
    if (src == slot) { repaint(); return; }
    auto r = ctx.proc.getRouting();
    const int pos = r.positionOf (slot);
    const bool pre = pos < r.numPre;
    const int inRow = pre ? pos : pos - r.numPre; // insert the dragged pedal at this card's position
    ctx.proc.moveSlot (src, inRow, pre);
    if (ctx.rigChanged) ctx.rigChanged();
}

//==============================================================================
ModuleCard::ModuleCard (EditorContext& c, bool isAmp) : ctx (c), amp (isAmp)
{
    auto& p = ctx.proc;
    if (amp)
    {
        power = std::make_unique<ParamToggle> (*p.param (ids::ampOn), "ON");
        juce::StringArray names; for (auto& n : ampTypeNames()) names.add (n);
        type = std::make_unique<ParamCombo> (*p.param (ids::ampType), names, names.size());
        const char* idsList[] { ids::ampGain, ids::ampBass, ids::ampMid, ids::ampTreble, ids::ampPresence, ids::ampMaster };
        const char* labels[] { "Gain", "Bass", "Mid", "Treble", "Presence", "Master" };
        for (int i = 0; i < 6; ++i)
        {
            auto* k = knobs.add (new Knob (true));
            k->bind (*p.param (idsList[i]), labels[i]);
            k->setAccent (colours::amber);
            addAndMakeVisible (k);
        }
    }
    else
    {
        power = std::make_unique<ParamToggle> (*p.param (ids::cabOn), "ON");
        juce::StringArray names; for (auto& n : cabTypeNames()) names.add (n);
        type = std::make_unique<ParamCombo> (*p.param (ids::cabType), names, names.size());
        const char* idsList[] { ids::cabMic, ids::cabLowCut, ids::cabHighCut, ids::cabLevel };
        const char* labels[] { "Mic", "Low Cut", "High Cut", "Level" };
        for (int i = 0; i < 4; ++i)
        {
            auto* k = knobs.add (new Knob (true));
            k->bind (*p.param (idsList[i]), labels[i]);
            k->setAccent (colours::teal);
            addAndMakeVisible (k);
        }
        irButton.setTooltip ("Load a WAV/AIFF impulse response (truncated to 2 s)");
        irButton.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Load impulse response", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                auto res = ctx.proc.loadImpulseResponse (f);
                if (res.failed()) juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Impulse response").withMessage (res.getErrorMessage()).withButton ("OK", 0), nullptr);
                refresh();
            });
        };
        addAndMakeVisible (irButton);
        styleCaption (irLabel, 10.5f);
        addAndMakeVisible (irLabel);
    }
    type->setTitle (amp ? "Amplifier model" : "Cabinet");
    addAndMakeVisible (*power);
    addAndMakeVisible (*type);
    refresh();
}

void ModuleCard::refresh()
{
    if (! amp) irLabel.setText (ctx.proc.getImpulseResponseName().isEmpty() ? "No IR loaded" : ctx.proc.getImpulseResponseName(), juce::dontSendNotification);
    repaint();
}

void ModuleCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto accent = amp ? colours::amber : colours::teal;
    juce::ColourGradient grad (colours::panelRaised.brighter (0.06f), 0, 0, colours::panelRaised.darker (0.2f), 0, r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (ctx.selected == (amp ? kTargetAmp : kTargetCab) ? accent : accent.withAlpha (0.35f));
    g.drawRoundedRectangle (r, 8.0f, ctx.selected == (amp ? kTargetAmp : kTargetCab) ? 2.0f : 1.2f);
    g.setFont (Theme::font (13.0f, true));
    g.setColour (accent);
    g.drawText (amp ? "AMPLIFIER" : "CABINET", getLocalBounds().reduced (10, 6).removeFromTop (20), juce::Justification::centredLeft);
}

void ModuleCard::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto header = r.removeFromTop (22);
    power->setBounds (header.removeFromRight (40).reduced (0, 1));
    header.removeFromLeft (amp ? 90 : 76);
    type->setBounds (header.reduced (4, 0));
    r.removeFromTop (4);
    if (! amp)
    {
        auto bottom = r.removeFromBottom (20);
        irButton.setBounds (bottom.removeFromLeft (90));
        irLabel.setBounds (bottom);
    }
    const int kw = r.getWidth() / std::max (1, knobs.size());
    for (auto* k : knobs) k->setBounds (r.removeFromLeft (kw));
}

void ModuleCard::mouseDown (const juce::MouseEvent&) { if (ctx.select) ctx.select (amp ? kTargetAmp : kTargetCab); }

//==============================================================================
SignalChainView::SignalChainView (EditorContext& c) : ctx (c), ampCard (c, true), cabCard (c, false)
{
    for (int s = 0; s < kNumSlots; ++s) addAndMakeVisible (cards.add (new PedalCard (ctx, s)));
    addAndMakeVisible (ampCard);
    addAndMakeVisible (cabCard);
}

void SignalChainView::refresh()
{
    const auto r = ctx.proc.getRouting().pack();
    if (r != shownRouting) { shownRouting = r; resized(); }
    for (auto* c : cards) c->refresh();
    ampCard.refresh(); cabCard.refresh();
}

void SignalChainView::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setFont (Theme::font (11.0f, true));
    g.setColour (colours::textDim);
    g.drawText ("INPUT  >  PRE-AMP", preArea.withHeight (16).translated (0, -18), juce::Justification::centredLeft);
    g.drawText ("POST-AMP  >  OUTPUT", postArea.withHeight (16).translated (0, -18), juce::Justification::centredLeft);
    // signal flow line
    g.setColour (colours::outline);
    const int x = getWidth() / 2;
    g.drawLine ((float) x, (float) preArea.getBottom(), (float) x, (float) ampCard.getY(), 2.0f);
    g.drawLine ((float) x, (float) ampCard.getBottom(), (float) x, (float) postArea.getY() - 18.0f, 2.0f);
}

void SignalChainView::resized()
{
    auto area = getLocalBounds().reduced (10);
    const auto routing = ctx.proc.getRouting();
    const int nPre = routing.numPre, nPost = kNumSlots - routing.numPre;
    const int moduleH = 150;
    const int rowsH = area.getHeight() - moduleH - 2 * 22 - 16;
    const int cardMinW = 120;
    auto linesFor = [&] (int n) { const int per = std::max (1, area.getWidth() / cardMinW); return std::max (1, (n + per - 1) / per); };
    const int preLines = linesFor (nPre), postLines = linesFor (nPost);
    const int lineH = juce::jlimit (110, 160, rowsH / std::max (1, preLines + postLines));

    area.removeFromTop (20);
    preArea = area.removeFromTop (lineH * preLines);
    area.removeFromTop (8);
    auto modules = area.removeFromTop (moduleH);
    area.removeFromTop (28);
    postArea = area.removeFromTop (lineH * postLines);

    ampCard.setBounds (modules.removeFromLeft (modules.getWidth() * 3 / 5).reduced (2));
    cabCard.setBounds (modules.reduced (2));

    auto place = [&] (juce::Rectangle<int> rowArea, int from, int count, int lines)
    {
        if (count == 0) return;
        const int perLine = (count + lines - 1) / lines;
        const int w = std::min (190, rowArea.getWidth() / perLine);
        for (int i = 0; i < count; ++i)
        {
            const int line = i / perLine, col = i % perLine;
            cards[routing.order[(size_t) (from + i)]]->setBounds (rowArea.getX() + col * w, rowArea.getY() + line * lineH, w - 6, lineH - 6);
        }
    };
    place (preArea, 0, nPre, preLines);
    place (postArea, nPre, nPost, postLines);
    repaint();
}

void SignalChainView::itemDropped (const SourceDetails& d)
{
    const int src = d.description.toString().fromFirstOccurrenceOf ("slot:", false, false).getIntValue();
    const auto p = d.localPosition.toInt();
    if (preArea.expanded (0, 20).contains (p)) ctx.proc.moveSlot (src, 99, true);
    else if (postArea.expanded (0, 20).contains (p) || p.y > cabCard.getBottom()) ctx.proc.moveSlot (src, 99, false);
    else if (p.y < ampCard.getY()) ctx.proc.moveSlot (src, 99, true);
    else return;
    if (ctx.rigChanged) ctx.rigChanged();
}
} // namespace pf::ui
