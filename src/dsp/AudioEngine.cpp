#include "AudioEngine.h"

namespace pf
{
AudioEngine::AudioEngine()
{
    tunerBuffer.assign (16384, 0.0f);
}

void AudioEngine::prepare (double sampleRate, int maxBlockSize, const EngineParams& p)
{
    std::lock_guard<std::mutex> lock (builderMutex);
    sr = sampleRate; maxBlock = std::max (1, maxBlockSize);

    for (int i = 0; i < kNumSlots; ++i)
    {
        slots[(size_t) i].prepare (sr, maxBlock);
        slots[(size_t) i].buildNow (p.slots[i].type, p.slots[i]);
    }

    gate.prepareEffect (sr, maxBlock);
    gate.setParameter (NoiseGate::Attack, 0.5f); gate.setParameter (NoiseGate::Hold, 30.0f);
    gate.setParameter (NoiseGate::Hysteresis, 6.0f); gate.setParameter (NoiseGate::Range, -80.0f);
    gate.setParameter (NoiseGate::Threshold, p.gateThreshDb); gate.setParameter (NoiseGate::Release, p.gateReleaseMs);
    gate.reset();

    amp.setType (p.ampType); amp.setControls (p.amp); amp.setQuality (p.quality); amp.setEnabled (p.ampOn); amp.setTrimDb (p.ampTrimDb);
    amp.prepare (sr, maxBlock);
    cab.setType (p.cabType); cab.setMic (p.cabMic); cab.setLowCut (p.cabLowCut); cab.setHighCut (p.cabHighCut);
    cab.setLevelDb (p.cabLevelDb); cab.setMix (p.cabMix); cab.setEnabled (p.cabOn);
    cab.prepare (sr, maxBlock);

    for (auto* s : { &inGain, &outGain, &master, &eqMix, &bypassMix, &gateMix }) s->prepare (sr, 20);
    inGain.snapTo (dbToGain (p.inGainDb)); outGain.snapTo (dbToGain (p.outGainDb)); master.snapTo (dbToGain (p.masterDb));
    eqMix.snapTo (p.eqOn ? 1.0f : 0.0f); bypassMix.snapTo (p.bypass ? 1.0f : 0.0f); gateMix.snapTo (p.gateOn ? 1.0f : 0.0f);
    eqState[0] = 1.0e9f;
    eqLow.reset(); eqMid.reset(); eqHigh.reset();
    limGain = 1.0f; limRelease = (float) std::exp (-1.0 / (0.12 * sr));
    for (auto& d : bypassDelay) d.setMaxDelay (256);
    dryL.assign ((size_t) maxBlock + 8, 0.0f); dryR = dryL;

    activeRouting = routingWord.load(); pendingRouting = activeRouting; duck = 1.0f; duckState = 0;
    tunerFifo.reset();
    prepared = true;
}

void AudioEngine::syncSlots (const EngineParams& p)
{
    std::lock_guard<std::mutex> lock (builderMutex);
    if (! prepared) return;
    for (int i = 0; i < kNumSlots; ++i)
    {
        auto& s = slots[(size_t) i];
        s.collectGarbage();
        if (p.slots[i].type != s.builderType())
            s.publish (p.slots[i].type, p.slots[i]);
    }
}

int AudioEngine::readTuner (float* dest, int maxSamples) noexcept
{
    int s1, n1, s2, n2;
    tunerFifo.prepareToRead (maxSamples, s1, n1, s2, n2);
    if (n1 > 0) std::copy (tunerBuffer.begin() + s1, tunerBuffer.begin() + s1 + n1, dest);
    if (n2 > 0) std::copy (tunerBuffer.begin() + s2, tunerBuffer.begin() + s2 + n2, dest + n1);
    tunerFifo.finishedRead (n1 + n2);
    return n1 + n2;
}

void AudioEngine::processSlots (int from, int to, const Routing& r, float* L, float* R, int n,
                                const EngineParams& p, const ProcessContext& ctx) noexcept
{
    for (int pos = from; pos < to; ++pos)
    {
        const int s = r.order[(size_t) pos];
        slots[(size_t) s].process (L, R, n, p.slots[s], ctx);
    }
}

void AudioEngine::process (float* L, float* R, int n, int numInputs, const EngineParams& p, const ProcessContext& ctx) noexcept
{
    if (! prepared) { std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f); return; }

