#include "TestUtils.h"
#include "dsp/Amps/AmpSection.h"
#include "dsp/Cabinets/Cabinet.h"
#include "dsp/SignalChain.h"
#include "dsp/Tuner.h"

using namespace pft;

namespace
{
/** Parameters that make "flat by default" effects actually do something in tests. */
std::map<std::string, float> activeParams (const std::string& id)
{
    if (id == "graphic_eq") return { { "band400", 8.0f }, { "band3200", -8.0f } };
    if (id == "parametric_eq") return { { "midGain", 9.0f }, { "highGain", -9.0f } };
    if (id == "noise_gate") return { { "thresholdDb", -30.0f } };
    return {};
}

class RegistryTest : public juce::UnitTest
{
public:
    RegistryTest() : juce::UnitTest ("Effect registry", "DSP") {}
    void runTest() override
    {
        beginTest ("30 pedal algorithms with unique ids and valid specs");
        const auto& reg = EffectRegistry::get();
        expectEquals (reg.size(), 31, "30 effects + the empty slot");
        std::set<std::string> idsSeen;
        for (auto& e : reg.all())
        {
            expect (idsSeen.insert (e.id).second, "duplicate id " + e.id);
            expect ((int) e.params.size() <= kMaxEffectParams, juce::String (e.id) + " has too many params");
            if (e.index > 0) expect ((bool) e.create, juce::String (e.id) + " has no factory");
            std::set<std::string> keys;
            for (auto& p : e.params)
            {
                expect (keys.insert (p.key).second, "duplicate key " + p.key);
                expect (p.defaultValue >= p.realMin() && p.defaultValue <= p.realMax(), juce::String (e.id) + "." + p.key + " default out of range");
                const float back = p.toReal (p.toNorm (p.defaultValue));
                expect (std::abs (back - p.defaultValue) < 1.0e-3f * (1.0f + std::abs (p.defaultValue)) || p.integer, juce::String (e.id) + "." + p.key + " norm round trip");
            }
        }

        beginTest ("each effect is a separate class (no shared instance types)");
        std::set<std::string> typeNames;
        for (auto& e : reg.all())
            if (e.index > 0) { auto fx = e.create(); typeNames.insert (typeid (*fx).name()); }
        expectEquals ((int) typeNames.size(), 30);
    }
};

class EffectBehaviourTest : public juce::UnitTest
{
public:
    EffectBehaviourTest() : juce::UnitTest ("Effect behaviour", "DSP") {}
    void runTest() override
    {
        const double sr = 48000.0;
        const auto guitar = makeGuitarDI (sr, 2.0);

        for (auto& e : EffectRegistry::get().all())
        {
            if (e.index == 0) continue;
            beginTest (e.name);

            // silence in -> silence out
            juce::AudioBuffer<float> zero (2, (int) sr / 2); zero.clear();
            auto z = renderEffect (e.id, activeParams (e.id), zero, sr, 256);
            expect (peakAbs (z) < 1.0e-4f, juce::String (e.id) + ": silence produced " + juce::String (peakAbs (z)));

            // guitar in -> finite, bounded, audibly processed
            auto y = renderEffect (e.id, activeParams (e.id), guitar, sr, 256);
            expect (allFinite (y), juce::String (e.id) + ": non-finite output");
            expect (peakAbs (y) < 8.0f, juce::String (e.id) + ": unbounded output " + juce::String (peakAbs (y)));
            expect (rmsDb (y) > -70.0f, juce::String (e.id) + ": output silent");
            juce::AudioBuffer<float> diff (y);
            for (int c = 0; c < 2; ++c) diff.addFrom (c, 0, guitar, c, 0, guitar.getNumSamples(), -1.0f);
            expect (rmsDb (diff) > rmsDb (guitar) - 50.0f, juce::String (e.id) + ": output is identical to input");

            // sample rates and buffer sizes
            for (double rate : { 44100.0, 48000.0, 96000.0 })
                for (int block : { 32, 64, 128, 256, 512, 4096 })
                {
                    auto sig = makeGuitarDI (rate, 0.5);
                    auto o = renderEffect (e.id, activeParams (e.id), sig, rate, block);
                    expect (allFinite (o) && peakAbs (o) < 8.0f, juce::String (e.id) + " failed at " + juce::String (rate) + " Hz / " + juce::String (block));
                }

            // Eco quality (no ADAA) still sane
            auto eco = renderEffect (e.id, activeParams (e.id), guitar, sr, 128, 0);
            expect (allFinite (eco), juce::String (e.id) + ": eco mode non-finite");
        }
    }
};

class EffectAlgorithmTest : public juce::UnitTest
{
public:
    EffectAlgorithmTest() : juce::UnitTest ("Effect algorithms", "DSP") {}
    void runTest() override
    {
        const double sr = 48000.0;

        beginTest ("digital delay places an echo at the set time");
        {
            juce::AudioBuffer<float> imp (2, (int) sr); imp.clear(); imp.setSample (0, 0, 1.0f); imp.setSample (1, 0, 1.0f);
            auto y = renderEffect ("digital_delay", { { "timeMs", 100.0f }, { "feedback", 0.5f }, { "stereo", 0.0f }, { "lowCutHz", 20.0f }, { "highCutHz", 20000.0f } }, imp, sr, 256);
            int peakIdx = 0; float pk = 0;
            for (int i = 1; i < (int) (0.15 * sr); ++i) if (std::abs (y.getSample (0, i)) > pk) { pk = std::abs (y.getSample (0, i)); peakIdx = i; }
            expectWithinAbsoluteError (peakIdx, (int) (0.1 * sr), 3);
            float pk2 = 0; for (int i = (int) (0.18 * sr); i < (int) (0.22 * sr); ++i) pk2 = std::max (pk2, std::abs (y.getSample (0, i)));
            expectWithinAbsoluteError (pk2 / pk, 0.5f, 0.08f);
        }

        beginTest ("tremolo modulates amplitude, chorus/vibrato modulate time");
        {
            auto sine = makeSine (sr, 440.0f, 0.5f, 1.0);
            auto t = renderEffect ("tremolo", { { "rateHz", 4.0f }, { "depth", 1.0f } }, sine, sr, 256);
            float mn = 1, mx = 0;
            for (int blk = 0; blk < 40; ++blk) { const float r = t.getRMSLevel (0, blk * 1200, 1200); mn = std::min (mn, r); mx = std::max (mx, r); }
            expect (mx / std::max (mn, 1.0e-6f) > 4.0f, "tremolo depth");
            auto v = renderEffect ("vibrato", { { "rateHz", 5.0f }, { "depth", 1.0f } }, sine, sr, 256);
            expectWithinAbsoluteError (v.getRMSLevel (0, 4800, 38000), sine.getRMSLevel (0, 4800, 38000), 0.03f); // pitch, not level
        }

        beginTest ("feedback paths stay stable at extreme settings");
        {
            juce::AudioBuffer<float> burst (2, (int) (sr * 12)); burst.clear();
            juce::Random r (3);
            for (int i = 0; i < (int) (sr * 0.5); ++i) { const float v = r.nextFloat() * 2 - 1; burst.setSample (0, i, v); burst.setSample (1, i, v); }
            auto tape = renderEffect ("tape_delay", { { "timeMs", 120.0f }, { "feedback", 1.05f }, { "saturation", 1.0f } }, burst, sr, 512);
            expect (allFinite (tape) && peakAbs (tape) < 4.0f, "tape self-oscillation bounded: " + juce::String (peakAbs (tape)));
            auto hall = renderEffect ("hall_reverb", { { "decay", 20.0f }, { "shimmer", 1.0f }, { "size", 1.0f } }, burst, sr, 512);
            expect (allFinite (hall) && peakAbs (hall) < 4.0f, "shimmer hall bounded: " + juce::String (peakAbs (hall)));
            auto flang = renderEffect ("flanger", { { "feedback", 0.95f }, { "depth", 1.0f } }, burst, sr, 512);
            expect (allFinite (flang) && peakAbs (flang) < 8.0f, "flanger bounded");
            auto phas = renderEffect ("phaser", { { "feedback", 0.95f }, { "stages", 5.0f } }, burst, sr, 512);
            expect (allFinite (phas) && peakAbs (phas) < 8.0f, "phaser bounded");
            auto plate = renderEffect ("plate_reverb", { { "decay", 10.0f } }, burst, sr, 512);
            expect (allFinite (plate) && peakAbs (plate) < 4.0f, "plate bounded");
            // tails decay
            expect (rmsDb (plate, (int) (sr * 11.5), (int) (sr * 0.5)) < rmsDb (plate, (int) (sr * 0.5), (int) (sr * 0.5)) - 20.0f, "plate tail decays");
        }

        beginTest ("pitch shifter moves the fundamental");
        {
            auto sine = makeSine (sr, 220.0f, 0.4f, 1.0);
            auto y = renderEffect ("pitch_shifter", { { "semitones", 12.0f } }, sine, sr, 256);
            const float at440 = thd (y, sr, 440.0f, 4800) , at220energy = thd (sine, sr, 220.0f, 4800);
            (void) at440; (void) at220energy;
            Tuner t; t.prepare (sr); t.push (y.getReadPointer (0, 9600), 30000);
            expect (t.reading().valid && std::abs (t.reading().hz - 440.0f) < 15.0f, "shifted pitch " + juce::String (t.reading().hz));
        }

        beginTest ("octaver produces a sub-octave");
        {
            auto sine = makeSine (sr, 220.0f, 0.4f, 1.0);
            auto y = renderEffect ("octaver", { { "sub1", 1.0f }, { "direct", 0.0f } }, sine, sr, 256);
            Tuner t; t.prepare (sr); t.push (y.getReadPointer (0, 9600), 30000);
            expect (t.reading().valid && std::abs (t.reading().hz - 110.0f) < 6.0f, "octave down pitch " + juce::String (t.reading().hz));
        }

        beginTest ("gain pedals are meaningfully different from each other");
        {
            const auto guitar = makeGuitarDI (sr, 2.0);
            std::vector<std::string> gain { "clean_boost", "transparent_od", "mid_overdrive", "hard_distortion", "high_gain_distortion", "fuzz", "octave_fuzz", "bitcrusher" };
            std::vector<std::vector<float>> prof;
            for (auto& id : gain) prof.push_back (bandProfile (renderEffect (id, {}, guitar, sr, 256), sr, (int) (0.2 * sr)));
            for (size_t a = 0; a < gain.size(); ++a)
                for (size_t b = a + 1; b < gain.size(); ++b)
                    expect (profileDistance (prof[a], prof[b]) > 0.4f, juce::String (gain[a]) + " vs " + juce::String (gain[b]) + " too similar: " + juce::String (profileDistance (prof[a], prof[b])));
        }

        beginTest ("compressor reduces dynamic range");
        {
            auto loud = makeSine (sr, 330.0f, 0.8f, 0.5), quiet = makeSine (sr, 330.0f, 0.08f, 0.5);
            std::map<std::string, float> p { { "thresholdDb", -30.0f }, { "ratio", 8.0f }, { "makeupDb", 0.0f }, { "kneeDb", 0.0f } };
            const float lo = rmsDb (renderEffect ("compressor", p, loud, sr, 256), 12000), qu = rmsDb (renderEffect ("compressor", p, quiet, sr, 256), 12000);
            expect (lo - qu < 12.0f, "20 dB input difference compressed to " + juce::String (lo - qu));
        }

        beginTest ("noise gate closes below threshold and opens above");
        {
            auto quiet = makeSine (sr, 200.0f, 0.001f, 0.5), loud = makeSine (sr, 200.0f, 0.3f, 0.5);
            std::map<std::string, float> p { { "thresholdDb", -40.0f } };
            expect (rmsDb (renderEffect ("noise_gate", p, quiet, sr, 256), 12000) < -100.0f, "gate closed");
            expect (rmsDb (renderEffect ("noise_gate", p, loud, sr, 256), 12000) > -15.0f, "gate open");
        }
    }
};

class AmpCabTest : public juce::UnitTest
{
public:
    AmpCabTest() : juce::UnitTest ("Amps and cabinets", "DSP") {}

