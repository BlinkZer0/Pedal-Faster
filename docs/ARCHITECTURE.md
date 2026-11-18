# Architecture

## Signal flow

```
Input bus (mono/stereo) → input mode (stereo / L / R / sum) → Input gain ──► tuner FIFO, input meter
→ Noise gate (global)
→ Pre-amp slots (0–8, in routing order)
→ Amp section   [oversampled 1x/2x/4x; model crossfade; latency-matched bypass; output trim]
→ Cabinet       [filter voicing or convolution IR; mic, low/high cut, level, wet/dry]
→ Post-amp slots
→ Master EQ (low shelf, sweepable mid, high shelf)
→ Preset output level → Output limiter → Master volume (× MIDI CC7)
→ Mono/Stereo → global bypass crossfade (dry signal delayed by the reported latency) → output meter
```

`AudioEngine` (`src/dsp/AudioEngine.*`) owns all of this. `PedalFasterProcessor` turns host parameters into an `EngineParams` snapshot every block and calls `AudioEngine::process` in chunks no larger than the prepared block size. That means oversized offline blocks are fine.

## Modules

| Area | Files | Notes |
|---|---|---|
| DSP primitives | `dsp/DspCommon.h` | Biquads (RBJ), TPT SVF, one-poles, DC blockers, delay lines (linear/Hermite), allpasses, LFOs, envelope followers, ADAA waveshapers, smoothers. JUCE-free and portable. |
| Pedal interface | `dsp/EffectProcessor.h` | `Effect` base class (`prepare / reset / setParameter / process / hasTail`) and `ParamSpec`, which maps normalised ↔ real units and handles formatting. |
| Registry | `dsp/EffectRegistry.*` | The single source of truth for the 30 effect types: stable ids, names, categories, parameter specs and factories. **Entry order is part of saved state; only append.** |
| Pedals | `dsp/effects/*.h` | One class per algorithm, grouped by family. |
| Amps | `dsp/Amps/AmpModels.*`, `AmpSection.*` | Six model classes plus oversampling, switching and bypass. |
| Cabinet | `dsp/Cabinets/Cabinet.*` | Five filter voicings, Direct, and the custom IR (JUCE `dsp::Convolution`). |
| Chain | `dsp/SignalChain.*` | `EffectSlot` (lock-free effect swaps) and `Routing` (order and pre/post split, packed into one atomic word). |
| Presets | `presets/*` | Model, JSON serializer and validator, factory library, user library manager. |
| UI | `PluginEditor.*`, `ui/*` | Native JUCE components, with a custom `Theme` LookAndFeel. |

## Real-time safety

The audio callback never allocates, frees, locks, reads files or waits.

- **Parameters** are read from APVTS atomics (`getRawParameterValue`) once per block. Effects receive real-unit values only when they change and smooth internally.
- **Effect type changes:** a non-audio *builder* creates the new `Effect`, prepares it at the current sample rate, applies its parameters and publishes it through one `std::atomic<Swap*>` per slot. The builder can be the 30 Hz message-thread timer (`runMaintenance`), `setSlotType`, `applyPreset` or `setStateInformation`. The audio thread takes it with `exchange(nullptr)` and crossfades from the old instance over 15 ms. When the fade finishes, the old instance travels back in the same `Swap` shell through a lock-free SPSC queue, and the builder deletes it. A `std::mutex` serialises builders only, never the audio thread.
- **Mismatched parameters:** until a slot's swap arrives, the slot's normalised parameters already describe the *new* effect. They are therefore only applied when the host-side type equals the running instance's type, so the old effect never receives the new effect's values.
- **Routing changes** (reorder, pre/post) go through one atomic `uint32_t`. The engine ducks the output for about 3 ms (128 samples down, 128 up) while switching order.
- **Amp model switches** crossfade over 20 ms. **Quality (oversampling) switches** fade out for one block, swap the oversampler and fade back in. Each quality level has its own preallocated oversampler and model set.
- **Cabinet changes** crossfade filter voicings. IR loads use JUCE's wait-free background loader.
- **Bypass:** slot bypass is a ramped mix. Delays and reverbs keep processing with their input muted, so tails ring out ("spill-over"). They stop processing after one second of silence. Non-tail effects stop processing when fully bypassed and are reset when re-enabled.
- **Protection:** denormals are flushed (`ScopedNoDenormals` and explicit flushing in feedback paths), feedback is clamped or soft-limited, and the output is clamped to ±4.0 as a last resort. If a non-finite sample ever reaches the output, every DSP state is reset within that block and the block is muted.

