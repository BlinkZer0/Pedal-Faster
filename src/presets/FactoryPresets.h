#pragma once
#include "Preset.h"
#include <vector>

namespace pf
{
struct FactoryBank
{
    std::string name;
    bool core = true;   // one of the 20 specified banks (false: additional electronic banks)
};

/** The banks in display order. */
const std::vector<FactoryBank>& factoryBanks();

/** All factory presets (built once, immutable). Includes level calibration offsets. */
const std::vector<Preset>& factoryPresets();

/** The authored presets without the generated loudness-calibration offsets (used by the calibration tool). */
std::vector<Preset> buildFactoryPresetsUncalibrated();
} // namespace pf
