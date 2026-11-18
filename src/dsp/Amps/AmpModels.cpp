#include "AmpModels.h"

namespace pf
{
const std::vector<std::string>& ampTypeIds()
{
    static const std::vector<std::string> ids { "studio_clean", "class_a_chime", "british_crunch", "hot_rod_lead", "modern_metal", "bass_di" };
    return ids;
}
const std::vector<std::string>& ampTypeNames()
{
    static const std::vector<std::string> n { "Studio Clean", "Class A Chime", "British Crunch", "Hot Rod Lead", "Modern Metal", "Bass DI / Amp" };
    return n;
}
int ampTypeIndex (const std::string& id)
{
    const auto& ids = ampTypeIds();
    for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) return (int) i;
    return -1;
}

namespace
{
//==============================================================================
/** 1. Studio Clean: one gentle preamp stage, bright cap that fades with gain, scooped tone stack
    *before* a recovery stage, push-pull power stage with lots of headroom. */
class StudioClean final : public AmpModel
{
public:
    void prepare (double osRate) override
    {
        sr = osRate;
        inHp.highpass (sr, 30, 0.7);
        st1.negHardness = 1.4f; st1.bias = 0.05f; st1.prepare (sr, 14000);
        st2.negHardness = 1.3f; st2.bias = 0.02f; st2.prepare (sr, 16000);
        tone.bassHz = 90; tone.midHz = 480; tone.midQ = 0.6; tone.midRange = 8; tone.midOffset = -3; tone.trebleHz = 2800;
        power.classA = false; power.headroom = 1.6f; power.prepare (sr);
        gainS.prepare (sr, 20); masterS.prepare (sr, 20);
    }
    void reset() noexcept override { inHp.reset(); bright.reset(); st1.reset(); st2.reset(); tone.reset(); power.reset(); gainS.snap(); masterS.snap(); }
    void setControls (const AmpControls& c) noexcept override
    {
        bright.highShelf (sr, 2500, 0.7, 6.0 * (1.0 - c.gain));
        tone.set (sr, c.bass, c.mid, c.treble);
        power.setPresence (sr, 4000, (c.presence - 0.5) * 12.0);
        power.sagAmount = c.sag * 0.5f;
        gainS.setTarget (1.2f + 9.0f * c.gain * c.gain);
        masterS.setTarget (0.25f + 1.6f * c.master);
    }
    void process (float* L, float* R, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = gainS.next(); power.drive = masterS.next();
            for (int ch = 0; ch < 2; ++ch)
            {
                float* s = ch == 0 ? L : R;
                float y = bright.process (inHp.process (s[i], ch), ch);
                y = st1.process (y, g, ch);
                y = tone.process (y, ch);
                y = st2.process (y, 1.8f, ch);
                s[i] = power.process (y, ch) * 1.25f;
            }
        }
    }
private:
    Biquad inHp, bright; TriodeStage st1, st2; ToneStack tone; PowerStage power;
};

//==============================================================================
/** 2. Class A Chime: bright pentode-style first stage, "top boost" tone circuit, fixed upper-mid chime
    resonance, no global negative feedback, cathode-biased single-ended-like power stage that breaks up early. */
