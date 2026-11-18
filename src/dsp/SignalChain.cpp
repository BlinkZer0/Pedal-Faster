#include "SignalChain.h"
#include <sstream>

namespace pf
{
EffectSlot::~EffectSlot()
{
    if (auto* p = pending.exchange (nullptr)) { delete p->incoming; delete p->outgoing; delete p; }
    collectGarbage();
    if (activeShell) { delete activeShell->outgoing; delete activeShell->incoming; delete activeShell; }
    delete current; delete fading;
}

void EffectSlot::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate; maxBlock = maxBlockSize;
    dryL.assign ((size_t) maxBlock + 8, 0.0f); dryR = dryL; fadeL = dryL; fadeR = dryL;
    enabledRamp.prepare (sr, 8); mixS.prepare (sr, 20);
    fadeInc = (float) (1.0 / (0.015 * sr));
}

std::unique_ptr<Effect> EffectSlot::makeEffect (int type, const SlotSettings& s) const
{
    const auto& info = EffectRegistry::get().at (type);
    if (type <= 0 || ! info.create) return nullptr;
    auto e = info.create();
    e->prepareEffect (sr, maxBlock);
    for (size_t i = 0; i < info.params.size(); ++i)
        e->setParameter ((int) i, info.params[i].toReal (s.norm[i]));
    e->reset();
    return e;
}

void EffectSlot::buildNow (int type, const SlotSettings& s)
{
    if (auto* p = pending.exchange (nullptr)) { delete p->incoming; delete p->outgoing; delete p; }
    collectGarbage();
    if (activeShell) { delete activeShell->outgoing; delete activeShell->incoming; delete activeShell; activeShell = nullptr; }
    delete current; delete fading;
    fading = nullptr; fadePos = 1.0f;
    current = makeEffect (type, s).release();
    currentType = current ? type : 0;
    publishedType = type;
    for (auto& v : lastNorm) v = -1.0f;
    enabledRamp.snapTo (s.enabled ? 1.0f : 0.0f);
    mixS.snapTo (s.mix);
    needsReset = false; silentSamples = 0;
}

void EffectSlot::publish (int type, const SlotSettings& s)
{
    auto* swap = new Swap();
    swap->incoming = makeEffect (type, s).release();
    swap->type = type;
    if (auto* old = pending.exchange (swap)) { delete old->incoming; delete old; } // never consumed
    publishedType = type;
}

void EffectSlot::collectGarbage()
{
    Swap* s = nullptr;
    while (retired.pop (s)) { delete s->outgoing; delete s->incoming; delete s; }
}

void EffectSlot::applyParams (Effect* e, int type, const SlotSettings& s, bool force) noexcept
{
    // only apply when the host-side type matches the running instance: during a pending swap the
    // parameter values already describe the new effect and must not be fed to the old one
    if (e == nullptr || type != currentType) return;
    const auto& info = EffectRegistry::get().at (type);
    for (size_t i = 0; i < info.params.size(); ++i)
        if (force || s.norm[i] != lastNorm[i])
        {
            lastNorm[i] = s.norm[i];
            e->setParameter ((int) i, info.params[i].toReal (s.norm[i]));
        }
}

void EffectSlot::resetAudio() noexcept
{
    if (current) current->reset();
    if (fading) fading->reset();
    silentSamples = 0;
}

void EffectSlot::finishFade() noexcept
{
    if (activeShell != nullptr)
    {
        activeShell->outgoing = fading;
        activeShell->incoming = nullptr;
        if (retired.push (activeShell)) { activeShell = nullptr; fading = nullptr; }
        // if the queue is full the instance stays alive (silent) and is retried next block
    }
    else if (fading == nullptr) { /* nothing */ }
    if (fading == nullptr) fadePos = 1.0f;
}

