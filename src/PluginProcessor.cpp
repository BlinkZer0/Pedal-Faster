#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/Amps/AmpModels.h"
#include "dsp/Cabinets/Cabinet.h"

namespace pf
{
PedalFasterProcessor::PedalFasterProcessor (juce::File presetRoot)
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout ([this] (int s) { return slotType (s); })),
      presetManager (presetRoot)
{
    formatManager.registerBasicFormats();
    rebuildParamCache();
    for (auto* p : params)
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            apvts.addParameterListener (withId->getParameterID(), this);

    if (! presetManager.factory().empty())
        applyPreset (presetManager.factory().front());

    // a guitar plugged into input 1 of an interface is the common standalone case
    if (wrapperType == wrapperType_Standalone)
        setParamReal (ids::inMode, 1.0f);

    dirty = false;
    startTimerHz (30);
}

PedalFasterProcessor::~PedalFasterProcessor()
{
    stopTimer();
    for (auto* p : params)
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            apvts.removeParameterListener (withId->getParameterID(), this);
}

void PedalFasterProcessor::rebuildParamCache()
{
    params.clear(); raw.clear(); paramIndex.clear();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            paramIndex[rp->getParameterID()] = (int) params.size();
            params.push_back (rp);
            raw.push_back (apvts.getRawParameterValue (rp->getParameterID()));
        }
    auto I = [this] (const juce::String& id) { auto it = paramIndex.find (id); jassert (it != paramIndex.end()); return it->second; };
    ix.inGain = I (ids::inGain); ix.inMode = I (ids::inMode); ix.bypass = I (ids::bypass);
    ix.gateOn = I (ids::gateOn); ix.gateThresh = I (ids::gateThresh); ix.gateRelease = I (ids::gateRelease);
    ix.ampOn = I (ids::ampOn); ix.ampType = I (ids::ampType); ix.ampGain = I (ids::ampGain); ix.ampBass = I (ids::ampBass);
    ix.ampMid = I (ids::ampMid); ix.ampTreble = I (ids::ampTreble); ix.ampPresence = I (ids::ampPresence);
    ix.ampMaster = I (ids::ampMaster); ix.ampSag = I (ids::ampSag); ix.ampTrim = I (ids::ampTrim);
    ix.cabOn = I (ids::cabOn); ix.cabType = I (ids::cabType); ix.cabMic = I (ids::cabMic); ix.cabLowCut = I (ids::cabLowCut);
    ix.cabHighCut = I (ids::cabHighCut); ix.cabLevel = I (ids::cabLevel); ix.cabMix = I (ids::cabMix);
    ix.eqOn = I (ids::eqOn); ix.eqLow = I (ids::eqLow); ix.eqMid = I (ids::eqMid); ix.eqMidFreq = I (ids::eqMidFreq); ix.eqHigh = I (ids::eqHigh);
    ix.outGain = I (ids::outGain); ix.limOn = I (ids::limOn); ix.limCeiling = I (ids::limCeiling); ix.masterVol = I (ids::masterVol);
    ix.outMode = I (ids::outMode); ix.quality = I (ids::quality); ix.tempo = I (ids::tempo); ix.tempoHost = I (ids::tempoHost);
    ix.tunerMute = I (ids::tunerMute);
    for (int m = 0; m < 4; ++m) ix.macro[m] = I (ids::macro (m));
    for (int s = 0; s < kNumSlots; ++s)
    {
        ix.slotType[s] = I (slotTypeId (s)); ix.slotOn[s] = I (slotOnId (s)); ix.slotMix[s] = I (slotMixId (s));
        for (int k = 0; k < kMaxEffectParams; ++k) ix.slotP[s][k] = I (slotParamId (s, k));
    }
}

//==============================================================================
bool PedalFasterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    return true;
}

void PedalFasterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedBlock = std::max (16, samplesPerBlock);
    sampleRateCache = sampleRate;
    EngineParams p;
    buildEngineParams (p);
    engine.prepare (sampleRate, preparedBlock, p);
    reloadIrIntoEngine();
    scratchR.assign ((size_t) preparedBlock + 8, 0.0f);
    loadMeasurer.reset (sampleRate, preparedBlock);
    const int q = juce::roundToInt (raw[(size_t) ix.quality]->load());
    setLatencySamples (engine.latencySamples (q));
    latencyQuality = q;
}

int PedalFasterProcessor::latencyForCurrentQuality() const
{
    return engine.latencySamples (juce::roundToInt (raw[(size_t) ix.quality]->load()));
}

