#include "TestUtils.h"
#include "PluginEditor.h"
#include "dsp/Cabinets/Cabinet.h"

using namespace pft;
extern juce::File gRegressionDir;

namespace
{
constexpr float kTargetRmsDb = -20.0f; // calibration target for the reference DI performance

juce::File regressionFile()
{
    return gRegressionDir.getChildFile ("baseline.json");
}

class ProcessorTest : public juce::UnitTest
{
public:
    ProcessorTest() : juce::UnitTest ("Processor", "Integration") {}
    void runTest() override
    {
        beginTest ("instantiation, silence in -> silence out at all rates and block sizes");
        for (double sr : { 44100.0, 48000.0, 96000.0 })
            for (int block : { 32, 64, 128, 256, 512 })
            {
                auto p = makeProcessor (sr, block);
                juce::AudioBuffer<float> silence (2, (int) sr / 2); silence.clear();
                auto out = render (*p, silence, block);
                expect (allFinite (out) && peakAbs (out) < 1.0e-3f, "silence at " + juce::String (sr) + "/" + juce::String (block) + ": " + juce::String (peakAbs (out)));
                auto big = render (*p, makeGuitarDI (sr, 0.5), 4096); // offline-sized blocks
                expect (allFinite (big), "large block render");
            }

        beginTest ("mono and stereo bus layouts");
        for (auto [in, out] : { std::pair<int, int> { 1, 1 }, { 1, 2 }, { 2, 2 } })
        {
            auto p = std::make_unique<PedalFasterProcessor> (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PedalFasterTests"));
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (in == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
            layout.outputBuses.add (out == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
            expect (p->setBusesLayout (layout), "layout " + juce::String (in) + "->" + juce::String (out));
            p->prepareToPlay (48000.0, 256);
            juce::AudioBuffer<float> buf (std::max (in, out), 48000);
            auto g = makeGuitarDI (48000.0, 1.0);
            for (int c = 0; c < buf.getNumChannels(); ++c) buf.copyFrom (c, 0, g, 0, 0, 48000);
            auto o = render (*p, buf, 256);
            expect (allFinite (o) && rmsDb (o) > -60.0f, "processes audio");
        }

        beginTest ("global bypass is the input delayed by the reported latency (every quality)");
        for (int q = 0; q < 3; ++q)
        {
            auto p = makeProcessor (48000.0, 128);
            p->setParamReal (ids::quality, (float) q);
            p->prepareToPlay (48000.0, 128);
            p->setParamNorm (ids::bypass, 1.0f);
            p->prepareToPlay (48000.0, 128);
            const int lat = p->getLatencySamples();
            expectEquals (lat, p->latencyForCurrentQuality());
            auto in = makeGuitarDI (48000.0, 0.5);
            auto out = render (*p, in, 128);
            float maxErr = 0;
            for (int i = lat; i < in.getNumSamples(); ++i) maxErr = std::max (maxErr, std::abs (out.getSample (0, i) - in.getSample (0, i - lat)));
            expect (maxErr < 1.0e-6f, "bypass error " + juce::String (maxErr) + " at quality " + juce::String (q) + " latency " + juce::String (lat));
        }

        beginTest ("signal chain order matters (pre-amp routing)");
        {
            auto p = makeProcessor();
            Preset a = factoryPresets()[0];
            a.effects.clear();
            EffectState boost; boost.type = "clean_boost"; boost.params = { { "gainDb", 24.0f } };
            EffectState comp; comp.type = "compressor"; comp.params = { { "thresholdDb", -30.0f }, { "ratio", 10.0f }, { "makeupDb", 0.0f } };
            a.effects = { boost, comp };
            loadAndSettle (*p, a);
            auto y1 = render (*p, makeGuitarDI (48000.0, 1.0), 256);
            a.effects = { comp, boost };
            loadAndSettle (*p, a);
            auto y2 = render (*p, makeGuitarDI (48000.0, 1.0), 256);
            expect (std::abs (rmsDb (y1) - rmsDb (y2)) > 3.0f, "order changed level by " + juce::String (rmsDb (y1) - rmsDb (y2)));
            auto r = p->getRouting();
            expect (r.isPre (r.order[0]) && r.numPre >= 2);
        }

        beginTest ("slot automation follows the slot and shows real units");
        {
            auto p = makeProcessor();
            p->setSlotType (0, EffectRegistry::get().indexOf ("compressor"));
            auto* prm = p->param (slotParamId (0, 0));
            expect (prm->getText (prm->getValue(), 32).contains ("dB"), "threshold text: " + prm->getText (prm->getValue(), 32));
            p->runMaintenance();
            expectEquals (p->slotType (0), EffectRegistry::get().indexOf ("compressor"));
        }

        beginTest ("macros move their destinations");
        {
            auto p = makeProcessor();
            loadAndSettle (*p, *p->presets().findById ("factory.classic-rock.solo-flight"));
            auto lo = (p->setParamNorm (ids::macro (0), 0.0f), render (*p, makeGuitarDI (48000.0, 1.0), 256));
            p->prepareToPlay (48000.0, 256);
            auto hi = (p->setParamNorm (ids::macro (0), 1.0f), render (*p, makeGuitarDI (48000.0, 1.0), 256));
            expect (profileDistance (bandProfile (lo, 48000.0, 4800), bandProfile (hi, 48000.0, 4800)) > 0.3f, "drive macro changes the tone");
        }

        beginTest ("MIDI: expression drives wah, CC7 volume");
        {
            auto p = makeProcessor();
            loadAndSettle (*p, *p->presets().findById ("factory.hard-rock.fire-ladder"));
            juce::AudioBuffer<float> buf (makeGuitarDI (48000.0, 0.5));
            juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 7, 0), 0);
            p->processBlock (buf, m);
            juce::MidiBuffer none;
            juce::AudioBuffer<float> next (makeGuitarDI (48000.0, 0.5));
            for (int i = 0; i < 4; ++i) p->processBlock (next, none);
            expect (rmsDb (next, 12000) < -60.0f, "CC7 = 0 mutes");
        }
    }
};

class PresetPlaybackTest : public juce::UnitTest
{
public:
    PresetPlaybackTest() : juce::UnitTest ("Preset playback", "Integration") {}
    void runTest() override
    {
        const double sr = 48000.0;
        const auto di = makeGuitarDI (sr, 4.0);
        auto p = makeProcessor (sr, 256);

        beginTest ("all factory presets load, are finite, levelled and distinct");
        std::vector<std::vector<float>> profiles;
        std::vector<float> levels;
        juce::var baseline = juce::JSON::parse (regressionFile());
        auto* baseObj = baseline.getDynamicObject();
        juce::DynamicObject::Ptr newBaseline = new juce::DynamicObject();
        int regressions = 0;
        for (auto& preset : factoryPresets())
        {
            loadAndSettle (*p, preset);
            expect (p->lastPresetWarnings.empty(), juce::String (preset.name) + " produced load warnings");
            for (int s = 0; s < kNumSlots; ++s)
                expectEquals (p->getEngine().activeSlotType (s), p->slotType (s), juce::String (preset.name) + " slot not adopted");
            auto out = render (*p, di, 256);
            expect (allFinite (out), juce::String (preset.name) + " non-finite");
            const float level = rmsDb (out, (int) (0.1 * sr));
            levels.push_back (level);
            expect (level > kTargetRmsDb - 4.0f && level < kTargetRmsDb + 4.0f, juce::String (preset.name) + " level " + juce::String (level, 1) + " dBFS");
            expect (peakAbs (out) <= juce::Decibels::decibelsToGain (preset.limiter.ceilingDb) + 1.0e-3f, juce::String (preset.name) + " exceeds ceiling");
            auto prof = bandProfile (out, sr, (int) (0.2 * sr));
            profiles.push_back (prof);

            // acoustic regression: level and spectral shape versus the stored baseline
            juce::Array<juce::var> arr; arr.add (level); for (auto v : prof) arr.add (v);
            newBaseline->setProperty (juce::String (preset.id), arr);
            if (baseObj != nullptr && baseObj->hasProperty (juce::String (preset.id)))
            {
                auto* b = baseObj->getProperty (juce::String (preset.id)).getArray();
                if (b != nullptr && b->size() == (int) prof.size() + 1)
                {
                    std::vector<float> bp; for (int i = 1; i < b->size(); ++i) bp.push_back ((float) (double) (*b)[i]);
                    const bool ok = std::abs ((float) (double) (*b)[0] - level) < 1.5f && profileDistance (bp, prof) < 1.5f;
                    if (! ok) ++regressions;
                    expect (ok, juce::String (preset.name) + " regressed: level " + juce::String ((float) (double) (*b)[0], 1) + " -> " + juce::String (level, 1)
                                    + ", shape distance " + juce::String (profileDistance (bp, prof), 2));
                }
            }
        }
        if (baseObj == nullptr)
        {
            regressionFile().getParentDirectory().createDirectory();
            regressionFile().replaceWithText (juce::JSON::toString (juce::var (newBaseline.get())));
            logMessage ("No regression baseline found - wrote " + regressionFile().getFullPathName());
        }

        int clashes = 0;
        for (size_t a = 0; a < profiles.size(); ++a)
            for (size_t b = a + 1; b < profiles.size(); ++b)
                if (profileDistance (profiles[a], profiles[b]) < 0.05f && std::abs (levels[a] - levels[b]) < 0.1f) ++clashes;
        expectEquals (clashes, 0, "presets with indistinguishable output");

        beginTest ("clean presets stay clean; bass presets keep the low end");
        for (auto& preset : factoryPresets())
        {
            if (preset.category == "Pristine Clean")
            {
                loadAndSettle (*p, preset);
                auto out = render (*p, makeSine (sr, 220.0f, 0.25f, 1.0), 256);
                const float d = thd (out, sr, 220.0f, 24000);
                expect (d < 0.08f, juce::String (preset.name) + " THD " + juce::String (d * 100.0f, 1) + "%");
            }
            if (preset.category == "Bass & Extended Range")
            {
                loadAndSettle (*p, preset);
                auto in = makeSine (sr, 55.0f, 0.3f, 1.0);
                auto out = render (*p, in, 256);
                const auto prof = bandProfile (out, sr, 12000);
                const float lowBand = prof[0], mid = prof[8];
                expect (lowBand > mid - 6.0f, juce::String (preset.name) + " lost its low end");
            }
        }

        beginTest ("high-gain presets: no runaway hiss with no input (gate + gain staging)");
        for (auto& preset : factoryPresets())
            if (preset.category == "Metal Rhythm")
            {
                loadAndSettle (*p, preset);
                juce::AudioBuffer<float> noise (2, (int) sr);
                juce::Random r (11);
                for (int i = 0; i < noise.getNumSamples(); ++i) { const float v = (r.nextFloat() * 2 - 1) * 0.0003f; noise.setSample (0, i, v); noise.setSample (1, i, v); }
                auto out = render (*p, noise, 256);
                expect (rmsDb (out, 12000) < -55.0f, juce::String (preset.name) + " hiss " + juce::String (rmsDb (out, 12000), 1) + " dBFS");
            }
    }
};

class StateTest : public juce::UnitTest
{
public:
    StateTest() : juce::UnitTest ("State recall", "Integration") {}
    void runTest() override
    {
        beginTest ("DAW state round trip restores parameters, routing, macros and IR exactly");
        auto a = makeProcessor();
        loadAndSettle (*a, *a->presets().findById ("factory.metal-lead.twin-harmony"));
        a->setParamReal (ids::ampGain, 0.33f);
        a->moveSlot (a->getRouting().order[0], 99, false);
        a->setSlotType (6, EffectRegistry::get().indexOf ("phaser"));
        juce::AudioBuffer<float> ir (1, 1000); ir.clear(); ir.setSample (0, 0, 1.0f); ir.setSample (0, 300, 0.5f);
        auto irFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pf_test_ir.wav");
        {
            irFile.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> os (irFile.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), 48000.0, 1, 24, {}, 0));
            if (w) { os.release(); w->writeFromAudioSampleBuffer (ir, 0, 1000); }
        }
        expect (a->loadImpulseResponse (irFile).wasOk(), "IR loads");
        expect (a->isDirty(), "edits mark the preset modified");
        juce::MemoryBlock state;
        a->getStateInformation (state);
        irFile.deleteFile(); // the project must not depend on the external file