    // ---- input routing
    if (numInputs < 2) std::copy (L, L + n, R);
    else if (p.inMode == 1) std::copy (L, L + n, R);
    else if (p.inMode == 2) std::copy (R, R + n, L);
    else if (p.inMode == 3) for (int i = 0; i < n; ++i) { const float m = 0.5f * (L[i] + R[i]); L[i] = R[i] = m; }

    // ---- input gain, meter, tuner tap
    inGain.setTarget (dbToGain (p.inGainDb));
    float pk = 0.0f;
    int s1, n1, s2, n2;
    tunerFifo.prepareToWrite (n, s1, n1, s2, n2);
    int written = 0;
    for (int i = 0; i < n; ++i)
    {
        const float g = inGain.next();
        L[i] *= g; R[i] *= g;
        pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
        const float mono = 0.5f * (L[i] + R[i]);
        if (written < n1)            tunerBuffer[(size_t) (s1 + written)] = mono;
        else if (written < n1 + n2)  tunerBuffer[(size_t) (s2 + written - n1)] = mono;
        ++written;
    }
    tunerFifo.finishedWrite (std::min (written, n1 + n2));
    inPeak.store (std::max (inPeak.load (std::memory_order_relaxed), pk), std::memory_order_relaxed);

    std::copy (L, L + n, dryL.begin()); std::copy (R, R + n, dryR.begin()); // for global bypass

    // ---- noise gate
    gate.setParameter (NoiseGate::Threshold, p.gateThreshDb);
    gate.setParameter (NoiseGate::Release, p.gateReleaseMs);
    gateMix.setTarget (p.gateOn ? 1.0f : 0.0f);
    if (p.gateOn || gateMix.current > 1.0e-4f)
    {
        float* tmpL = dryL.data(); (void) tmpL;
        // gate in place, then blend with ungated by the on/off ramp
        for (int i = 0; i < n; i += 64)
        {
            const int m = std::min (64, n - i);
            float bl[64], br[64];
            std::copy (L + i, L + i + m, bl); std::copy (R + i, R + i + m, br);
            gate.process (L + i, R + i, m, ctx);
            for (int k = 0; k < m; ++k)
            {
                const float g = gateMix.next();
                L[i + k] = bl[k] + (L[i + k] - bl[k]) * g;
                R[i + k] = br[k] + (R[i + k] - br[k]) * g;
            }
        }
        gateGain.store (gate.currentGain(), std::memory_order_relaxed);
    }
    else gateGain.store (1.0f, std::memory_order_relaxed);

    // ---- routing changes are applied under a short duck to avoid clicks
    const uint32_t wanted = routingWord.load (std::memory_order_acquire);
    if (wanted != activeRouting && duckState == 0) { pendingRouting = wanted; duckState = 1; }
    const Routing r = Routing::unpack (activeRouting);

    // ---- pre slots, amp, cab, post slots
    processSlots (0, r.numPre, r, L, R, n, p, ctx);

    amp.setType (p.ampType); amp.setControls (p.amp); amp.setTrimDb (p.ampTrimDb);
    amp.setEnabled (p.ampOn); amp.setQuality (p.quality);
    amp.process (L, R, n);

    cab.setType (p.cabType); cab.setMic (p.cabMic); cab.setLowCut (p.cabLowCut); cab.setHighCut (p.cabHighCut);
    cab.setLevelDb (p.cabLevelDb); cab.setMix (p.cabMix); cab.setEnabled (p.cabOn);
    cab.process (L, R, n);

    processSlots (r.numPre, kNumSlots, r, L, R, n, p, ctx);

