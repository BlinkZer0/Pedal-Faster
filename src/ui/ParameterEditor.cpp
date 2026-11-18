#include "ParameterEditor.h"
#include "SignalChainView.h"
#include "../dsp/Cabinets/Cabinet.h"

namespace pf::ui
{
ParameterEditor::ParameterEditor (EditorContext& c) : ctx (c)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    build();
}

void ParameterEditor::showTarget (int t)
{
    if (t == target && builtType == (t < kNumSlots ? ctx.proc.slotType (t) : -1)) return;
    target = t;
    build();
}

void ParameterEditor::refresh()
{
    if (target >= 0 && target < kNumSlots && ctx.proc.slotType (target) != builtType) build();
}

void ParameterEditor::addKnob (juce::RangedAudioParameter& p, const juce::String& label, juce::Colour a)
{
    auto k = std::make_unique<Knob>();
    k->bind (p, label);
    k->setAccent (a);
    content.addAndMakeVisible (*k);
    items.push_back ({ std::move (k), nullptr, false });
}

void ParameterEditor::addSpecKnob (int slot, int k, const ParamSpec& spec, juce::Colour a)
{
    auto& p = *ctx.proc.param (slotParamId (slot, k));
    if (spec.isChoice())
    {
        juce::StringArray names; for (auto& n : spec.choices) names.add (n);
        addCombo (std::make_unique<ParamCombo> (p, names, names.size()), spec.name);
        return;
    }
    auto kn = std::make_unique<Knob>();
    kn->bindSpec (p, spec);
    kn->setAccent (a);
    content.addAndMakeVisible (*kn);
    items.push_back ({ std::move (kn), nullptr, false });
}

void ParameterEditor::addCombo (std::unique_ptr<juce::Component> c, const juce::String& label)
{
    auto l = std::make_unique<juce::Label>();
    l->setText (label, juce::dontSendNotification);
    styleCaption (*l, 12.0f);
    l->setJustificationType (juce::Justification::centredLeft);
    content.addAndMakeVisible (*c);
    content.addAndMakeVisible (*l);
    items.push_back ({ std::move (c), std::move (l), true });
}

void ParameterEditor::addToggle (juce::RangedAudioParameter& p, const juce::String& label)
{
    addCombo (std::make_unique<ParamToggle> (p, label), {});
}