        auto b = makeProcessor();
        b->setStateInformation (state.getData(), (int) state.getSize());
        b->prepareToPlay (48000.0, 256);
        a->prepareToPlay (48000.0, 256);
        {
            juce::StringArray la, lb;
            la.addLines (PresetSerializer::toJson (a->captureState())); lb.addLines (PresetSerializer::toJson (b->captureState()));
            juce::String firstDiff;
            for (int i = 0; i < std::max (la.size(), lb.size()) && firstDiff.isEmpty(); ++i)
                if (la[i] != lb[i]) firstDiff = "line " + juce::String (i) + ": " + la[i].trim() + "  vs  " + lb[i].trim();
            expect (firstDiff.isEmpty(), "captured state differs at " + firstDiff);
        }
        expect (a->getRouting().pack() == b->getRouting().pack(), "routing restored");
        expect (b->getImpulseResponseName() == "pf_test_ir.wav", "embedded IR restored");
        expect (b->isDirty(), "modified flag restored");
        auto in = makeGuitarDI (48000.0, 1.0);
        // fresh state for both, then let both background convolution loads finish before comparing
        a->prepareToPlay (48000.0, 256); b->prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> quiet (2, 256);
        for (int i = 0; i < 300; ++i) { quiet.clear(); render (*a, quiet, 256); quiet.clear(); render (*b, quiet, 256); juce::Thread::sleep (2); }
        auto ya = render (*a, in, 256), yb = render (*b, in, 256);
        float maxDiff = 0;
        for (int i = 0; i < ya.getNumSamples(); ++i) maxDiff = std::max (maxDiff, std::abs (ya.getSample (0, i) - yb.getSample (0, i)));
        expect (maxDiff < 1.0e-4f, "restored processor renders identically (max diff " + juce::String (maxDiff) + ")");