class ClassAChime final : public AmpModel
{
public:
    void prepare (double osRate) override
    {
        sr = osRate;
        inHp.highpass (sr, 60, 0.7);
        st1.negHardness = 2.1f; st1.bias = 0.12f; st1.prepare (sr, 16000);
        tone.bassHz = 120; tone.bassRange = 10; tone.midHz = 1100; tone.midQ = 0.9; tone.midRange = 8; tone.trebleHz = 2200; tone.trebleRange = 14;
        chime.peak (sr, 3200, 1.2, 3.5);
        power.classA = true; power.headroom = 0.9f; power.prepare (sr);
        gainS.prepare (sr, 20); masterS.prepare (sr, 20);
    }
    void reset() noexcept override { inHp.reset(); st1.reset(); tone.reset(); chime.reset(); power.reset(); gainS.snap(); masterS.snap(); }
    void setControls (const AmpControls& c) noexcept override
    {
        tone.set (sr, c.bass, c.mid, c.treble);
        power.setPresence (sr, 5000, (c.presence - 0.5) * 16.0);
        power.sagAmount = 0.2f + c.sag * 0.8f;
        gainS.setTarget (2.0f + 28.0f * c.gain * c.gain);
        masterS.setTarget (0.5f + 3.5f * c.master);
    }
    void process (float* L, float* R, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = gainS.next(); power.drive = masterS.next();
            for (int ch = 0; ch < 2; ++ch)
            {
                float* s = ch == 0 ? L : R;
                float y = st1.process (inHp.process (s[i], ch), g, ch);
                y = chime.process (tone.process (y, ch), ch);
                s[i] = power.process (y * 0.8f, ch) * 1.1f;
            }
        }
    }
private:
    Biquad inHp, chime; TriodeStage st1; ToneStack tone; PowerStage power;
};

//==============================================================================
/** 3. British Crunch: tight input, bright-capped first stage, second cascaded stage, cathode-follower
    soft limit, mid-forward tone stack after the preamp, EL34-style push-pull power section with sag. */
class BritishCrunch final : public AmpModel
{
public:
    void prepare (double osRate) override
    {
        sr = osRate;
        inHp.highpass (sr, 90, 0.7);
        brightCap.highShelf (sr, 700, 0.7, 4.0);
        st1.negHardness = 1.6f; st1.bias = 0.15f; st1.prepare (sr, 11000);
        st2.negHardness = 1.8f; st2.bias = -0.1f; st2.prepare (sr, 9000);
        tone.bassHz = 100; tone.bassRange = 10; tone.midHz = 720; tone.midQ = 0.8; tone.midRange = 10; tone.midOffset = 2; tone.trebleHz = 2200; tone.trebleRange = 10;
        power.classA = false; power.headroom = 1.0f; power.prepare (sr);
        gainS.prepare (sr, 20); masterS.prepare (sr, 20);
    }
    void reset() noexcept override { inHp.reset(); brightCap.reset(); st1.reset(); st2.reset(); tone.reset(); power.reset(); gainS.snap(); masterS.snap(); }
    void setControls (const AmpControls& c) noexcept override
    {
        tone.set (sr, c.bass, c.mid, c.treble);
        power.setPresence (sr, 3500, (c.presence - 0.5) * 14.0);
        power.sagAmount = c.sag * 0.9f;
        gainS.setTarget (1.5f + 14.0f * c.gain);
        masterS.setTarget (0.4f + 2.6f * c.master);
    }
    void process (float* L, float* R, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = gainS.next(); power.drive = masterS.next();
            for (int ch = 0; ch < 2; ++ch)
            {
                float* s = ch == 0 ? L : R;
                float y = brightCap.process (inHp.process (s[i], ch), ch);
                y = st1.process (y, g * 1.5f, ch);
                y = st2.process (y, 1.0f + g * 0.9f, ch);
                y = fastTanh (y * 1.2f);                 // cathode follower
                y = tone.process (y, ch);
                s[i] = power.process (y * 0.9f, ch) * 0.4f;
            }
        }
    }
private:
    Biquad inHp, brightCap; TriodeStage st1, st2; ToneStack tone; PowerStage power;
};

//==============================================================================
/** 4. Hot Rod Lead: three cascaded stages with progressively darker Miller filtering (smooth, compressed,
    sustaining), mid-hump tone stack, warm power stage, final top-end smoothing. */
