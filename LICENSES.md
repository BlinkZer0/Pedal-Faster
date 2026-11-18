# Licenses

## Pedal Faster

The Pedal Faster source code, DSP algorithms, amp and cabinet voicings, UI and all 250 factory presets are original work in this repository. No third-party presets, impulse responses, samples, artwork or Guitar Rig code are included. The amp models are original voicings, not measured hardware captures. The cabinets are filter-based voicings, not copied impulse responses.

Choose a license for your own code before you distribute it. Because the plugin links JUCE, that choice is constrained by the JUCE terms below.

## Third-party components

| Component | Version | License | Use |
|---|---|---|---|
| [JUCE](https://juce.com) | 9.0.5 | Dual: **AGPLv3** or the commercial **JUCE 9 licence** | Framework (audio, GUI, plugin wrappers). Linked into the plugin and app. |
| Steinberg VST3 SDK (bundled inside JUCE) | as shipped with JUCE 9.0.5 | **MIT** (Steinberg, 2025) | VST3 plugin interfaces. Linked into the VST3. |
| [pluginval](https://github.com/Tracktion/pluginval) | 1.0.4 | GPLv3 | Validation tool only. Downloaded at test time and **not** distributed. |
| Steinberg VST3 SDK `validator` | upstream `master` | MIT | Validation tool only. Built at test time and **not** distributed. |

### What this means for distribution (please verify yourself)

JUCE 9 modules are licensed **either** under the AGPLv3 **or** under the commercial JUCE 9 licence ([juce.com/legal/juce-9-licence](https://juce.com/legal/juce-9-licence/)).

- **Open source release:** you can distribute Pedal Faster under the AGPLv3. You must then publish the complete corresponding source, including your own code, under AGPLv3-compatible terms.
- **Closed source or commercial release:** you need a JUCE licence tier that covers your use. Check the current JUCE licensing FAQ for revenue thresholds and tiers.

The VST3 SDK is MIT licensed, so its copyright notice must accompany binary distributions. "VST" is a trademark of Steinberg Media Technologies GmbH; follow the VST3 usage guidelines (`VST3_Usage_Guidelines.pdf` in JUCE's `VST3_SDK` folder) if you use the VST logo.

This file is a summary, not legal advice. Read the actual license texts in the JUCE checkout (`%LOCALAPPDATA%\PedalFaster\deps\juce-src\LICENSE.md` and `...\VST3_SDK\LICENSE.txt`) before distributing.