//==============================================================================
void PedalFasterProcessor::buildEngineParams (EngineParams& p) const noexcept
{
    const size_t N = params.size();
    float norm[256];
    jassert (N <= 256);
    for (size_t i = 0; i < N && i < 256; ++i) norm[i] = params[i]->convertTo0to1 (raw[i]->load (std::memory_order_relaxed));

    // macros: offset their destinations around the stored value (0.5 = neutral)
    const int a = macroActive.load (std::memory_order_acquire);
    const int count = macroCount[a].load (std::memory_order_acquire);
    for (int k = 0; k < count; ++k)
    {
        const auto& m = macroBuf[a][(size_t) k];
        if (m.param < 0 || m.param >= (int) N) continue;
        const float mv = norm[(size_t) ix.macro[m.macro]];
        norm[(size_t) m.param] = juce::jlimit (0.0f, 1.0f, norm[(size_t) m.param] + (mv - 0.5f) * 2.0f * m.amount);
    }

    auto real = [&] (int i) { return params[(size_t) i]->convertFrom0to1 (norm[(size_t) i]); };
    auto on = [&] (int i) { return norm[(size_t) i] > 0.5f; };

    // MIDI expression pedal (CC 11) drives every wah slot's position
    const float expr = midiExpression.load (std::memory_order_relaxed);
    static const int wahIndex = EffectRegistry::get().indexOf ("wah");

    p.inGainDb = real (ix.inGain); p.inMode = juce::roundToInt (real (ix.inMode)); p.bypass = on (ix.bypass);
    p.gateOn = on (ix.gateOn); p.gateThreshDb = real (ix.gateThresh); p.gateReleaseMs = real (ix.gateRelease);
    p.ampOn = on (ix.ampOn); p.ampType = juce::roundToInt (real (ix.ampType));
    p.amp.gain = norm[(size_t) ix.ampGain]; p.amp.bass = norm[(size_t) ix.ampBass]; p.amp.mid = norm[(size_t) ix.ampMid];
    p.amp.treble = norm[(size_t) ix.ampTreble]; p.amp.presence = norm[(size_t) ix.ampPresence];
    p.amp.master = norm[(size_t) ix.ampMaster]; p.amp.sag = norm[(size_t) ix.ampSag]; p.ampTrimDb = real (ix.ampTrim);
    p.cabOn = on (ix.cabOn); p.cabType = juce::roundToInt (real (ix.cabType)); p.cabMic = real (ix.cabMic);
    p.cabLowCut = real (ix.cabLowCut); p.cabHighCut = real (ix.cabHighCut); p.cabLevelDb = real (ix.cabLevel); p.cabMix = real (ix.cabMix);
    p.eqOn = on (ix.eqOn); p.eqLow = real (ix.eqLow); p.eqMid = real (ix.eqMid); p.eqMidFreq = real (ix.eqMidFreq); p.eqHigh = real (ix.eqHigh);
    p.outGainDb = real (ix.outGain); p.limOn = on (ix.limOn); p.limCeilingDb = real (ix.limCeiling); p.masterDb = real (ix.masterVol);
    p.outMode = juce::roundToInt (real (ix.outMode)); p.quality = juce::roundToInt (real (ix.quality));
    for (int s = 0; s < kNumSlots; ++s)
    {
        auto& ss = p.slots[s];
        ss.type = juce::roundToInt (real (ix.slotType[s]));
        ss.enabled = on (ix.slotOn[s]);
        ss.mix = norm[(size_t) ix.slotMix[s]];
        for (int k = 0; k < kMaxEffectParams; ++k) ss.norm[k] = norm[(size_t) ix.slotP[s][k]];
        if (expr >= 0.0f && ss.type == wahIndex) ss.norm[0] = expr;
    }
}

void PedalFasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, buffer.getNumSamples());

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isController())
        {
            const int cc = m.getControllerNumber();
            const float v = (float) m.getControllerValue() / 127.0f;
            if (cc == 11) midiExpression.store (v);
            else if (cc == 7) midiVolume.store (v * v);
            else if (cc >= 20 && cc <= 23) midiMacro[cc - 20].store (v);
            else if (cc == 80 && m.getControllerValue() >= 64) midiNavRequest.fetch_add (1);
            else if (cc == 81 && m.getControllerValue() >= 64) midiNavRequest.fetch_sub (1);
        }
        else if (m.isProgramChange()) midiProgramRequest.store (m.getProgramChangeNumber());
    }

    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    if (! engine.isPrepared() || n == 0) { buffer.clear(); return; }

    auto& p = scratchParams;
    buildEngineParams (p);
    p.midiVolume = midiVolume.load();

    ProcessContext ctx;
    ctx.quality = p.quality;
    ctx.bpm = raw[(size_t) ix.tempo]->load();
    if (raw[(size_t) ix.tempoHost]->load() > 0.5f)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto bpm = pos->getBpm()) ctx.bpm = *bpm;
                if (auto ppq = pos->getPpqPosition()) ctx.ppqPosition = *ppq;
                ctx.playing = pos->getIsPlaying();
            }
    ctx.bpm = juce::jlimit (20.0, 400.0, ctx.bpm);
    lastBpm.store ((float) ctx.bpm);

    const bool mute = raw[(size_t) ix.tunerMute]->load() > 0.5f;
    for (int pos = 0; pos < n; pos += preparedBlock)
    {
        const int m = std::min (preparedBlock, n - pos);
        float* L = buffer.getWritePointer (0, pos);
        float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, pos) : scratchR.data();
        if (numIn == 0) { juce::FloatVectorOperations::clear (L, m); juce::FloatVectorOperations::clear (R, m); }
        engine.process (L, R, m, numIn >= 2 ? 2 : 1, p, ctx);
        if (numOut == 1 || buffer.getNumChannels() == 1)
            for (int i = 0; i < m; ++i) L[i] = 0.5f * (L[i] + R[i]);
        if (mute) { juce::FloatVectorOperations::clear (L, m); if (buffer.getNumChannels() > 1) juce::FloatVectorOperations::clear (R, m); }
    }
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
}

//==============================================================================
void PedalFasterProcessor::runMaintenance()
{
    EngineParams p;
    buildEngineParams (p);
    engine.syncSlots (p);

    const int q = juce::roundToInt (raw[(size_t) ix.quality]->load());
    if (q != latencyQuality.load() && engine.isPrepared())
    {
        latencyQuality = q;
        setLatencySamples (engine.latencySamples (q));
    }

    for (int m = 0; m < 4; ++m)
    {
        const float v = midiMacro[m].exchange (-1.0f);
        if (v >= 0.0f) setParamNorm (ids::macro (m), v);
    }

    const int pc = midiProgramRequest.exchange (-1);
    if (pc >= 0)
    {
        auto list = presetManager.query ({}, {}, PresetManager::Source::All, PresetManager::Sort::Bank);
        if (pc < (int) list.size()) applyPreset (*list[(size_t) pc]);
    }
    const int nav = midiNavRequest.exchange (0);
    if (nav != 0)
    {
        stepPreset (nav, presetManager.query ({}, {}, PresetManager::Source::All, PresetManager::Sort::Bank));

    }
}

void PedalFasterProcessor::updateTuner()
{

    const double sr = engine.getSampleRate();
    if (sr != tunerRate) { tuner.prepare (sr); tunerRate = sr; tunerScratch.assign (16384, 0.0f); }
    const int got = engine.readTuner (tunerScratch.data(), (int) tunerScratch.size());
    if (got > 0) tuner.push (tunerScratch.data(), got);
}

void PedalFasterProcessor::parameterChanged (const juce::String& id, float)
{
    if (applyingPreset) return;
    if (id == ids::masterVol || id == ids::tunerMute || id == ids::quality || id == ids::inMode) return; // not preset content
    dirty = true;
}

//==============================================================================
void PedalFasterProcessor::setParamNorm (const juce::String& id, float norm)
{
    if (auto* p = apvts.getParameter (id))
    {
        norm = juce::jlimit (0.0f, 1.0f, norm);
        if (std::abs (p->getValue() - norm) > 1.0e-7f) p->setValueNotifyingHost (norm);
    }
}

void PedalFasterProcessor::setParamReal (const juce::String& id, float realValue)
{
    if (auto* p = apvts.getParameter (id)) setParamNorm (id, p->convertTo0to1 (realValue));
}