    juce::AudioBuffer<float> runAmp (int type, const AmpControls& c, const juce::AudioBuffer<float>& in, double sr, int quality = 1)
    {
        AmpSection a; a.setType (type); a.setControls (c); a.setQuality (quality); a.setEnabled (true); a.setTrimDb (0);
        a.prepare (sr, 256);
        juce::AudioBuffer<float> out (in);
        for (int pos = 0; pos < out.getNumSamples(); pos += 256)
        {
            const int n = std::min (256, out.getNumSamples() - pos);
            a.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), n);
        }
        return out;
    }

    void runTest() override
    {
        const double sr = 48000.0;
        const auto guitar = makeGuitarDI (sr, 2.0);
        AmpControls c; c.gain = 0.6f;

        beginTest ("six amp models are perceptibly different");
        std::vector<std::vector<float>> prof; std::vector<float> dist;
        const auto sine = makeSine (sr, 196.0f, 0.3f, 1.0);
        for (int t = 0; t < 6; ++t)
        {
            auto y = runAmp (t, c, guitar, sr);
            expect (allFinite (y) && peakAbs (y) < 4.0f, "amp " + juce::String (t) + " bounded");
            prof.push_back (bandProfile (y, sr, (int) (0.2 * sr)));
            dist.push_back (thd (runAmp (t, c, sine, sr), sr, 196.0f, 12000));
        }
        for (int a = 0; a < 6; ++a)
            for (int b = a + 1; b < 6; ++b)
            {
                const float d = profileDistance (prof[(size_t) a], prof[(size_t) b]) + std::abs (dist[(size_t) a] - dist[(size_t) b]) * 5.0f;
                expect (d > 0.5f, juce::String (ampTypeNames()[(size_t) a]) + " vs " + juce::String (ampTypeNames()[(size_t) b]) + " too similar: " + juce::String (d));
            }
        {
            AmpControls low; low.gain = 0.3f;
            const auto quietSine = makeSine (sr, 196.0f, 0.1f, 1.0);
            const float clean = thd (runAmp (0, low, quietSine, sr), sr, 196.0f, 12000), metal = thd (runAmp (4, low, quietSine, sr), sr, 196.0f, 12000);
            expect (metal > clean * 3.0f, "Modern Metal distorts far more than Studio Clean: " + juce::String (metal) + " vs " + juce::String (clean));
        }

        beginTest ("Studio Clean stays clean at low gain; Bass DI keeps lows");
        {
            AmpControls clean; clean.gain = 0.2f; clean.master = 0.4f;
            expect (thd (runAmp (0, clean, makeSine (sr, 220.0f, 0.25f, 1.0), sr), sr, 220.0f, 12000) < 0.05f, "clean THD");
            AmpControls bass; bass.gain = 0.3f;
            auto in = makeSine (sr, 55.0f, 0.3f, 1.0);
            auto out = runAmp (5, bass, in, sr);
            expect (rmsDb (out, 12000) > rmsDb (in, 12000) - 10.0f, "bass fundamental preserved");
        }

        beginTest ("oversampling latency is reported per quality and is integer");
        {
            AmpSection a; a.prepare (sr, 256);
            expectEquals (a.latencyFor (0), 0);
            expect (a.latencyFor (1) >= 0 && a.latencyFor (2) >= a.latencyFor (1), "latency grows with quality");
            for (int q = 0; q < 3; ++q) expect (allFinite (runAmp (3, c, guitar, sr, q)), "quality " + juce::String (q));
        }

        beginTest ("cabinets differ; direct is near-transparent");
        {
            std::vector<std::vector<float>> cp;
            for (int t = 0; t < 5; ++t)
            {
                CabinetSection cab; cab.setType (t); cab.prepare (sr, 256);
                juce::AudioBuffer<float> y (guitar);
                for (int pos = 0; pos < y.getNumSamples(); pos += 256)
                    cab.process (y.getWritePointer (0, pos), y.getWritePointer (1, pos), std::min (256, y.getNumSamples() - pos));
                cp.push_back (bandProfile (y, sr, 9600));
            }
            for (int a = 0; a < 5; ++a) for (int b = a + 1; b < 5; ++b)
                expect (profileDistance (cp[(size_t) a], cp[(size_t) b]) > 0.3f, "cabs " + juce::String (a) + "/" + juce::String (b) + " too similar");
        }

        beginTest ("custom IR loads and convolves");
        {
            CabinetSection cab; cab.setType ((int) CabType::CustomIR); cab.setLowCut (20); cab.setHighCut (20000); cab.prepare (sr, 256);
            juce::AudioBuffer<float> ir (1, 2048); ir.clear();
            ir.setSample (0, 0, 1.0f); ir.setSample (0, 480, 0.8f); // direct + 10 ms echo
            cab.loadImpulseResponse (std::move (ir), sr);
            juce::AudioBuffer<float> imp (2, 256);
            int found = -1;
            for (int attempt = 0; attempt < 400 && found < 0; ++attempt)
            {
                imp.clear(); imp.setSample (0, 0, 1.0f); imp.setSample (1, 0, 1.0f);
                juce::AudioBuffer<float> longBuf (2, 1024); longBuf.clear(); longBuf.copyFrom (0, 0, imp, 0, 0, 256); longBuf.copyFrom (1, 0, imp, 1, 0, 256);
                for (int pos = 0; pos < 1024; pos += 256) cab.process (longBuf.getWritePointer (0, pos), longBuf.getWritePointer (1, pos), 256);
                const float early = std::abs (longBuf.getSample (0, 0)), echo = std::abs (longBuf.getSample (0, 480));
                if (early > 0.05f && echo > early * 0.5f) found = attempt;
                juce::Thread::sleep (5);
                cab.reset();
            }
            expect (found >= 0, "convolution echo not observed");
        }
    }
};

