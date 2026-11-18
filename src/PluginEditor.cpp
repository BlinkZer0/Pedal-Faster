#include "PluginEditor.h"

namespace pf
{
using namespace ui;

PedalFasterEditor::PedalFasterEditor (PedalFasterProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      ctx { p },
      inMeter (p.getEngine().inPeak), outMeter (p.getEngine().outPeak),
      browser (ctx), chain (ctx), editorPanel (ctx)
{
    setLookAndFeel (&theme);
    ctx.select = [this] (int t) { select (t); };
    ctx.rigChanged = [this] { refreshAll(); };

    juce::PropertiesFile::Options opts;
    opts.applicationName = "settings"; opts.filenameSuffix = ".xml"; opts.folderName = "Pedal Faster";
    opts.osxLibrarySubFolder = "Application Support"; opts.storageFormat = juce::PropertiesFile::storeAsXML;
    settings = std::make_unique<juce::PropertiesFile> (opts);

    // ---- top bar
    presetButton.setTooltip ("Choose a preset");
    presetButton.onClick = [this] { showPresetMenu(); };
    prevButton.onClick = [this] { prevNext (-1); };
    nextButton.onClick = [this] { prevNext (1); };
    prevButton.setTooltip ("Previous preset (Left arrow)"); nextButton.setTooltip ("Next preset (Right arrow)");
    saveButton.onClick = [this] { savePreset (false); };
    saveButton.setTooltip ("Save (factory presets are saved as a new user preset)");
    fileButton.onClick = [this] { showFileMenu(); };
    fileButton.setTooltip ("Save As, rename, duplicate, delete, import / export");
    favButton.setClickingTogglesState (false);
    favButton.setTooltip ("Toggle favourite");
    favButton.onClick = [this]
    {
        auto& pm = proc.presets();
        pm.setFavourite (proc.currentPresetId, ! pm.isFavourite (proc.currentPresetId));
        updatePresetLabel();
    };
    tunerButton.setClickingTogglesState (true);
    tunerButton.onClick = [this] { tunerVisible = tunerButton.getToggleState(); repaint(); };
    masterButton.onClick = [this] { select (kTargetMaster); };
    searchBox.setTextToShowWhenEmpty ("Search presets, tags...", colours::textDim);
    searchBox.onTextChange = [this] { browser.setFilter (searchBox.getText(), categoryBox.getSelectedId() > 1 ? categoryBox.getText() : juce::String()); };
    searchBox.setTitle ("Preset search");
    categoryBox.addItem ("All categories", 1);
    {
        int id = 2;
        for (auto& c : proc.presets().categories()) categoryBox.addItem (c, id++);
    }
    categoryBox.setSelectedId (1, juce::dontSendNotification);
    categoryBox.onChange = [this] { browser.setFilter (searchBox.getText(), categoryBox.getSelectedId() > 1 ? categoryBox.getText() : juce::String()); };
    categoryBox.setTitle ("Category filter");
    bypassButton = std::make_unique<ParamToggle> (*proc.param (ids::bypass), "BYPASS");
    bypassButton->setColour (juce::TextButton::buttonOnColourId, colours::danger);
    qualityBox = std::make_unique<ParamCombo> (*proc.param (ids::quality), juce::StringArray { "Eco", "Normal", "High" }, 3);
    qualityBox->setTooltip ("Oversampling quality: Eco 1x, Normal 2x, High 4x (adds latency)");
    for (auto* l : { &cpuLabel, &inLabel, &outLabel }) styleCaption (*l, 10.5f);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &presetButton, &prevButton, &nextButton, &saveButton, &fileButton, &favButton,
                                                                         &tunerButton, &masterButton, &searchBox, &categoryBox, bypassButton.get(),
                                                                         qualityBox.get(), &inMeter, &outMeter, &cpuLabel, &inLabel, &outLabel })
        addAndMakeVisible (c);

    addAndMakeVisible (browser);
    addAndMakeVisible (chain);
    addAndMakeVisible (editorPanel);