float PedalFasterProcessor::getParamReal (const juce::String& id) const
{
    if (auto* v = apvts.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

int PedalFasterProcessor::slotType (int slot) const
{
    if (slot < 0 || slot >= kNumSlots || raw.empty()) return 0;
    return juce::roundToInt (raw[(size_t) ix.slotType[slot]]->load());
}

void PedalFasterProcessor::setRouting (const Routing& r)
{
    if (! r.isValid()) return;
    engine.setRouting (r);
    if (! applyingPreset) dirty = true;
}

void PedalFasterProcessor::setSlotType (int slot, int type)
{
    const auto& info = EffectRegistry::get().at (type);
    const bool wasApplying = applyingPreset;
    setParamReal (slotTypeId (slot), (float) info.index);
    for (int k = 0; k < kMaxEffectParams; ++k)
        setParamNorm (slotParamId (slot, k), k < (int) info.params.size() ? info.params[(size_t) k].toNorm (info.params[(size_t) k].defaultValue) : 0.5f);
    setParamNorm (slotMixId (slot), info.defaultMix);
    setParamNorm (slotOnId (slot), 1.0f);
    applyingPreset = wasApplying;
    publishMacros();
    EngineParams p; buildEngineParams (p); engine.syncSlots (p);
}

void PedalFasterProcessor::resetSlot (int slot) { setSlotType (slot, slotType (slot)); }

void PedalFasterProcessor::moveSlot (int slot, int newPosition, bool pre)
{
    Routing r = getRouting();
    std::vector<int> preList, postList;
    for (int i = 0; i < kNumSlots; ++i)
    {
        const int s = r.order[(size_t) i];
        if (s == slot) continue;
        (i < r.numPre ? preList : postList).push_back (s);
    }
    auto& target = pre ? preList : postList;
    newPosition = juce::jlimit (0, (int) target.size(), newPosition);
    target.insert (target.begin() + newPosition, slot);
    Routing out; int k = 0;
    for (int s : preList) out.order[(size_t) k++] = s;
    for (int s : postList) out.order[(size_t) k++] = s;
    out.numPre = (int) preList.size();
    setRouting (out);
}

EffectState PedalFasterProcessor::captureSlot (int slot) const
{
    EffectState e;
    const auto& info = EffectRegistry::get().at (slotType (slot));
    e.type = info.id; e.slotId = "slot-" + std::to_string (slot + 1);
    e.pre = getRouting().isPre (slot);
    e.enabled = raw[(size_t) ix.slotOn[slot]]->load() > 0.5f;
    e.mix = info.hasMix ? raw[(size_t) ix.slotMix[slot]]->load() : -1.0f;
    for (size_t k = 0; k < info.params.size(); ++k)
        e.params[info.params[k].key] = info.params[k].toReal (raw[(size_t) ix.slotP[slot][k]]->load());
    return e;
}

void PedalFasterProcessor::applySlot (int slot, const EffectState& e)
{
    const auto* info = EffectRegistry::get().find (e.type);
    if (info == nullptr) return;
    setParamReal (slotTypeId (slot), (float) info->index);
    for (int k = 0; k < kMaxEffectParams; ++k)
    {
        float norm = 0.5f;
        if (k < (int) info->params.size())
        {
            const auto& spec = info->params[(size_t) k];
            auto it = e.params.find (spec.key);
            norm = spec.toNorm (it != e.params.end() && std::isfinite (it->second) ? it->second : spec.defaultValue);
        }
        setParamNorm (slotParamId (slot, k), norm);
    }
    setParamNorm (slotMixId (slot), e.mix >= 0.0f ? e.mix : info->defaultMix);
    setParamNorm (slotOnId (slot), e.enabled ? 1.0f : 0.0f);
    publishMacros();
    EngineParams p; buildEngineParams (p); engine.syncSlots (p);
}

//==============================================================================
std::vector<MacroMapping> PedalFasterProcessor::deriveMacros (const Preset& p)
{
    std::vector<MacroMapping> m;
    auto add = [&m] (int macro, const std::string& target, float amt) { m.push_back ({ macro, target, amt }); };
    // Drive
    if (p.amp.enabled) add (0, ids::ampGain, 0.25f);
    for (size_t i = 0; i < p.effects.size(); ++i)
    {
        const auto& t = p.effects[i].type;
        const char* key = t == "transparent_od" || t == "mid_overdrive" || t == "octave_fuzz" ? "drive"
                        : t == "hard_distortion" || t == "high_gain_distortion" ? "gain"
                        : t == "fuzz" ? "sustain" : t == "clean_boost" ? "gainDb" : nullptr;
        if (key) { add (0, "fx:" + std::to_string (i) + ":" + key, 0.25f); break; }
    }
    if (! p.amp.enabled)
        for (size_t i = 0; i < p.effects.size(); ++i)
            if (p.effects[i].type == "compressor") { add (0, "fx:" + std::to_string (i) + ":thresholdDb", -0.3f); break; }
    // Tone
    if (p.amp.enabled) { add (1, ids::ampTreble, 0.2f); add (1, ids::ampPresence, 0.15f); }
    else add (1, ids::eqHigh, 0.2f);
    add (1, ids::cabHighCut, 0.15f);
    // Space
    bool anySpace = false;
    for (size_t i = 0; i < p.effects.size(); ++i)
    {
        const auto* info = EffectRegistry::get().find (p.effects[i].type);
        if (info && (info->category == "Delay" || info->category == "Reverb")) { add (2, "fx:" + std::to_string (i) + ":mix", 0.3f); anySpace = true; }
    }
    if (! anySpace)
        for (size_t i = 0; i < p.effects.size(); ++i)
        {
            const auto* info = EffectRegistry::get().find (p.effects[i].type);
            if (info && info->category == "Modulation" && info->hasMix) add (2, "fx:" + std::to_string (i) + ":mix", 0.3f);
        }
    // Output
    add (3, ids::outGain, 0.25f);
    return m;
}

void PedalFasterProcessor::setMacroMappings (const std::vector<MacroMapping>& m)
{
    macroMappings = m;
    publishMacros();
}

void PedalFasterProcessor::publishMacros()
{
    const int target = 1 - macroActive.load();
    int n = 0;
    for (auto& m : macroMappings)
    {
        if (n >= (int) macroBuf[target].size()) break;
        int idx = -1;
        juce::String t (m.target);
        if (t.startsWith ("slot:"))
        {
            juce::StringArray parts; parts.addTokens (t, ":", "");
            if (parts.size() == 3)
            {
                const int s = parts[1].getIntValue();
                if (s >= 0 && s < kNumSlots)
                {
                    if (parts[2] == "mix") idx = ix.slotMix[s];
                    else
                    {
                        const int k = EffectRegistry::get().at (slotType (s)).findParam (parts[2].toStdString());
                        if (k >= 0) idx = ix.slotP[s][k];
                    }
                }
            }
        }
        else if (auto it = paramIndex.find (t); it != paramIndex.end()) idx = it->second;
        if (idx >= 0 && m.macro >= 0 && m.macro < 4) macroBuf[target][(size_t) n++] = { m.macro, idx, m.amount };
    }
    macroCount[target].store (n, std::memory_order_release);
    macroActive.store (target, std::memory_order_release);
}

//==============================================================================
void PedalFasterProcessor::applyPreset (const Preset& pr)
{
    applyingPreset = true;
    lastPresetWarnings.clear();
    auto R = [this] (const char* id, float v) { setParamReal (id, v); };
    auto B = [this] (const char* id, bool v) { setParamNorm (id, v ? 1.0f : 0.0f); };

    R (ids::inGain, pr.inputGainDb);
    B (ids::gateOn, pr.gate.enabled); R (ids::gateThresh, pr.gate.thresholdDb); R (ids::gateRelease, pr.gate.releaseMs);
    B (ids::ampOn, pr.amp.enabled); R (ids::ampType, (float) std::max (0, ampTypeIndex (pr.amp.type)));
    R (ids::ampGain, pr.amp.gain); R (ids::ampBass, pr.amp.bass); R (ids::ampMid, pr.amp.mid); R (ids::ampTreble, pr.amp.treble);
    R (ids::ampPresence, pr.amp.presence); R (ids::ampMaster, pr.amp.master); R (ids::ampSag, pr.amp.sag); R (ids::ampTrim, pr.amp.trimDb);

    int cabIdx = std::max (0, cabTypeIndex (pr.cab.type));
    if (cabIdx == (int) CabType::CustomIR)
    {
        juce::File f (juce::String (pr.cab.irFile));
        if (pr.cab.irFile.empty() || ! f.existsAsFile() || loadImpulseResponse (f).failed())
        {
            if (irData.getNumSamples() == 0)
            {
                lastPresetWarnings.push_back ("Impulse response '" + pr.cab.irFile + "' not found - using 4x12 closed-back");
                cabIdx = (int) CabType::Closed4x12;
            }
        }
    }
    B (ids::cabOn, pr.cab.enabled); R (ids::cabType, (float) cabIdx); R (ids::cabMic, pr.cab.mic);
    R (ids::cabLowCut, pr.cab.lowCutHz); R (ids::cabHighCut, pr.cab.highCutHz); R (ids::cabLevel, pr.cab.levelDb); R (ids::cabMix, pr.cab.mix);
    B (ids::eqOn, pr.eq.enabled); R (ids::eqLow, pr.eq.lowDb); R (ids::eqMid, pr.eq.midDb); R (ids::eqMidFreq, pr.eq.midFreqHz); R (ids::eqHigh, pr.eq.highDb);
    R (ids::outGain, pr.outputGainDb); B (ids::limOn, pr.limiter.enabled); R (ids::limCeiling, pr.limiter.ceilingDb);
    if (pr.tempoBpm > 0.0f) R (ids::tempo, pr.tempoBpm);
    for (int m = 0; m < 4; ++m) setParamNorm (ids::macro (m), 0.5f);

    // ---- slots: honour "slot-N" ids when they are valid and unique, otherwise assign in order
    std::array<int, kNumSlots> slotOf {}; slotOf.fill (-1);
    std::array<bool, kNumSlots> used {};
    bool idsValid = pr.effects.size() <= (size_t) kNumSlots;
    for (size_t i = 0; i < pr.effects.size() && idsValid; ++i)
    {
        const auto& sid = pr.effects[i].slotId;
        const int s = sid.rfind ("slot-", 0) == 0 ? std::atoi (sid.c_str() + 5) - 1 : -1;
        if (s < 0 || s >= kNumSlots || used[(size_t) s]) idsValid = false;
        else { used[(size_t) s] = true; slotOf[i] = s; }
    }
    if (! idsValid) { used.fill (false); for (size_t i = 0; i < pr.effects.size() && i < (size_t) kNumSlots; ++i) { slotOf[i] = (int) i; used[i] = true; } }

    for (int s = 0; s < kNumSlots; ++s)
    {
        int effectIndex = -1;
        for (size_t i = 0; i < pr.effects.size() && i < (size_t) kNumSlots; ++i) if (slotOf[i] == s) effectIndex = (int) i;
        if (effectIndex >= 0) applySlotNoSync (s, pr.effects[(size_t) effectIndex]);
        else
        {
            setParamReal (slotTypeId (s), 0.0f); setParamNorm (slotOnId (s), 1.0f); setParamNorm (slotMixId (s), 1.0f);
            for (int k = 0; k < kMaxEffectParams; ++k) setParamNorm (slotParamId (s, k), 0.5f);
        }
    }

    // ---- routing: pre effects, then free slots split between pre and post, post effects after the amp
    std::vector<int> pre, post, empty;
    for (size_t i = 0; i < pr.effects.size() && i < (size_t) kNumSlots; ++i) (pr.effects[i].pre ? pre : post).push_back (slotOf[i]);
    for (int s = 0; s < kNumSlots; ++s) if (! used[(size_t) s]) empty.push_back (s);
    const size_t emptyPre = (empty.size() + 1) / 2;
    for (size_t i = 0; i < empty.size(); ++i) (i < emptyPre ? pre : post).push_back (empty[i]);
    Routing r; int k = 0;
    for (int s : pre) r.order[(size_t) k++] = s;
    for (int s : post) r.order[(size_t) k++] = s;
    r.numPre = (int) pre.size();
    engine.setRouting (r);

    // ---- macros (preset format "fx:<effect index>:<key>" -> internal "slot:<slot>:<key>")
    std::vector<MacroMapping> maps;
    for (auto m : pr.macros.empty() ? deriveMacros (pr) : pr.macros)
    {
        juce::String t (m.target);
        if (t.startsWith ("fx:"))
        {
            juce::StringArray parts; parts.addTokens (t, ":", "");
            const int ei = parts[1].getIntValue();
            if (parts.size() != 3 || ei < 0 || ei >= (int) pr.effects.size() || ei >= kNumSlots) continue;
            m.target = ("slot:" + juce::String (slotOf[(size_t) ei]) + ":" + parts[2]).toStdString();
        }
        maps.push_back (m);
    }
    setMacroMappings (maps);

    EngineParams ep; buildEngineParams (ep); engine.syncSlots (ep);
    currentPresetId = pr.id;
    currentPresetName = pr.name;
    applyingPreset = false;
    dirty = false;
    ++stateVersion;
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

void PedalFasterProcessor::applySlotNoSync (int slot, const EffectState& e)
{
    const auto* info = EffectRegistry::get().find (e.type);
    if (info == nullptr) { setParamReal (slotTypeId (slot), 0.0f); return; }
    setParamReal (slotTypeId (slot), (float) info->index);
    for (int k = 0; k < kMaxEffectParams; ++k)
    {
        float norm = 0.5f;
        if (k < (int) info->params.size())
        {
            const auto& spec = info->params[(size_t) k];
            auto it = e.params.find (spec.key);
            norm = spec.toNorm (it != e.params.end() && std::isfinite (it->second) ? it->second : spec.defaultValue);
        }
        setParamNorm (slotParamId (slot, k), norm);
    }
    setParamNorm (slotMixId (slot), e.mix >= 0.0f ? e.mix : info->defaultMix);
    setParamNorm (slotOnId (slot), e.enabled ? 1.0f : 0.0f);
}

Preset PedalFasterProcessor::captureState() const
{
    Preset p;
    const Preset* src = presetManager.findById (currentPresetId);
    p.id = currentPresetId; p.name = currentPresetName.toStdString();
    if (src) { p.category = src->category; p.description = src->description; p.tags = src->tags; p.author = src->author; }
    else p.category = "User";

    auto V = [this] (const char* id) { return getParamReal (id); };
    auto on = [this] (const char* id) { return getParamReal (id) > 0.5f; };
    p.inputGainDb = V (ids::inGain);
    p.gate.enabled = on (ids::gateOn); p.gate.thresholdDb = V (ids::gateThresh); p.gate.releaseMs = V (ids::gateRelease);
    p.amp.enabled = on (ids::ampOn); p.amp.type = ampTypeIds()[(size_t) juce::jlimit (0, 5, juce::roundToInt (V (ids::ampType)))];
    p.amp.gain = V (ids::ampGain); p.amp.bass = V (ids::ampBass); p.amp.mid = V (ids::ampMid); p.amp.treble = V (ids::ampTreble);
    p.amp.presence = V (ids::ampPresence); p.amp.master = V (ids::ampMaster); p.amp.sag = V (ids::ampSag); p.amp.trimDb = V (ids::ampTrim);
    p.cab.enabled = on (ids::cabOn); p.cab.type = cabTypeIds()[(size_t) juce::jlimit (0, (int) CabType::Count - 1, juce::roundToInt (V (ids::cabType)))];
    p.cab.mic = V (ids::cabMic); p.cab.lowCutHz = V (ids::cabLowCut); p.cab.highCutHz = V (ids::cabHighCut);
    p.cab.levelDb = V (ids::cabLevel); p.cab.mix = V (ids::cabMix);
    if (p.cab.type == "custom_ir") p.cab.irFile = irPath.toStdString();
    p.eq.enabled = on (ids::eqOn); p.eq.lowDb = V (ids::eqLow); p.eq.midDb = V (ids::eqMid); p.eq.midFreqHz = V (ids::eqMidFreq); p.eq.highDb = V (ids::eqHigh);
    p.outputGainDb = V (ids::outGain); p.limiter.enabled = on (ids::limOn); p.limiter.ceilingDb = V (ids::limCeiling);
    if (src && src->tempoBpm > 0.0f) p.tempoBpm = V (ids::tempo);

    const Routing r = getRouting();
    std::array<int, kNumSlots> listIndexOfSlot {}; listIndexOfSlot.fill (-1);
    for (int pos = 0; pos < kNumSlots; ++pos)
    {
        const int s = r.order[(size_t) pos];
        if (slotType (s) <= 0) continue;
        auto e = captureSlot (s);
        e.pre = pos < r.numPre;
        listIndexOfSlot[(size_t) s] = (int) p.effects.size();
        p.effects.push_back (e);
    }
    for (auto m : macroMappings)
    {
        juce::String t (m.target);
        if (t.startsWith ("slot:"))
        {
            juce::StringArray parts; parts.addTokens (t, ":", "");
            const int s = parts[1].getIntValue();
            if (parts.size() != 3 || s < 0 || s >= kNumSlots || listIndexOfSlot[(size_t) s] < 0) continue;
            m.target = ("fx:" + juce::String (listIndexOfSlot[(size_t) s]) + ":" + parts[2]).toStdString();
        }
        p.macros.push_back (m);
    }
    return p;
}

bool PedalFasterProcessor::loadPresetById (const std::string& id)
{
    if (auto* p = presetManager.findById (id)) { applyPreset (*p); return true; }
    return false;
}

void PedalFasterProcessor::stepPreset (int delta, const std::vector<const Preset*>& list)
{
    if (list.empty()) return;
    int cur = -1;
    for (size_t i = 0; i < list.size(); ++i) if (list[i]->id == currentPresetId) cur = (int) i;
    const int n = (int) list.size();
    const int next = cur < 0 ? (delta > 0 ? 0 : n - 1) : ((cur + delta) % n + n) % n;
    applyPreset (*list[(size_t) next]);
}

//==============================================================================
juce::Result PedalFasterProcessor::loadImpulseResponse (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr) return juce::Result::fail ("Unsupported or unreadable audio file: " + file.getFileName());
    if (reader->sampleRate <= 0 || reader->lengthInSamples <= 0) return juce::Result::fail ("Empty impulse response");
    const int maxLen = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 2.0)); // IRs are truncated to 2 s
    const int ch = (int) std::min<unsigned int> (2, reader->numChannels);
    juce::AudioBuffer<float> buf (ch, maxLen);
    reader->read (&buf, 0, maxLen, 0, true, ch > 1);
    const int fade = std::min (maxLen / 4, (int) (reader->sampleRate * 0.005));
    for (int c = 0; c < ch; ++c) buf.applyGainRamp (c, maxLen - fade, fade, 1.0f, 0.0f);
    irData = std::move (buf);
    irRate = reader->sampleRate;
    irName = file.getFileName();
    irPath = file.getFullPathName();
    reloadIrIntoEngine();
    setParamReal (ids::cabType, (float) CabType::CustomIR);
    return juce::Result::ok();
}