        beginTest ("garbage state is ignored safely");
        auto c = makeProcessor();
        const char junk[] = "this is not a plugin state";
        c->setStateInformation (junk, (int) sizeof (junk));
        expect (allFinite (render (*c, makeGuitarDI (48000.0, 0.3), 256)));

        beginTest ("user presets save and reload through the processor");
        auto d = makeProcessor();
        loadAndSettle (*d, factoryPresets()[123]);
        d->setParamReal (ids::ampTreble, 0.91f);
        auto captured = d->captureState();
        captured.name = "Integration Save";
        std::string id;
        expect (d->presets().saveUser (captured, id).wasOk());
        d->loadPresetById (factoryPresets()[0].id);
        expect (d->loadPresetById (id));
        expectWithinAbsoluteError (d->getParamReal (ids::ampTreble), 0.91f, 1.0e-4f);
        d->presets().deleteUser (id);
    }
};

class SwitchingTest : public juce::UnitTest
{
public:
    SwitchingTest() : juce::UnitTest ("Preset switching", "Integration") {}
    void runTest() override
    {
        beginTest ("switching presets and pedal types while audio runs stays finite and click-limited");
        auto p = makeProcessor (48000.0, 128);
        auto in = makeGuitarDI (48000.0, 8.0);
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        juce::Random rng (5);
        const auto& all = factoryPresets();
        float worstJump = 0, prev = 0;
        for (int pos = 0, blk = 0; pos + 128 <= out.getNumSamples(); pos += 128, ++blk)
        {
            if (blk % 23 == 0) p->applyPreset (all[(size_t) rng.nextInt ((int) all.size())]);
            if (blk % 41 == 0) p->setSlotType (rng.nextInt (8), rng.nextInt (EffectRegistry::get().size()));
            if (blk % 3 == 0) p->runMaintenance();
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, 128);
            p->processBlock (view, midi);
            for (int i = 0; i < 128; ++i) { const float v = view.getSample (0, i); worstJump = std::max (worstJump, std::abs (v - prev)); prev = v; }
        }
        expect (allFinite (out), "finite");
        expect (worstJump < 1.2f, "largest sample step " + juce::String (worstJump));
    }
};

class EditorTest : public juce::UnitTest
{
public:
    EditorTest() : juce::UnitTest ("Editor", "Integration") {}
    void runTest() override
    {
        beginTest ("editor opens, lays out at several sizes, follows preset changes and closes");
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        expect (ed != nullptr);
        for (auto [w, h] : { std::pair<int, int> { 1100, 700 }, { 1400, 860 }, { 2200, 1300 } }) ed->setSize (w, h);
        p->applyPreset (factoryPresets()[100]);
        p->setSlotType (2, EffectRegistry::get().indexOf ("harmonizer"));
        ed->setSize (1500, 900);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        expect (img.isValid());
        ed.reset();
    }
};

ProcessorTest processorTest;
PresetPlaybackTest presetPlaybackTest;
StateTest stateTest;
SwitchingTest switchingTest;
EditorTest editorTest;
} // namespace
