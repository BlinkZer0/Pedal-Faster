# Validation record

Build: JUCE 9.0.5, MSVC (Visual Studio 2026 v18.12), Windows 11 x64, Release. Date: 2026-11-17.

## Results

| Check | Result |
|---|---|
| VST3 + Standalone + test runner build | ✅ no errors, no compiler warnings in project sources (`/W3`) |
| Automated tests (`scripts/test-windows.ps1`) | ✅ **6,815 checks passed, 0 failed** (DSP, Presets, Integration, acoustic regression) |
| Steinberg VST3 SDK validator | ✅ **47 tests passed, 0 failed**, including processing at 22.05 kHz – 1.23 MHz |
| pluginval 1.0.4, strictness **10**, editor tests included, Steinberg validator integrated | ✅ **SUCCESS** (25 test groups: open cold/warm, info, editor, editor automation, audio processing, non-releasing processing, state, state restoration, automation, automatable parameters, parameter thread safety, buses, parameter fuzzing) |
| `build.bat` end to end | ✅ |
| `install.bat` copy logic (dry run into a scratch folder, elevation skipped) | ✅ whole bundle incl. `moduleinfo.json`, standalone exe, Start Menu shortcut |
| `scripts/package-windows.ps1` | ✅ `dist\PedalFaster-1.0.0-win64.zip` (≈ 8.4 MB) |

## What the automated tests cover

- **Registry:** exactly 30 effects with unique ids and 30 distinct implementation classes; parameter specs valid; normalised↔real round trips.
- **Per effect (all 30):** silence in → silence out; finite and bounded on a guitar DI; output differs from input; 44.1 / 48 / 96 kHz × block sizes 32, 64, 128, 256, 512, 4096; Eco quality.
- **Algorithms:** delay echo position and feedback ratio; tremolo depth; vibrato level; pitch shifter output pitch (+12 → 440 Hz from 220 Hz); octaver sub-octave (110 Hz); compressor dynamic-range reduction; gate open/closed; stability at extreme feedback (tape 1.05, shimmer hall at a 20 s decay, flanger/phaser 0.95, plate tail decay); the 8 gain/drive pedals pairwise different.
- **Amps and cabs:** the 6 amp models pairwise different (spectral shape and harmonic distortion); Studio Clean < 5% THD at low gain; Modern Metal ≥ 3x the distortion of Studio Clean; Bass DI keeps 55 Hz; integer latency per quality; 5 cabinets pairwise different; custom IR convolution observed.
- **Chain:** routing pack/unpack/string round trips; random effect-type swapping during processing (finite, no leaks: JUCE leak detector); tuner accuracy (A2 within 2 cents).
- **Presets:** exactly 200 presets in the 20 specified banks, 10 each, with the specified names in order; 5 × 10 electronic presets (250 total); unique ids; every preset passes strict validation (types, keys, ranges); no two presets share identical settings; coverage goals (≥ 30 low-gain, ≥ 30 high-gain, ≥ 30 modulation/ambient, 20 bass/acoustic); all 30 effects and 6 amps used; lossless JSON round trip for every preset; malformed JSON, unknown types, non-numeric values and future schema/bank versions rejected; schema-0 migration; validator range errors; bank round trip; user preset save/rename/duplicate/delete; factory immutability; favourites persistence; preset/bank import and export.
- **Processor:** silence at every rate and block size; 4096-sample offline blocks; mono→mono, mono→stereo, stereo→stereo buses; **global bypass equals the input delayed by exactly the reported latency** at every quality; pre-amp chain order changes the result; slot automation text in real units; macros change the sound; MIDI CC7 volume; **every one of the 250 presets** loads without warnings, adopts all slots, renders finite audio at −20 ± 4 dBFS RMS and never exceeds its limiter ceiling; no two presets produce indistinguishable output; Pristine Clean presets < 8% THD at −12 dBFS; bass presets keep their low end; Metal Rhythm presets stay quiet on noise-floor input; **DAW state round trip** (parameters, routing, macros, embedded IR, modified flag) renders identically after the original IR file is deleted; garbage state is ignored; user preset save/reload through the processor; preset and pedal-type switching while audio runs (finite, bounded steps); editor opens, resizes (1100×700 → 2200×1300) and closes.
- **Acoustic regression:** level and 16-band spectral profile of all 250 presets against `tests/regression/baseline.json` (±1.5 dB, shape distance < 1.5 dB).

Approximate performance: the calibration tool renders 3,000 seconds of audio through full presets (250 presets × 3 passes × 4 s) in about 40 s on one thread. That's roughly 75x real time on average at Normal quality, or about 1–2% of one core per instance. Heavier presets (shimmer hall, harmonizer, High quality) cost more. The plugin shows its live CPU load in the top bar.

## Not verified (please check locally)

- **Live guitar through an audio interface.** The standalone app was built but not launched against a real device in this session. Start *Pedal Faster.exe*, pick your interface in Options → Audio/MIDI Settings, and play.
- **Specific DAWs** (Reaper, Ableton Live, Cubase, Studio One, FL Studio, Bitwig). pluginval and the Steinberg validator act as hosts, but no commercial DAW was run. Recommended checks: insert on a mono guitar track, automate a slot parameter, save/close/reopen the project, render offline.
- **The elevation step of `install.bat`.** The UAC prompt was not exercised. Only the copy logic was run in a scratch folder.
- **Subjective tone.** Presets are level-calibrated and tested for distinctness, cleanliness and stability, but nobody listened to all 250 with a real guitar. Expect to fine-tune favourites by ear.
- **HiDPI at 150% and 200% scaling** and keyboard-only navigation of every control (JUCE handles scaling; not visually inspected).
- **macOS and Linux.** The DSP code is portable C++, but only Windows x64 was built.
