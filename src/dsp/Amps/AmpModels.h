#pragma once
// Six original amplifier voicings. Each model is its own class with its own topology (number of gain
// stages, transfer curves, coupling/Miller filters, tone-stack placement and power section). All models
// run at the oversampled rate supplied by AmpSection.

#include "../DspCommon.h"
#include <memory>
#include <string>
#include <vector>

namespace pf
{
enum class AmpType { StudioClean = 0, ClassAChime, BritishCrunch, HotRodLead, ModernMetal, BassDI, Count };

struct AmpControls
{
    float gain = 0.5f, bass = 0.5f, mid = 0.5f, treble = 0.5f, presence = 0.5f, master = 0.5f, sag = 0.3f;
    bool operator!= (const AmpControls& o) const noexcept
    {
        return gain != o.gain || bass != o.bass || mid != o.mid || treble != o.treble || presence != o.presence
            || master != o.master || sag != o.sag;
    }
};

const std::vector<std::string>& ampTypeIds();   // "studio_clean", ...
const std::vector<std::string>& ampTypeNames(); // "Studio Clean", ...
int ampTypeIndex (const std::string& id);

//==============================================================================
/** Asymmetric triode-like gain stage: soft positive clipping, earlier/harder negative clipping,
    bias offset (even harmonics), output DC removed by the following coupling high-pass. */
struct TriodeStage
{
    float gain = 2.0f, bias = 0.1f, negHardness = 1.6f, outScale = 1.0f;
    OnePole miller; DCBlocker coupling; float biasOut = 0.0f;

    void prepare (double osRate, float millerHz)
    {
        miller.setCutoff (osRate, millerHz);
        coupling.prepare (osRate);
        update();
    }
    void update() noexcept { biasOut = shape (bias); }
    void reset() noexcept { miller.reset(); coupling.reset(); }
    inline float shape (float u) const noexcept { return u >= 0.0f ? fastTanh (u) : fastTanh (u * negHardness) / negHardness; }
    inline float process (float x, float g, int ch) noexcept
    {
        const float u = miller.lowpass (x * g, ch) + bias;
        return coupling.process (shape (u) - biasOut, ch) * outScale;
    }
};

/** Three-band tone stack with model-specific corner frequencies and ranges. */
struct ToneStack
{
    double bassHz = 100, midHz = 650, midQ = 0.8, trebleHz = 2500;
    double bassRange = 12, midRange = 10, midOffset = 0, trebleRange = 12;
    Biquad low, mid, high;
    void reset() noexcept { low.reset(); mid.reset(); high.reset(); }
    void set (double sr, float b, float m, float t)
    {
        low.lowShelf (sr, bassHz, 0.7, (b - 0.5) * 2.0 * bassRange);
        mid.peak (sr, midHz, midQ, midOffset + (m - 0.5) * 2.0 * midRange);
        high.highShelf (sr, trebleHz, 0.7, (t - 0.5) * 2.0 * trebleRange);
    }
    inline float process (float x, int ch) noexcept { return high.process (mid.process (low.process (x, ch), ch), ch); }
};

/** Power section: master drives a push-pull (symmetric) or single-ended (asymmetric) output stage with
    supply sag (signal-dependent compression) and a presence shelf in the feedback path. */
struct PowerStage
{
    bool classA = false;
    float sagAmount = 0.3f, drive = 1.0f, headroom = 1.0f;
    EnvelopeFollower supply; Biquad presence;
    void prepare (double osRate) { supply.set (osRate, 8.0f, 180.0f); }
    void reset() noexcept { supply.reset(); presence.reset(); }
    void setPresence (double sr, double hz, double db) { presence.highShelf (sr, hz, 0.7, db); }
    inline float process (float x, int ch) noexcept
    {
        const float env = ch == 0 ? supply.process (x * drive) : supply.env;
        const float sagGain = 1.0f / (1.0f + sagAmount * 2.5f * env);
        float u = x * drive * sagGain / headroom;
        float y;
        if (classA) y = (u >= 0.0f ? fastTanh (u) : fastTanh (u * 1.7f) / 1.7f + 0.0f);
        else        y = fastTanh (u);
        y *= headroom * (1.0f - 0.25f * sagAmount * env);
        return presence.process (y, ch);
    }
};

//==============================================================================
class AmpModel
{
public:
    virtual ~AmpModel() = default;
    virtual void prepare (double osRate) = 0;
    virtual void reset() noexcept = 0;
    /** Called at block rate when controls change. */
    virtual void setControls (const AmpControls& c) noexcept = 0;
    /** Processes oversampled stereo audio in place. */
    virtual void process (float* L, float* R, int n) noexcept = 0;

    static std::unique_ptr<AmpModel> create (AmpType type);

protected:
    double sr = 88200.0;
    Smoother gainS, masterS;
};
} // namespace pf