class HotRodLead final : public AmpModel
{
public:
    void prepare (double osRate) override
    {
        sr = osRate;
        inHp.highpass (sr, 70, 0.7);
        st1.negHardness = 1.5f; st1.bias = 0.2f; st1.prepare (sr, 8000);
        st2.negHardness = 1.7f; st2.bias = -0.15f; st2.prepare (sr, 7000);
        st3.negHardness = 1.4f; st3.bias = 0.1f; st3.prepare (sr, 6500);
        tone.bassHz = 110; tone.midHz = 900; tone.midQ = 0.7; tone.midRange = 10; tone.midOffset = 3; tone.trebleHz = 2000;
        smoothTop.lowpass (sr, 6500, 0.6);
        power.classA = false; power.headroom = 1.0f; power.prepare (sr);
        gainS.prepare (sr, 20); masterS.prepare (sr, 20);
    }
    void reset() noexcept override { inHp.reset(); st1.reset(); st2.reset(); st3.reset(); tone.reset(); smoothTop.reset(); power.reset(); gainS.snap(); masterS.snap(); }
    void setControls (const AmpControls& c) noexcept override
    {
        tone.set (sr, c.bass, c.mid, c.treble);
        power.setPresence (sr, 3800, (c.presence - 0.5) * 12.0);
        power.sagAmount = c.sag * 0.7f;
        gainS.setTarget (2.0f + 16.0f * c.gain);
        masterS.setTarget (0.35f + 2.2f * c.master);
    }
    void process (float* L, float* R, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = gainS.next(); power.drive = masterS.next();
            for (int ch = 0; ch < 2; ++ch)
            {
                float* s = ch == 0 ? L : R;
                float y = st1.process (inHp.process (s[i], ch), g, ch);
                y = st2.process (y, 1.5f + g * 1.2f, ch);
                y = st3.process (y, 1.5f + g * 0.6f, ch);
                y = tone.process (y, ch);
                s[i] = smoothTop.process (power.process (y * 0.8f, ch), ch) * 0.4f;
            }
        }
    }
private:
    Biquad inHp, smoothTop; TriodeStage st1, st2, st3; ToneStack tone; PowerStage power;
};

//==============================================================================
/** 5. Modern Metal: heavy pre-distortion low cut for tightness, four stages with interstage high-passes,
    wide-range scoopable mids, stiff (low-sag) power stage, low resonance and strong presence restore the body. */
class ModernMetal final : public AmpModel
{
public:
    void prepare (double osRate) override
    {
        sr = osRate;
        tight1.highpass (sr, 160, 0.7); tight2.lowShelf (sr, 250, 0.7, -6.0);
        st1.negHardness = 1.5f; st1.bias = 0.2f; st1.prepare (sr, 10000);
        st2.negHardness = 1.9f; st2.bias = -0.2f; st2.prepare (sr, 9000);
        st3.negHardness = 1.6f; st3.bias = 0.15f; st3.prepare (sr, 8000);
        st4.negHardness = 1.3f; st4.bias = 0.0f; st4.prepare (sr, 9000);
        inter.highpass (sr, 110, 0.7);
        tone.bassHz = 90; tone.midHz = 750; tone.midQ = 1.0; tone.midRange = 12; tone.trebleHz = 2800;
        resonance.lowShelf (sr, 90, 0.8, 4.0);
        fizz.lowpass (sr, 9500, 0.7);
        power.classA = false; power.headroom = 1.0f; power.prepare (sr);
        gainS.prepare (sr, 20); masterS.prepare (sr, 20);
    }
    void reset() noexcept override
    {
        tight1.reset(); tight2.reset(); st1.reset(); st2.reset(); st3.reset(); st4.reset(); inter.reset();
        tone.reset(); resonance.reset(); fizz.reset(); power.reset(); gainS.snap(); masterS.snap();
    }
    void setControls (const AmpControls& c) noexcept override
    {
        tone.set (sr, c.bass, c.mid, c.treble);
        power.setPresence (sr, 4500, (c.presence - 0.5) * 16.0);
        power.sagAmount = c.sag * 0.3f;
        gainS.setTarget (3.0f + 22.0f * c.gain);
        masterS.setTarget (0.4f + 2.0f * c.master);
    }
    void process (float* L, float* R, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = gainS.next(); power.drive = masterS.next();
            for (int ch = 0; ch < 2; ++ch)
            {
                float* s = ch == 0 ? L : R;
                float y = tight2.process (tight1.process (s[i], ch), ch);
                y = st1.process (y, g, ch);
                y = inter.process (y, ch);
                y = st2.process (y, 2.0f + g, ch);
                y = st3.process (y, 1.5f + g * 0.5f, ch);
                y = st4.process (y, 2.0f, ch);
                y = resonance.process (tone.process (y, ch), ch);
                s[i] = fizz.process (power.process (y * 0.7f, ch), ch) * 0.32f;
            }
        }
    }