void ParameterEditor::build()
{
    items.clear(); actions.clear(); slotTypeBox.reset();
    auto& proc = ctx.proc;
    auto P = [&proc] (const juce::String& id) -> juce::RangedAudioParameter& { return *proc.param (id); };
    auto action = [this] (const juce::String& text, std::function<void()> fn)
    {
        auto* b = actions.add (new juce::TextButton (text));
        b->onClick = std::move (fn);
        content.addAndMakeVisible (b);
    };

    if (target >= 0 && target < kNumSlots)
    {
        const int s = target;
        builtType = proc.slotType (s);
        const auto& info = EffectRegistry::get().at (builtType);
        accent = categoryColour (info.category);
        title = "Slot " + juce::String (s + 1) + ": " + (builtType > 0 ? juce::String (info.name) : juce::String ("Empty"));
        subtitle = builtType > 0 ? juce::String (info.category) + (proc.getRouting().isPre (s) ? "  |  pre-amp" : "  |  post-amp") : "Choose a pedal";

        slotTypeBox = std::make_unique<juce::ComboBox>();
        fillEffectTypeCombo (*slotTypeBox);
        slotTypeBox->setSelectedId (builtType + 1, juce::dontSendNotification);
        slotTypeBox->setTitle ("Pedal type");
        slotTypeBox->onChange = [this, s]
        {
            const int t = slotTypeBox->getSelectedId() - 1;
            juce::MessageManager::callAsync ([this, safe = juce::Component::SafePointer<ParameterEditor> (this), s, t]
            {
                if (safe == nullptr) return;
                ctx.proc.setSlotType (s, t);
                if (ctx.rigChanged) ctx.rigChanged();
                build();
            });
        };
        auto* raw = slotTypeBox.get();
        content.addAndMakeVisible (*raw);
        if (builtType > 0)
        {
            addToggle (P (slotOnId (s)), "Enabled");
            if (info.hasMix) addKnob (P (slotMixId (s)), "Mix", accent);
            for (size_t k = 0; k < info.params.size(); ++k) addSpecKnob (s, (int) k, info.params[k], accent);
        }
        action ("Reset", [this, s] { ctx.proc.resetSlot (s); });
        action ("Copy", [this, s] { ctx.clipboard = ctx.proc.captureSlot (s); });
        action ("Paste", [this, s] { if (ctx.clipboard) { ctx.proc.applySlot (s, *ctx.clipboard); if (ctx.rigChanged) ctx.rigChanged(); build(); } });
        action ("More...", [this, s] { if (actions.size() > 3) PedalCard::showSlotMenu (ctx, s, actions[3]); });
    }
    else if (target == kTargetAmp)
    {
        builtType = -1; accent = colours::amber;
        title = "Amplifier"; subtitle = "Original voicings - not hardware captures";
        juce::StringArray names; for (auto& n : ampTypeNames()) names.add (n);
        addCombo (std::make_unique<ParamCombo> (P (ids::ampType), names, names.size()), "Model");
        addToggle (P (ids::ampOn), "Amp enabled");
        addKnob (P (ids::ampGain), "Gain", accent); addKnob (P (ids::ampBass), "Bass", accent); addKnob (P (ids::ampMid), "Middle", accent);
        addKnob (P (ids::ampTreble), "Treble", accent); addKnob (P (ids::ampPresence), "Presence", accent); addKnob (P (ids::ampMaster), "Master", accent);
        addKnob (P (ids::ampSag), "Sag", accent); addKnob (P (ids::ampTrim), "Output Trim", accent);
    }
    else if (target == kTargetCab)
    {
        builtType = -1; accent = colours::teal;
        title = "Cabinet"; subtitle = proc.getImpulseResponseName().isEmpty() ? "Filter-based factory voicings" : "IR: " + proc.getImpulseResponseName();
        juce::StringArray names; for (auto& n : cabTypeNames()) names.add (n);
        addCombo (std::make_unique<ParamCombo> (P (ids::cabType), names, names.size()), "Cabinet");
        addToggle (P (ids::cabOn), "Cabinet enabled");
        addKnob (P (ids::cabMic), "Mic Position", accent); addKnob (P (ids::cabLowCut), "Low Cut", accent);
        addKnob (P (ids::cabHighCut), "High Cut", accent); addKnob (P (ids::cabLevel), "Level", accent); addKnob (P (ids::cabMix), "Wet/Dry", accent);
        action ("Load IR...", [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Load impulse response", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult() == juce::File()) return;
                auto r = ctx.proc.loadImpulseResponse (fc.getResult());
                if (r.failed()) juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Impulse response").withMessage (r.getErrorMessage()).withButton ("OK", 0), nullptr);
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ParameterEditor> (this)] { if (safe) safe->build(); });
            });
        });
        action ("Clear IR", [this] { ctx.proc.clearImpulseResponse(); ctx.proc.setParamReal (ids::cabType, (float) CabType::Closed4x12); build(); });
    }
    else
    {
        builtType = -1; accent = colours::teal;
        title = "Master"; subtitle = "Input, gate, EQ, output, tempo";
        addCombo (std::make_unique<ParamCombo> (P (ids::inMode), juce::StringArray { "Stereo", "Left (Mono)", "Right (Mono)", "Sum to Mono" }, 4), "Input");
        addKnob (P (ids::inGain), "Input Gain", accent);
        addToggle (P (ids::gateOn), "Noise gate");
        addKnob (P (ids::gateThresh), "Gate Threshold", accent); addKnob (P (ids::gateRelease), "Gate Release", accent);
        addToggle (P (ids::eqOn), "Master EQ");
        addKnob (P (ids::eqLow), "EQ Low", accent); addKnob (P (ids::eqMid), "EQ Mid", accent);
        addKnob (P (ids::eqMidFreq), "Mid Freq", accent); addKnob (P (ids::eqHigh), "EQ High", accent);
        addKnob (P (ids::outGain), "Preset Output", accent);
        addToggle (P (ids::limOn), "Output limiter");
        addKnob (P (ids::limCeiling), "Ceiling", accent); addKnob (P (ids::masterVol), "Master Volume", accent);
        addCombo (std::make_unique<ParamCombo> (P (ids::outMode), juce::StringArray { "Stereo", "Mono" }, 2), "Output");
        addCombo (std::make_unique<ParamCombo> (P (ids::quality), juce::StringArray { "Eco (no oversampling)", "Normal (2x)", "High (4x)" }, 3), "Quality");
        addToggle (P (ids::tempoHost), "Follow host tempo");
        addToggle (P (ids::tunerMute), "Mute output (silent tuning)");
        addKnob (P (ids::tempo), "Tempo", accent);
        for (int m = 0; m < 4; ++m) addKnob (P (ids::macro (m)), "Macro " + macroNames()[m], colours::amber);
    }
    resized();
    repaint();
}

void ParameterEditor::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    auto r = getLocalBounds().reduced (12, 8);
    g.setColour (accent);
    g.fillRect (r.getX(), r.getY() + 4, 3, 34);
    g.setFont (Theme::font (16.0f, true));
    g.setColour (colours::text);
    g.drawText (title, r.withHeight (22).withTrimmedLeft (10), juce::Justification::centredLeft, true);
    g.setFont (Theme::font (11.5f));
    g.setColour (colours::textDim);
    g.drawText (subtitle, r.withTrimmedTop (22).withHeight (16).withTrimmedLeft (10), juce::Justification::centredLeft, true);
}

void ParameterEditor::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (50).reduced (6, 4));
    const int w = viewport.getWidth() - viewport.getScrollBarThickness();
    int y = 4;
    if (slotTypeBox) { slotTypeBox->setBounds (4, y, w - 8, 26); y += 34; }
    const int cols = std::max (2, w / 96);
    const int cw = w / cols, kh = 92;
    int col = 0;
    for (auto& it : items)
    {
        if (it.wide)
        {
            if (col != 0) { y += kh; col = 0; }
            if (it.label) { it.label->setBounds (6, y, 100, 26); it.comp->setBounds (106, y, w - 112, 26); }
            else it.comp->setBounds (6, y, w - 12, 26);
            y += 32;
        }
        else
        {
            it.comp->setBounds (col * cw + 2, y, cw - 4, kh - 4);
            if (++col >= cols) { col = 0; y += kh; }
        }
    }
    if (col != 0) y += kh;
    y += 6;
    if (! actions.isEmpty())
    {
        const int bw = (w - 8) / std::min (4, actions.size());
        int x = 4, n = 0;
        for (auto* b : actions)
        {
            b->setBounds (x, y, bw - 4, 26);
            x += bw;
            if (++n % 4 == 0) { x = 4; y += 30; }
        }
        y += 34;
    }
    content.setSize (w, y);
}
} // namespace pf::ui
