#pragma once
#include "../EffectProcessor.h"

namespace pf
{
//==============================================================================
/** Noise gate: peak detector -> open/hold/close state machine with hysteresis -> smoothed gain. */
class NoiseGate final : public Effect
{
public:
    enum { Threshold, Attack, Hold, Release, Hysteresis, Range };

    void setParameter (int i, float v) noexcept override
    {
        switch (i)
        {
            case Threshold:  thresholdDb = v; break;
            case Attack:     attackMs = v; break;
            case Hold:       holdMs = v; break;
            case Release:    releaseMs = v; break;
            case Hysteresis: hysteresisDb = v; break;
            case Range:      rangeDb = v; break;
            default: break;
        }
        updateCoeffs();
    }

    void reset() noexcept override { detector.reset(); gain = open ? 1.0f : floorGain; holdCounter = 0; }

    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        const float openLevel  = dbToGain (thresholdDb);
        const float closeLevel = dbToGain (thresholdDb - hysteresisDb);
        for (int i = 0; i < n; ++i)
        {
            const float in = std::max (std::abs (L[i]), std::abs (R[i]));
            const float env = detector.process (in);

            if (env >= openLevel)          { open = true; holdCounter = holdSamples; }
            else if (env < closeLevel)     { if (holdCounter > 0) --holdCounter; else open = false; }
            else if (open && holdCounter > 0) --holdCounter;

            const float target = open ? 1.0f : floorGain;
            const float c = target > gain ? attackCoeff : releaseCoeff;
            gain = target + c * (gain - target);
            L[i] *= gain; R[i] *= gain;
        }
    }

    float currentGain() const noexcept { return gain; }

protected:
    void prepare (double, int) override { updateCoeffs(); detector.set (sr, 0.2f, 25.0f); reset(); }

private:
    void updateCoeffs() noexcept
    {
        attackCoeff  = (float) std::exp (-1.0 / (std::max (0.05f, attackMs) * 0.001 * sr));
        releaseCoeff = (float) std::exp (-1.0 / (std::max (1.0f, releaseMs) * 0.001 * sr));
        holdSamples  = (int) (holdMs * 0.001 * sr);
        floorGain    = dbToGain (rangeDb);
    }

    float thresholdDb = -55, attackMs = 1, holdMs = 40, releaseMs = 120, hysteresisDb = 6, rangeDb = -80;
    float attackCoeff = 0, releaseCoeff = 0, floorGain = 0.0001f, gain = 0.0f;
    int holdSamples = 0, holdCounter = 0;
    bool open = false;
    EnvelopeFollower detector;
};

//==============================================================================
/** Feed-forward stereo-linked compressor with soft knee, log-domain smoothing and makeup gain. */
class Compressor final : public Effect
{
public:
    enum { Threshold, Ratio, Attack, Release, Makeup, Knee };

    void setParameter (int i, float v) noexcept override
    {
        switch (i)
        {
            case Threshold: thresholdDb = v; break;
            case Ratio:     ratio = std::max (1.0f, v); break;
            case Attack:    attackMs = v; break;
            case Release:   releaseMs = v; break;
            case Makeup:    makeup.setTarget (dbToGain (v)); break;
            case Knee:      kneeDb = v; break;
            default: break;
        }
        updateCoeffs();
    }

    void reset() noexcept override { grDb = 0.0f; makeup.snap(); }

    void process (float* L, float* R, int n, const ProcessContext&) noexcept override
    {
        const float slope = 1.0f - 1.0f / ratio;
        for (int i = 0; i < n; ++i)
        {
            const float peak = std::max (std::abs (L[i]), std::abs (R[i]));
            const float inDb = gainToDb (peak);
            float over = inDb - thresholdDb;
            float targetGr;
            if (kneeDb > 0.0f && std::abs (over) <= kneeDb * 0.5f)
            {
                const float k = over + kneeDb * 0.5f;
                targetGr = slope * k * k / (2.0f * kneeDb);
            }
            else
                targetGr = over > 0.0f ? slope * over : 0.0f;

            const float c = targetGr > grDb ? attackCoeff : releaseCoeff;
            grDb = targetGr + c * (grDb - targetGr);
            const float g = dbToGain (-grDb) * makeup.next();
            L[i] *= g; R[i] *= g;
        }
    }

    float gainReductionDb() const noexcept { return grDb; }

protected:
    void prepare (double, int) override { makeup.prepare (sr, 30.0f); updateCoeffs(); reset(); }

private:
    void updateCoeffs() noexcept
    {
        attackCoeff  = (float) std::exp (-1.0 / (std::max (0.05f, attackMs) * 0.001 * sr));
        releaseCoeff = (float) std::exp (-1.0 / (std::max (1.0f, releaseMs) * 0.001 * sr));
    }
    float thresholdDb = -20, ratio = 4, attackMs = 10, releaseMs = 150, kneeDb = 6;
    float attackCoeff = 0, releaseCoeff = 0, grDb = 0;
    Smoother makeup;
};
} // namespace pf