## Latency

The only latency source is the amp's polyphase IIR oversampler (JUCE `dsp::Oversampling` with `useIntegerLatency = true`), so the value is an exact integer:

| Quality | Oversampling | Reported latency at 48 kHz |
|---|---|---|
| Eco | none | 0 samples |
| Normal | 2x | 4 samples |
| High | 4x | 6 samples |

`setLatencySamples` is updated from the message thread when quality changes. Amp bypass and global bypass delay the dry signal by the same amount, so bypass is sample-identical to the input shifted by the reported latency (this is tested). Pedal waveshapers use first-order ADAA instead of oversampling, which adds no latency. Pitch effects have inherent (musical) delay that isn't compensated, as with hardware.

## Host automation

All parameter IDs are stable (`src/Parameters.h`); never rename or remove one.

- **Global:** input, gate, amp, cabinet, master EQ, output, limiter, quality, tempo, macros.
- **Slots: automation follows the slot, not the effect.** Each slot exposes `sN_type`, `sN_on`, `sN_mix` and `sN_p1 … sN_p10`, all stored normalised 0..1. The meaning of `sN_pK` is the K-th parameter of the pedal currently in slot N, as given by the registry's `ParamSpec`. The host text shows that pedal's real value and unit (for example `-52.0 dB`). Changing a pedal type resets that slot's parameters to the new pedal's defaults. Saved projects store the type too, so they always reload consistently.
- Type selectors (`sN_type`, `amp_type`, `cab_type`) and some I/O settings are marked non-automatable.

## Macros

Four host parameters (`macro_drive`, `macro_tone`, `macro_space`, `macro_output`) default to 0.5 (neutral). Each preset either lists explicit mappings or gets them derived from its chain:

| Macro | Derived destinations |
|---|---|
| Drive | amp gain, plus the first gain pedal's drive/gain/sustain |
| Tone | amp treble and presence, cabinet high cut (master EQ high when the amp is off) |
| Space | wet/dry of every delay and reverb (modulation mix if there are none) |
| Output | preset output level |

The offset is applied in the normalised domain (`dest = clamp(base + (macro − 0.5) · 2 · amount)`), so the stored values stay untouched. Resolved mappings are double-buffered for the audio thread.

## MIDI

| Message | Action |
|---|---|
| CC 11 (expression) | Wah position of every wah slot |
| CC 7 | Master volume multiplier |
| CC 20–23 | Macros Drive / Tone / Space / Output (written to the host parameters) |
| CC 80 / CC 81 (value ≥ 64) | Next / previous preset |
| Program Change n | Load preset n in bank order |

## State

`getStateInformation` writes the APVTS tree plus a `RIG` child holding the routing, preset id and name, the modified flag, the macro mappings and the **embedded custom IR** (base64 float samples, sample rate, channel count). A project therefore restores exactly, even if the factory library changes or the original preset or IR files are gone. Unknown or garbage state is ignored safely.

## Threads

| Thread | Work |
|---|---|
| Audio | `processBlock` → `AudioEngine::process` |
| Message | UI, 30 Hz maintenance timer (slot building, garbage collection, MIDI-triggered actions, latency updates), preset I/O, tuner analysis |
| JUCE convolution background thread | IR preparation |