    // ---- master EQ
    if (eqState[0] != p.eqLow || eqState[1] != p.eqMid || eqState[2] != p.eqMidFreq || eqState[3] != p.eqHigh)
    {
        eqState[0] = p.eqLow; eqState[1] = p.eqMid; eqState[2] = p.eqMidFreq; eqState[3] = p.eqHigh;
        eqLow.lowShelf (sr, 100, 0.7, p.eqLow);
        eqMid.peak (sr, p.eqMidFreq, 0.9, p.eqMid);
        eqHigh.highShelf (sr, 6000, 0.7, p.eqHigh);
    }
    eqMix.setTarget (p.eqOn ? 1.0f : 0.0f);
    outGain.setTarget (dbToGain (p.outGainDb));
    master.setTarget (dbToGain (p.masterDb) * p.midiVolume);
    bypassMix.setTarget (p.bypass ? 1.0f : 0.0f);

    const float ceiling = dbToGain (p.limCeilingDb);
    const int lat = amp.latencyFor (amp.activeQuality());
    float opk = 0.0f, minGain = 1.0f;
    bool bad = false;
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], rr = R[i];
        const float em = eqMix.next();
        if (em > 1.0e-4f)
        {
            const float el = eqHigh.process (eqMid.process (eqLow.process (l, 0), 0), 0);
            const float er = eqHigh.process (eqMid.process (eqLow.process (rr, 1), 1), 1);
            l += (el - l) * em; rr += (er - rr) * em;
        }
        const float og = outGain.next();
        l *= og; rr *= og;

        if (p.limOn)
        {
            const float peak = std::max (std::abs (l), std::abs (rr));
            const float target = peak > ceiling ? ceiling / peak : 1.0f;
            limGain = target < limGain ? target : target + limRelease * (limGain - target);
            l *= limGain; rr *= limGain;
            l = clampf (l, -ceiling, ceiling); rr = clampf (rr, -ceiling, ceiling);
            minGain = std::min (minGain, limGain);
        }

        const float mg = master.next();
        l *= mg; rr *= mg;

        if (p.outMode == 1) { const float m = 0.5f * (l + rr); l = rr = m; }

        // global bypass: latency-matched dry signal
        bypassDelay[0].push (dryL[(size_t) i]); bypassDelay[1].push (dryR[(size_t) i]);
        const float bm = bypassMix.next();
        if (bm > 1.0e-4f)
        {
            const float bl = lat > 0 ? bypassDelay[0].readInt (lat + 1) : dryL[(size_t) i];
            const float br = lat > 0 ? bypassDelay[1].readInt (lat + 1) : dryR[(size_t) i];
            l += (bl - l) * bm; rr += (br - rr) * bm;
        }

        // routing duck
        if (duckState == 1) { duck -= 1.0f / 128.0f; if (duck <= 0.0f) { duck = 0.0f; duckState = 2; activeRouting = pendingRouting; } }
        else if (duckState == 2) { duck += 1.0f / 128.0f; if (duck >= 1.0f) { duck = 1.0f; duckState = 0; } }
        l *= duck; rr *= duck;

        if (! std::isfinite (l) || ! std::isfinite (rr)) { bad = true; l = rr = 0.0f; }
        l = clampf (l, -4.0f, 4.0f); rr = clampf (rr, -4.0f, 4.0f); // last-resort protection
        L[i] = l; R[i] = rr;
        opk = std::max (opk, std::max (std::abs (l), std::abs (rr)));
    }
    outPeak.store (std::max (outPeak.load (std::memory_order_relaxed), opk), std::memory_order_relaxed);
    limiterGrDb.store (-gainToDb (minGain), std::memory_order_relaxed);

    if (bad)
    {
        // a non-finite sample escaped somewhere: clear every state so the rig recovers immediately
        for (auto& s : slots) s.resetAudio();
        amp.reset(); cab.reset(); eqLow.reset(); eqMid.reset(); eqHigh.reset(); gate.reset();
        std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
    }
}
} // namespace pf