void PedalFasterProcessor::clearImpulseResponse()
{
    irData.setSize (0, 0); irName.clear(); irPath.clear();
    engine.cabinet().clearCustomIR();
}

void PedalFasterProcessor::reloadIrIntoEngine()
{
    if (irData.getNumSamples() == 0) return;
    juce::AudioBuffer<float> copy (irData);
    engine.cabinet().loadImpulseResponse (std::move (copy), irRate);
}

void PedalFasterProcessor::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! tapTimes.empty() && now - tapTimes.back() > 2000.0) tapTimes.clear();
    tapTimes.push_back (now);
    if (tapTimes.size() > 5) tapTimes.erase (tapTimes.begin());
    if (tapTimes.size() >= 2)
    {
        const double avg = (tapTimes.back() - tapTimes.front()) / (double) (tapTimes.size() - 1);
        setParamReal (ids::tempo, (float) juce::jlimit (40.0, 240.0, 60000.0 / avg));
        setParamNorm (ids::tempoHost, 0.0f);
    }
}

//==============================================================================
void PedalFasterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::ValueTree rig ("RIG");
    rig.setProperty ("version", 1, nullptr);
    rig.setProperty ("routing", juce::String (getRouting().toString()), nullptr);
    rig.setProperty ("presetId", juce::String (currentPresetId), nullptr);
    rig.setProperty ("presetName", currentPresetName, nullptr);
    rig.setProperty ("dirty", dirty.load(), nullptr);
    juce::Array<juce::var> maps;
    for (auto& m : macroMappings)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("macro", m.macro); o->setProperty ("target", juce::String (m.target)); o->setProperty ("amount", m.amount);
        maps.add (juce::var (o));
    }
    rig.setProperty ("macros", juce::JSON::toString (maps, true), nullptr);
    if (irData.getNumSamples() > 0)
    {
        juce::MemoryBlock mb;
        for (int c = 0; c < irData.getNumChannels(); ++c) mb.append (irData.getReadPointer (c), sizeof (float) * (size_t) irData.getNumSamples());
        rig.setProperty ("irName", irName, nullptr);
        rig.setProperty ("irPath", irPath, nullptr);
        rig.setProperty ("irRate", irRate, nullptr);
        rig.setProperty ("irChannels", irData.getNumChannels(), nullptr);
        rig.setProperty ("irSamples", irData.getNumSamples(), nullptr);
        rig.setProperty ("irData", mb.toBase64Encoding(), nullptr);
    }
    state.removeChild (state.getChildWithName ("RIG"), nullptr);
    state.appendChild (rig, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void PedalFasterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.hasType (apvts.state.getType())) return;
    auto rig = tree.getChildWithName ("RIG").createCopy();
    tree.removeChild (tree.getChildWithName ("RIG"), nullptr);

    applyingPreset = true;
    apvts.replaceState (tree);

    if (rig.isValid())
    {
        engine.setRouting (Routing::fromString (rig.getProperty ("routing").toString().toStdString()));
        currentPresetId = rig.getProperty ("presetId").toString().toStdString();
        currentPresetName = rig.getProperty ("presetName", "Init").toString();
        std::vector<MacroMapping> maps;
        const auto parsedMacros = juce::JSON::parse (rig.getProperty ("macros").toString()); // keep alive while iterating
        if (auto* arr = parsedMacros.getArray())
            for (auto& m : *arr)
                maps.push_back ({ (int) m["macro"], m["target"].toString().toStdString(), (float) (double) m["amount"] });
        macroMappings = maps;

        const int ch = rig.getProperty ("irChannels", 0), ns = rig.getProperty ("irSamples", 0);
        if (ch > 0 && ns > 0)
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (rig.getProperty ("irData").toString()) && mb.getSize() == sizeof (float) * (size_t) ch * (size_t) ns)
            {
                irData.setSize (ch, ns);
                for (int c = 0; c < ch; ++c)
                    std::memcpy (irData.getWritePointer (c), static_cast<const char*> (mb.getData()) + sizeof (float) * (size_t) ns * (size_t) c, sizeof (float) * (size_t) ns);
                irRate = (double) rig.getProperty ("irRate", 48000.0);
                irName = rig.getProperty ("irName").toString();
                irPath = rig.getProperty ("irPath").toString();
                reloadIrIntoEngine();
            }
        }
        else clearImpulseResponse();
    }
    publishMacros();
    EngineParams p; buildEngineParams (p); engine.syncSlots (p);
    applyingPreset = false;
    dirty = rig.isValid() && (bool) rig.getProperty ("dirty", false);
    ++stateVersion;
}

juce::AudioProcessorEditor* PedalFasterProcessor::createEditor() { return new PedalFasterEditor (*this); }
} // namespace pf

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new pf::PedalFasterProcessor(); }