    // ---- bottom bar
    auto knob = [this] (const char* id, const juce::String& label, juce::Colour c)
    {
        auto* k = bottomKnobs.add (new Knob (true));
        k->bind (*proc.param (id), label); k->setAccent (c);
        addAndMakeVisible (k);
    };
    knob (ids::inGain, "Input", colours::teal);
    gateButton = std::make_unique<ParamToggle> (*proc.param (ids::gateOn), "GATE");
    addAndMakeVisible (*gateButton);
    knob (ids::gateThresh, "Gate", colours::teal);
    knob (ids::eqLow, "Low", colours::teal); knob (ids::eqMid, "Mid", colours::teal); knob (ids::eqHigh, "High", colours::teal);
    for (int m = 0; m < 4; ++m) knob (ids::macro (m), macroNames()[m], colours::amber);
    knob (ids::outGain, "Level", colours::teal);
    knob (ids::masterVol, "Master", colours::teal);
    tapButton.onClick = [this] { proc.tapTempo(); };
    tapButton.setTooltip ("Tap tempo (T)");
    styleCaption (bpmLabel, 12.0f, colours::text);
    outModeBox = std::make_unique<ParamCombo> (*proc.param (ids::outMode), juce::StringArray { "Stereo", "Mono" }, 2);
    limiterButton = std::make_unique<ParamToggle> (*proc.param (ids::limOn), "LIMITER");
    styleCaption (limiterLabel, 10.0f);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &tapButton, &bpmLabel, outModeBox.get(), limiterButton.get(), &limiterLed, &limiterLabel })
        addAndMakeVisible (c);

    setResizable (true, true);
    setResizeLimits (1100, 700, 2600, 1600);
    if (auto* c = getConstrainer()) c->setMinimumOnscreenAmounts (100, 100, 100, 100);
    setSize (settings->getIntValue ("width", 1400), settings->getIntValue ("height", 860));
    setWantsKeyboardFocus (true);

    select (kTargetAmp);
    refreshAll();
    startTimerHz (30);
}

PedalFasterEditor::~PedalFasterEditor()
{
    stopTimer();
    settings->setValue ("width", getWidth());
    settings->setValue ("height", getHeight());
    settings->saveIfNeeded();
    setLookAndFeel (nullptr);
}

void PedalFasterEditor::select (int target)
{
    ctx.selected = target;
    editorPanel.showTarget (target);
    chain.repaint();
    for (auto* c : chain.getChildren()) c->repaint();
}

void PedalFasterEditor::refreshAll()
{
    chain.refresh();
    editorPanel.refresh();
    browser.refresh();
    updatePresetLabel();
}

void PedalFasterEditor::updatePresetLabel()
{
    presetButton.setButtonText (proc.currentPresetName + (proc.isDirty() ? "  *" : ""));
    const bool fav = proc.presets().isFavourite (proc.currentPresetId);
    favButton.setButtonText (fav ? juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x86")));
    favButton.setColour (juce::TextButton::textColourOffId, fav ? colours::amber : colours::textDim);
    lastDirty = proc.isDirty();
}

void PedalFasterEditor::timerCallback()
{
    inMeter.update(); outMeter.update();
    cpuLabel.setText ("CPU " + juce::String (juce::roundToInt (proc.cpuLoad() * 100.0f)) + "%", juce::dontSendNotification);
    bpmLabel.setText (juce::String (proc.currentBpm(), 1) + " BPM", juce::dontSendNotification);
    limiterLed.setOn (proc.getEngine().limiterGrDb.load() > 0.5f);

    if (proc.stateVersion.load() != lastStateVersion)
    {
        lastStateVersion = proc.stateVersion.load();
        select (ctx.selected);
        refreshAll();
    }
    else
    {
        chain.refresh();
        editorPanel.refresh();
        if (proc.isDirty() != lastDirty) updatePresetLabel();
    }

    if (tunerVisible)
    {
        proc.updateTuner();
        repaint (tunerArea);
    }
}

void PedalFasterEditor::prevNext (int delta)
{
    proc.stepPreset (delta, browser.visible().empty() ? proc.presets().query ({}, {}, PresetManager::Source::All, PresetManager::Sort::Bank)
                                                      : browser.visible());
    refreshAll();
}

void PedalFasterEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    auto& pm = proc.presets();
    std::vector<const Preset*> flat;
    for (auto& cat : pm.categories())
    {
        juce::PopupMenu sub;
        for (auto* p : pm.query ({}, cat, PresetManager::Source::All, PresetManager::Sort::Bank))
        {
            flat.push_back (p);
            sub.addItem ((int) flat.size(), p->name, true, p->id == proc.currentPresetId);
        }
        if (sub.getNumItems() > 0) menu.addSubMenu (cat, sub);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton), [this, flat] (int r)
    {
        if (r > 0 && r <= (int) flat.size()) { proc.applyPreset (*flat[(size_t) r - 1]); refreshAll(); }
    });
}