void EffectSlot::process (float* L, float* R, int n, const SlotSettings& s, const ProcessContext& ctx) noexcept
{
    // ---- adopt a newly built effect (wait-free)
    if (fading == nullptr || fadePos >= 1.0f)
    {
        if (fading != nullptr) finishFade();
        if (fading == nullptr)
            if (Swap* sw = pending.exchange (nullptr))
            {
                fading = current; fadingType = currentType;
                current = sw->incoming; currentType = current ? sw->type : 0;
                sw->incoming = nullptr;
                activeShell = sw;           // carries 'fading' back to the builder when the fade ends
                fadePos = fading != nullptr ? 0.0f : 1.0f;
                if (fading == nullptr) finishFade();
                for (auto& v : lastNorm) v = -1.0f;
                needsReset = false;
            }
    }

    applyParams (current, s.type, s, false);

    const auto& info = EffectRegistry::get().at (currentType);
    mixS.setTarget ((current && info.hasMix) ? s.mix : 1.0f);
    enabledRamp.setTarget (s.enabled ? 1.0f : 0.0f);

    if (current == nullptr && fading == nullptr) return; // empty slot: pass-through

    const bool tail = current != nullptr && current->hasTail();
    const bool fullyOff = ! s.enabled && enabledRamp.current < 1.0e-4f;
    if (fullyOff && fading == nullptr)
    {
        if (! tail || silentSamples > (int) sr) { needsReset = true; enabledRamp.snapTo (0.0f); return; }
    }
    if (needsReset && s.enabled) { if (current) current->reset(); needsReset = false; silentSamples = 0; }

    std::copy (L, L + n, dryL.begin()); std::copy (R, R + n, dryR.begin());
    if (fading != nullptr) { std::copy (L, L + n, fadeL.begin()); std::copy (R, R + n, fadeR.begin()); }

    if (current != nullptr)
    {
        if (tail)
        {
            Smoother r = enabledRamp; // feed input only while enabled so the tail rings out on bypass
            for (int i = 0; i < n; ++i) { const float g = r.next(); L[i] *= g; R[i] *= g; }
        }
        current->process (L, R, n, ctx);
    }

    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float m = mixS.next(), e = enabledRamp.next();
        const float d0 = dryL[(size_t) i], d1 = dryR[(size_t) i];
        float outL = d0, outR = d1;
        if (current != nullptr)
        {
            if (tail)
            {
                outL = d0 * (1.0f - m * e) + L[i] * m;
                outR = d1 * (1.0f - m * e) + R[i] * m;
                peak = std::max (peak, std::max (std::abs (L[i]), std::abs (R[i])));
            }
            else
            {
                outL = d0 + (L[i] - d0) * m * e;
                outR = d1 + (R[i] - d1) * m * e;
            }
        }
        L[i] = outL; R[i] = outR;
    }

    if (fading != nullptr)
    {
        fading->process (fadeL.data(), fadeR.data(), n, ctx);
        const auto& finfo = EffectRegistry::get().at (fadingType);
        const float fmix = finfo.hasMix ? mixS.current : 1.0f;
        const float fe = enabledRamp.current;
        for (int i = 0; i < n; ++i)
        {
            fadePos = std::min (1.0f, fadePos + fadeInc);
            const float a = std::sin (fadePos * kPi * 0.5f), b = std::cos (fadePos * kPi * 0.5f);
            const float d0 = dryL[(size_t) i], d1 = dryR[(size_t) i];
            const float oldL = d0 + (fadeL[(size_t) i] - d0) * fmix * fe;
            const float oldR = d1 + (fadeR[(size_t) i] - d1) * fmix * fe;
            L[i] = L[i] * a + oldL * b;
            R[i] = R[i] * a + oldR * b;
        }
        if (fadePos >= 1.0f) finishFade();
    }

    if (tail && ! s.enabled) silentSamples = peak < 1.0e-5f ? silentSamples + n : 0;
    else silentSamples = 0;
}

//==============================================================================
std::string Routing::toString() const
{
    std::ostringstream o;
    for (int i = 0; i < kNumSlots; ++i)
    {
        if (i == numPre) o << '|';
        else if (i > 0) o << ',';
        o << order[(size_t) i];
    }
    if (numPre == kNumSlots) o << '|';
    return o.str();
}

Routing Routing::fromString (const std::string& s)
{
    Routing r; Routing out; int idx = 0; int pre = -1;
    std::string num;
    auto flush = [&] { if (! num.empty() && idx < kNumSlots) out.order[(size_t) idx++] = std::atoi (num.c_str()); num.clear(); };
    for (char c : s)
    {
        if (c >= '0' && c <= '9') num += c;
        else if (c == ',') flush();
        else if (c == '|') { flush(); pre = idx; }
    }
    flush();
    out.numPre = pre < 0 ? idx : pre;
    if (idx != kNumSlots || ! out.isValid()) return r;
    return out;
}
} // namespace pf