private:
    Biquad tight1, tight2, inter, resonance, fizz; TriodeStage st1, st2, st3, st4; ToneStack tone; PowerStage power;
};

//==============================================================================
/** 6. Bass DI / Amp: band-split processor. The low band (< 220 Hz) stays clean with optional sag
    compression; the upper band gets the drive. Bands are recombined then shaped by a bass-voiced EQ. */
class BassDI final : public AmpModel
{
public:
    void prepare (double osRate) override
    {
        sr = osRate;
        inHp.highpass (sr, 28, 0.7);
        lp1.lowpass (sr, 220, 0.7071); lp2.lowpass (sr, 220, 0.7071);
        hp1.highpass (sr, 220, 0.7071); hp2.highpass (sr, 220, 0.7071);
        grit.negHardness = 1.8f; grit.bias = 0.15f; grit.prepare (sr, 6000);
        tone.bassHz = 60; tone.bassRange = 12; tone.midHz = 500; tone.midQ = 0.8; tone.midRange = 10; tone.trebleHz = 3000;
        lowComp.set (sr, 5.0f, 120.0f);
        power.classA = false; power.headroom = 2.0f; power.prepare (sr);
        gainS.prepare (sr, 20); masterS.prepare (sr, 20);
    }
    void reset() noexcept override
    {
        inHp.reset(); lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); grit.reset(); tone.reset(); air.reset();
        lowComp.reset(); power.reset(); gainS.snap(); masterS.snap();
    }
    void setControls (const AmpControls& c) noexcept override
    {
        tone.set (sr, c.bass, c.mid, c.treble);
        air.highShelf (sr, 6000, 0.7, (c.presence - 0.5) * 20.0);
        sag = c.sag;
        drive = c.gain;
        gainS.setTarget (1.0f + 45.0f * c.gain * c.gain);
        masterS.setTarget (0.3f + 1.4f * c.master);
    }
    void process (float* L, float* R, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = gainS.next(); power.drive = masterS.next();
            const float env = lowComp.process (0.5f * (L[i] + R[i]));
            const float comp = 1.0f / (1.0f + sag * 4.0f * env);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* s = ch == 0 ? L : R;
                const float x = inHp.process (s[i], ch);
                const float low = lp2.process (lp1.process (x, ch), ch);
                const float high = hp2.process (hp1.process (x, ch), ch);
                const float driven = grit.process (high, g, ch) * (0.4f + 0.4f * drive);
                const float cleanHigh = high * (1.0f - drive);
                float y = low * comp * 1.2f + driven + cleanHigh;
                y = air.process (tone.process (y, ch), ch);
                s[i] = power.process (y, ch) * 1.2f;
            }
        }
    }
private:
    Biquad inHp, lp1, lp2, hp1, hp2, air; TriodeStage grit; ToneStack tone; EnvelopeFollower lowComp; PowerStage power;
    float sag = 0.3f, drive = 0.3f;
};
} // namespace

std::unique_ptr<AmpModel> AmpModel::create (AmpType type)
{
    switch (type)
    {
        case AmpType::StudioClean:   return std::make_unique<StudioClean>();
        case AmpType::ClassAChime:   return std::make_unique<ClassAChime>();
        case AmpType::BritishCrunch: return std::make_unique<BritishCrunch>();
        case AmpType::HotRodLead:    return std::make_unique<HotRodLead>();
        case AmpType::ModernMetal:   return std::make_unique<ModernMetal>();
        case AmpType::BassDI:        return std::make_unique<BassDI>();
        default: break;
    }
    return std::make_unique<StudioClean>();
}
} // namespace pf