void PedalFasterEditor::savePreset (bool saveAs)
{
    auto& pm = proc.presets();
    const bool isFactory = pm.isFactory (proc.currentPresetId) || pm.findById (proc.currentPresetId) == nullptr;
    if (! saveAs && ! isFactory)
    {
        std::string id;
        auto r = pm.saveUser (proc.captureState(), id);
        if (r.failed()) juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Save preset").withMessage (r.getErrorMessage()).withButton ("OK", 0), nullptr);
        else { proc.currentPresetId = id; proc.clearDirty(); }
        refreshAll();
        return;
    }
    auto* w = new juce::AlertWindow ("Save preset as", "Factory presets are read-only; this creates a user preset.", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", proc.currentPresetName + (isFactory ? " (User)" : " Copy"), "Name");
    w->addTextEditor ("category", "User", "Category");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result == 1)
        {
            auto p = proc.captureState();
            p.id.clear();
            p.name = w->getTextEditorContents ("name").trim().toStdString();
            p.category = w->getTextEditorContents ("category").trim().toStdString();
            std::string id;
            auto r = proc.presets().saveUser (p, id);
            if (r.failed()) juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Save preset").withMessage (r.getErrorMessage()).withButton ("OK", 0), nullptr);
            else { proc.currentPresetId = id; proc.currentPresetName = p.name; proc.clearDirty(); }
            refreshAll();
        }
    }), true);
}

void PedalFasterEditor::showFileMenu()
{
    auto& pm = proc.presets();
    const bool user = ! pm.isFactory (proc.currentPresetId) && pm.findById (proc.currentPresetId) != nullptr;
    juce::PopupMenu m;
    m.addItem (1, "Save As...");
    m.addItem (2, "Duplicate");
    m.addItem (3, "Rename...", user);
    m.addItem (4, "Delete...", user);
    m.addSeparator();
    m.addItem (5, "Import preset...");
    m.addItem (6, "Export preset...");
    m.addItem (7, "Import bank...");
    m.addItem (8, "Export visible presets as bank...");
    m.addSeparator();
    m.addItem (9, "Restore factory default (first factory preset)");
    m.addItem (10, "Reload saved version of this preset", pm.findById (proc.currentPresetId) != nullptr);
    m.addItem (11, "Open user preset folder");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileButton), [this, user] (int r)
    {
        auto& pm = proc.presets();
        auto fail = [] (const juce::Result& res, const juce::String& title)
        { if (res.failed()) juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle (title).withMessage (res.getErrorMessage()).withButton ("OK", 0), nullptr); };
        if (r == 1) savePreset (true);
        else if (r == 2) { std::string id; fail (pm.duplicate (proc.currentPresetId, id), "Duplicate"); if (! id.empty()) proc.loadPresetById (id); refreshAll(); }
        else if (r == 3 && user)
        {
            auto* w = new juce::AlertWindow ("Rename preset", {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", proc.currentPresetName, "Name");
            w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int res)
            {
                if (res != 1) return;
                const auto name = w->getTextEditorContents ("name").trim();
                if (proc.presets().renameUser (proc.currentPresetId, name).wasOk()) proc.currentPresetName = name;
                refreshAll();
            }), true);
        }
        else if (r == 4 && user)
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Delete preset")
                                              .withMessage ("Delete \"" + proc.currentPresetName + "\"? This cannot be undone.")
                                              .withButton ("Delete", 1).withButton ("Cancel", 0),
                                          [this] (int res)
                                          {
                                              if (res != 1) return;
                                              if (proc.presets().deleteUser (proc.currentPresetId).wasOk()) proc.currentPresetId.clear();
                                              refreshAll();
                                          });
        }
        else if (r >= 5 && r <= 8)
        {
            const bool save = r == 6 || r == 8;
            const juce::String pattern = (r == 7 || r == 8) ? "*.pfbank" : "*.pfpreset;*.json";
            chooser = std::make_unique<juce::FileChooser> (save ? "Export" : "Import", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), pattern);
            const int flags = (save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting : juce::FileBrowserComponent::openMode)
                            | juce::FileBrowserComponent::canSelectFiles;
            chooser->launchAsync (flags, [this, r] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                auto& pm2 = proc.presets();
                juce::Result res = juce::Result::ok();
                if (r == 5) { std::string id; res = pm2.importPreset (f, id); if (res.wasOk()) proc.loadPresetById (id); }
                else if (r == 6) res = pm2.exportPreset (proc.captureState(), f.withFileExtension ("pfpreset"));
                else if (r == 7) { int n = 0; res = pm2.importBank (f, n); if (res.wasOk()) juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Import bank").withMessage ("Imported " + juce::String (n) + " presets.").withButton ("OK", 0), nullptr); }
                else if (r == 8) res = pm2.exportBank (f.getFileNameWithoutExtension(), browser.visible(), f.withFileExtension ("pfbank"));
                if (res.failed()) juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Presets").withMessage (res.getErrorMessage()).withButton ("OK", 0), nullptr);
                refreshAll();
            });
        }
        else if (r == 9) { proc.applyPreset (pm.factory().front()); refreshAll(); }
        else if (r == 10) { proc.loadPresetById (proc.currentPresetId); refreshAll(); }
        else if (r == 11) { pm.userPresetDirectory().createDirectory(); pm.userPresetDirectory().startAsProcess(); }
    });
}

