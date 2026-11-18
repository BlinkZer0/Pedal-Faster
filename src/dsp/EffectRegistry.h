#pragma once
// The effect registry: the single source of truth for pedal type IDs, display names, categories,
// parameter specifications and factories. The ORDER of entries is part of the saved-state format
// (it is the index used by the slot "type" automation parameter) - only ever append new types.

#include "EffectProcessor.h"
#include <functional>

namespace pf
{
struct EffectInfo
{
    int index = 0;                 // stable index, 0 == empty slot
    std::string id;                // stable JSON id, e.g. "mid_overdrive"
    std::string name;              // display name
    std::string category;          // "Dynamics", "Gain", ...
    bool hasMix = false;           // slot wet/dry knob is meaningful
    float defaultMix = 1.0f;
    std::vector<ParamSpec> params; // at most kMaxEffectParams
    std::function<std::unique_ptr<Effect>()> create;

    int findParam (const std::string& key) const noexcept
    {
        for (size_t i = 0; i < params.size(); ++i) if (params[i].key == key) return (int) i;
        return -1;
    }
};

class EffectRegistry
{
public:
    static const EffectRegistry& get();

    const std::vector<EffectInfo>& all() const noexcept { return infos; }
    int size() const noexcept { return (int) infos.size(); }
    const EffectInfo& at (int index) const noexcept { return infos[(size_t) std::clamp (index, 0, size() - 1)]; }
    const EffectInfo* find (const std::string& id) const noexcept;
    int indexOf (const std::string& id) const noexcept { auto* i = find (id); return i ? i->index : -1; }
    std::vector<std::string> categories() const;

private:
    EffectRegistry();
    std::vector<EffectInfo> infos;
};
} // namespace pf