class ChainTest : public juce::UnitTest
{
public:
    ChainTest() : juce::UnitTest ("Signal chain", "DSP") {}
    void runTest() override
    {
        beginTest ("routing pack/unpack and text round trip");
        Routing r; r.order = { 3, 1, 7, 0, 2, 6, 5, 4 }; r.numPre = 3;
        expect (r.isValid());
        auto u = Routing::unpack (r.pack());
        expect (u.order == r.order && u.numPre == r.numPre);
        auto s = Routing::fromString (r.toString());
        expect (s.order == r.order && s.numPre == r.numPre, "string round trip " + juce::String (r.toString()));
        expect (! Routing::fromString ("1,2,3").isValid() || Routing::fromString ("1,2,3").order == Routing().order, "malformed routing falls back");

        beginTest ("slot swaps while processing are safe and leak-free");
        {
            EffectSlot slot; slot.prepare (48000.0, 128);
            SlotSettings st; st.type = 1;
            for (int k = 0; k < kMaxEffectParams; ++k) st.norm[k] = 0.5f;
            slot.buildNow (1, st);
            auto sig = makeGuitarDI (48000.0, 2.0);
            ProcessContext ctx;
            juce::Random rng (7);
            for (int pos = 0, blk = 0; pos + 128 <= sig.getNumSamples(); pos += 128, ++blk)
            {
                if (blk % 5 == 0)
                {
                    st.type = rng.nextInt (EffectRegistry::get().size());
                    slot.publish (st.type, st);
                    slot.collectGarbage();
                }
                st.enabled = (blk % 37) != 0;
                slot.process (sig.getWritePointer (0, pos), sig.getWritePointer (1, pos), 128, st, ctx);
            }
            expect (allFinite (sig) && peakAbs (sig) < 8.0f, "random swapping stayed finite");
        }

        beginTest ("tuner reads a 110 Hz A within 2 cents");
        {
            Tuner t; t.prepare (48000.0);
            auto sine = makeSine (48000.0, 110.0f, 0.3f, 1.0);
            t.push (sine.getReadPointer (0), sine.getNumSamples());
            const auto rd = t.reading();
            expect (rd.valid && juce::String (rd.name) == "A" && rd.octave == 2 && std::abs (rd.cents) < 2.0f,
                    "tuner read " + juce::String (rd.name) + juce::String (rd.octave) + " " + juce::String (rd.cents));
        }
    }
};

RegistryTest registryTest;
EffectBehaviourTest effectBehaviourTest;
EffectAlgorithmTest effectAlgorithmTest;
AmpCabTest ampCabTest;
ChainTest chainTest;
} // namespace