bool PedalFasterEditor::keyPressed (const juce::KeyPress& k)
{
    if (searchBox.hasKeyboardFocus (true) && k != juce::KeyPress::escapeKey) return false;
    if (k == juce::KeyPress::escapeKey) { searchBox.clear(); browser.setFilter ({}, {}); grabKeyboardFocus(); return true; }
    if (k == juce::KeyPress::rightKey || k == juce::KeyPress::pageDownKey) { prevNext (1); return true; }
    if (k == juce::KeyPress::leftKey || k == juce::KeyPress::pageUpKey) { prevNext (-1); return true; }
    if (k == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0)) { savePreset (false); return true; }
    if (k == juce::KeyPress ('f', juce::ModifierKeys::commandModifier, 0)) { searchBox.grabKeyboardFocus(); return true; }
    if (k == juce::KeyPress ('t', 0, 0)) { proc.tapTempo(); return true; }
    if (k == juce::KeyPress ('u', 0, 0)) { tunerButton.triggerClick(); return true; }
    if (k == juce::KeyPress ('b', 0, 0)) { bypassButton->triggerClick(); return true; }
    if (k.getKeyCode() >= '1' && k.getKeyCode() <= '8' && k.getModifiers().isCommandDown()) { select (k.getKeyCode() - '1'); return true; }
    return false;
}

void PedalFasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::panel);
    g.fillRect (topBar);
    g.fillRect (bottomBar);
    g.setColour (colours::outline);
    g.drawHorizontalLine (topBar.getBottom(), 0.0f, (float) getWidth());
    g.drawHorizontalLine (bottomBar.getY(), 0.0f, (float) getWidth());

    // branding
    g.setFont (Theme::font (20.0f, true));
    g.setColour (colours::text);
    g.drawText ("PEDAL", brandArea.withWidth (68), juce::Justification::centredLeft);
    g.setColour (colours::teal);
    g.drawText ("FASTER", brandArea.withTrimmedLeft (68), juce::Justification::centredLeft);

    if (tunerVisible) drawTuner (g, tunerArea);
}

void PedalFasterEditor::drawTuner (juce::Graphics& g, juce::Rectangle<int> r)
{
    const auto rd = proc.tuner.reading();
    g.setColour (colours::background.withAlpha (0.92f));
    g.fillRoundedRectangle (r.toFloat(), 10.0f);
    g.setColour (colours::teal);
    g.drawRoundedRectangle (r.toFloat().reduced (1.0f), 10.0f, 1.5f);
    auto inner = r.reduced (20);
    g.setFont (Theme::font (12.0f, true));
    g.setColour (colours::textDim);
    g.drawText ("TUNER", inner.removeFromTop (18), juce::Justification::centred);
    g.setFont (Theme::font (54.0f, true));
    const bool inTune = rd.valid && std::abs (rd.cents) < 3.0f;
    g.setColour (rd.valid ? (inTune ? colours::good : colours::text) : colours::textDim);
    g.drawText (rd.valid ? juce::String (rd.name) + juce::String (rd.octave) : "--", inner.removeFromTop (64), juce::Justification::centred);
    auto bar = inner.removeFromTop (26).reduced (10, 6);
    g.setColour (colours::panelRaised);
    g.fillRoundedRectangle (bar.toFloat(), 4.0f);
    g.setColour (colours::outline.brighter (0.3f));
    for (int c = -50; c <= 50; c += 10)
    {
        const float x = (float) bar.getCentreX() + (float) c / 50.0f * (float) bar.getWidth() * 0.5f;
        g.drawVerticalLine ((int) x, (float) bar.getY(), (float) bar.getBottom());
    }
    if (rd.valid)
    {
        const float x = (float) bar.getCentreX() + juce::jlimit (-50.0f, 50.0f, rd.cents) / 50.0f * (float) bar.getWidth() * 0.5f;
        g.setColour (inTune ? colours::good : colours::amber);
        g.fillRoundedRectangle (x - 3.0f, (float) bar.getY() - 4.0f, 6.0f, (float) bar.getHeight() + 8.0f, 3.0f);
    }
    g.setFont (Theme::font (13.0f));
    g.setColour (colours::textDim);
    g.drawText (rd.valid ? juce::String (rd.hz, 1) + " Hz   " + (rd.cents >= 0 ? "+" : "") + juce::String (rd.cents, 1) + " cents" : "Play a single note",
                inner.removeFromTop (22), juce::Justification::centred);
}

void PedalFasterEditor::resized()
{
    auto r = getLocalBounds();
    topBar = r.removeFromTop (54);
    bottomBar = r.removeFromBottom (92);

    // ---- top bar
    auto t = topBar.reduced (10, 11);
    brandArea = t.removeFromLeft (150);
    prevButton.setBounds (t.removeFromLeft (28)); t.removeFromLeft (4);
    presetButton.setBounds (t.removeFromLeft (230)); t.removeFromLeft (4);
    nextButton.setBounds (t.removeFromLeft (28)); t.removeFromLeft (6);
    favButton.setBounds (t.removeFromLeft (30)); t.removeFromLeft (6);
    saveButton.setBounds (t.removeFromLeft (56)); t.removeFromLeft (4);
    fileButton.setBounds (t.removeFromLeft (32)); t.removeFromLeft (10);
    searchBox.setBounds (t.removeFromLeft (std::max (140, t.getWidth() / 5))); t.removeFromLeft (6);
    categoryBox.setBounds (t.removeFromLeft (170)); t.removeFromLeft (10);
    cpuLabel.setBounds (t.removeFromRight (64));
    auto meters = t.removeFromRight (150);
    auto m1 = meters.removeFromTop (meters.getHeight() / 2);
    inLabel.setBounds (m1.removeFromLeft (28)); inMeter.setBounds (m1.reduced (0, 3));
    outLabel.setBounds (meters.removeFromLeft (28)); outMeter.setBounds (meters.reduced (0, 3));
    t.removeFromRight (8);
    bypassButton->setBounds (t.removeFromRight (72)); t.removeFromRight (4);
    tunerButton.setBounds (t.removeFromRight (58)); t.removeFromRight (4);
    qualityBox->setBounds (t.removeFromRight (84)); t.removeFromRight (4);
    masterButton.setBounds (t.removeFromRight (64));

    // ---- centre
    r.reduce (8, 8);
    const int browserW = juce::jlimit (240, 360, getWidth() / 5);
    const int editorW = juce::jlimit (280, 420, getWidth() / 4);
    browser.setBounds (r.removeFromLeft (browserW)); r.removeFromLeft (8);
    editorPanel.setBounds (r.removeFromRight (editorW)); r.removeFromRight (8);
    chain.setBounds (r);
    tunerArea = r.withSizeKeepingCentre (std::min (420, r.getWidth() - 40), 200);

    // ---- bottom bar
    auto b = bottomBar.reduced (10, 6);
    const int kw = 62;
    auto next = [&b] (int w = 62) { auto a = b.removeFromLeft (w); b.removeFromLeft (4); return a; };
    bottomKnobs[0]->setBounds (next());                                    // input
    gateButton->setBounds (next (54).withSizeKeepingCentre (54, 26));
    bottomKnobs[1]->setBounds (next());                                    // gate threshold
    b.removeFromLeft (10);
    for (int i = 2; i <= 4; ++i) bottomKnobs[i]->setBounds (next());       // EQ
    b.removeFromLeft (10);
    for (int i = 5; i <= 8; ++i) bottomKnobs[i]->setBounds (next());       // macros
    b.removeFromLeft (10);
    auto tap = next (70);
    tapButton.setBounds (tap.removeFromTop (tap.getHeight() / 2).reduced (2, 4));
    bpmLabel.setBounds (tap);
    // right side
    auto right = b;
    auto lim = right.removeFromRight (84);
    limiterButton->setBounds (lim.removeFromTop (lim.getHeight() / 2).reduced (2, 4));
    limiterLed.setBounds (lim.removeFromLeft (20));
    limiterLabel.setBounds (lim);
    right.removeFromRight (6);
    bottomKnobs[10]->setBounds (right.removeFromRight (kw));               // master
    right.removeFromRight (4);
    bottomKnobs[9]->setBounds (right.removeFromRight (kw));                // preset output
    right.removeFromRight (8);
    outModeBox->setBounds (right.removeFromRight (90).withSizeKeepingCentre (90, 26));
}
} // namespace pf
