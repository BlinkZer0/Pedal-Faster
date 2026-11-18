# Preset format (schema version 1)

Presets are UTF-8 JSON. User presets live in `%APPDATA%\Pedal Faster\Presets\*.json`. Exports use `.pfpreset` (one preset), `.pfbank` (several) and `.pfpedal` (one pedal's settings). All of them are the same JSON format. The factory library is compiled into the plugin, and a reference copy is kept in `resources/presets/factory/`.

## Example

```json
{
  "schemaVersion": 1,
  "id": "factory.metal-rhythm.tight-machine",
  "name": "Tight Machine",
  "category": "Metal Rhythm",
  "description": "Tight, gated modern rhythm tone.",
  "author": "Pedal Faster",
  "tags": ["metal", "rhythm", "high-gain", "tight"],
  "gainLevel": 9,
  "inputGainDb": 0.0,
  "gate": { "enabled": true, "thresholdDb": -60.0, "releaseMs": 60.0 },
  "amp": {
    "type": "modern_metal", "enabled": true,
    "parameters": { "gain": 0.68, "bass": 0.44, "mid": 0.56, "treble": 0.64, "presence": 0.58, "master": 0.5, "sag": 0.3, "trimDb": 0.0 }
  },
  "cabinet": { "type": "closed_4x12", "enabled": true, "mic": 0.3, "lowCutHz": 85.0, "highCutHz": 7600.0, "levelDb": 0.0, "mix": 1.0 },
  "effects": [
    { "slotId": "slot-1", "type": "noise_gate", "position": "pre", "enabled": true,
      "parameters": { "thresholdDb": -52.0, "attackMs": 2.0, "holdMs": 40.0, "releaseMs": 90.0, "hysteresisDb": 6.0, "rangeDb": -80.0 } },
    { "slotId": "slot-2", "type": "mid_overdrive", "position": "pre", "enabled": true,
      "parameters": { "drive": 0.12, "tone": 0.67, "mid": 0.6, "level": 0.78 } }
  ],
  "masterEq": { "enabled": true, "lowDb": 0.0, "midDb": 0.0, "midFreqHz": 800.0, "highDb": 0.0 },
  "limiter": { "enabled": true, "ceilingDb": -0.5 },
  "macros": [],
  "outputGainDb": -9.6
}
```

## Fields

| Field | Type / range | Notes |
|---|---|---|
| `schemaVersion` | int | Current version is 1. Higher versions are **rejected**; 0 or missing is migrated. |
| `id` | string | Stable id. Factory: `factory.<bank>.<name>`. User: `user.<name>[-n]`. |
| `name`, `category`, `description`, `author` | string | `name` is required |
| `tags` | string[] | Searchable |
| `gainLevel` | 0–10 | Informational; recomputed from the chain (used for sorting) |
| `inputGainDb` | −24…24 | |
| `gate` | `enabled`, `thresholdDb` −90…−20, `releaseMs` 5…500 | Global input gate |
| `amp.type` | `studio_clean`, `class_a_chime`, `british_crunch`, `hot_rod_lead`, `modern_metal`, `bass_di` | |
| `amp.parameters` | `gain bass mid treble presence master sag` 0…1, `trimDb` −24…12 | |
| `cabinet.type` | `open_1x12`, `open_2x12`, `closed_4x12`, `bass_4x10`, `bass_1x15`, `direct`, `custom_ir` | |
| `cabinet.*` | `mic` 0…1, `lowCutHz` 20…400, `highCutHz` 2000…20000, `levelDb` −24…12, `mix` 0…1, `irFile` (custom_ir only) | If the IR file is missing, the preset loads with the 4x12 voicing and a warning |
| `effects[]` | up to 8, **in signal order** | |
| `effects[].slotId` | `slot-1`…`slot-8` | Honoured when valid and unique (keeps automation lanes on the same slot); otherwise slots are assigned in order |
| `effects[].type` | registry id (below) | Unknown types are **rejected** |
| `effects[].position` | `pre` / `post` | Before or after the amp |
| `effects[].enabled` | bool | Schema 0 used `"bypass": true` |
| `effects[].mix` | 0…1 | Only for effects with a wet/dry control; omitted means the effect's default |
| `effects[].parameters` | object | Real units, keyed as below. Missing keys use defaults. Unknown keys are ignored with a warning (the strict validator reports them). |
| `masterEq` | `lowDb` / `midDb` / `highDb` −12…12, `midFreqHz` 200…5000 | |
| `limiter` | `enabled`, `ceilingDb` −12…0 | |
| `macros[]` | `{ "macro": "drive"\|"tone"\|"space"\|"output", "target": ..., "amount": −1…1 }` | Target is a host parameter id (`amp_gain`, `cab_highcut`, `out_gain`, …) or `fx:<effect index>:<parameter key or mix>`. An empty list means mappings are derived from the chain. |
| `tempoBpm` | 40…240 | Optional; sets the internal tempo when the preset loads |
| `outputGainDb` | −24…12 | Preset output level (factory values include loudness calibration) |

## Validation

`PresetValidator::validate` (strict, used by the tests on all 250 factory presets) checks required fields, every type id, every parameter key and range, slot id uniqueness, the effect count and macro ranges. Parsing rejects malformed JSON, non-object roots, non-numeric values, unknown effect, amp or cabinet types, and future schema or bank versions. Out-of-range values are reported as warnings and clamped when a preset is applied.

## Effect parameter keys

Choice parameters take the **index** of the choice. `sync` indices: 0 Off, 1 1/1, 2 1/2, 3 1/2 D, 4 1/4, 5 1/4 D, 6 1/4 T, 7 1/8, 8 1/8 D, 9 1/8 T, 10 1/16, 11 1/16 T, 12 1/32.

| Type id | Parameters (key: range, default) |
|---|---|
| `noise_gate` | thresholdDb −90…−10 (−55), attackMs 0.1…50 (1), holdMs 0…500 (40), releaseMs 5…1000 (120), hysteresisDb 0…20 (6), rangeDb −90…0 (−80) |
| `compressor` | thresholdDb −60…0 (−20), ratio 1…20 (4), attackMs 0.1…100 (10), releaseMs 10…1000 (150), makeupDb 0…24 (4), kneeDb 0…12 (6) · mix |
| `clean_boost` | gainDb 0…24 (8), tone 0…1 (.5), outputDb −24…12 (0) |
| `transparent_od` | drive, bass, treble, level 0…1 (.4/.5/.5/.6) |
| `mid_overdrive` | drive, tone, mid, level 0…1 (.5/.5/.5/.6) |
| `hard_distortion` | gain 0…1 (.5), tone 0…1 (.5), bias −1…1 (0), level 0…1 (.55) |
| `high_gain_distortion` | gain, bass, middle, treble, presence, level 0…1 |
| `fuzz` | sustain, tone, bias, level 0…1 (bias < .5 = starved/gated) |
| `octave_fuzz` | drive, octave, tone, level 0…1 |
| `bitcrusher` | bits 1…16 (8), downsample 1…64 (4), tone 0…1, outputDb −24…6 · mix |
| `wah` | position 0…1, resonance 0…1, drive 0…1, range {0 Classic, 1 Low, 2 High} · mix |
| `auto_wah` | rateHz 0.05…12, sync, depth, resonance, sensitivity, centre 0…1, mode {0 Band-pass, 1 Low-pass, 2 Peak} · mix |
| `envelope_filter` | sensitivity 0…1, attackMs 1…100, decayMs 20…2000, q 0.5…15, direction {0 Up, 1 Down}, mode {0 LP, 1 BP, 2 HP}, base 0…1 · mix |
| `graphic_eq` | band100, band200, band400, band800, band1600, band3200, band6400 −12…12 dB, outputDb −12…12 |
| `parametric_eq` | lowFreq 30…600, lowGain ±15, lowQ 0.3…2, midFreq 150…6000, midGain ±15, midQ 0.3…10, highFreq 1000…16000, highGain ±15, highQ 0.3…2, outputDb ±12 |
| `phaser` | rateHz 0.02…10, depth 0…1, feedback −0.95…0.95, stages {0:2 … 5:12}, centre, stereo 0…1 · mix (.5) |
| `flanger` | rateHz 0.02…5, depth, feedback −0.95…0.95, delayMs 0.5…10, stereo · mix (.5) |
| `chorus` | rateHz 0.05…5, depth, voices {0:1 … 3:4}, width, delayMs 5…30, tone · mix (.5) |
| `vibrato` | rateHz 0.1…12, depth, shape {0 Sine, 1 Triangle, 2 Square} · mix |
| `tremolo` | rateHz 0.1…20, sync, depth, waveform {0 Sine, 1 Tri, 2 Square, 3 Saw Down, 4 Saw Up}, stereo |
| `rotary` | speed {0 Slow, 1 Fast, 2 Brake}, acceleration, width, drive, balance · mix |
| `pitch_shifter` | semitones −24…24, cents −100…100, grainMs 15…100 · mix |
| `octaver` | sub1, sub2, up, direct, tracking 0…1 |
| `harmonizer` | interval {0:−12, 1:−7, 2:−5, 3:−4, 4:−3, 5:+3, 6:+4, 7:+5, 8:+7, 9:+9, 10:+12}, voice2 {0 Off, then the same list +1}, level, spread · mix |
| `slapback` | timeMs 40…250, feedback 0…1 (internally capped at 0.7), tone · mix (.3) |
| `digital_delay` | timeMs 10…2000, sync, feedback 0…1 (capped .95), stereo {0 Mono, 1 Stereo, 2 Ping-Pong}, spread, lowCutHz 20…1000, highCutHz 1000…20000 · mix (.3) |
| `tape_delay` | timeMs 40…1500, sync, feedback 0…1.05, saturation, wow, flutter, tone · mix (.3) |
| `spring_reverb` | decay 0.5…5 s, tone, character, dwell · mix (.25) |
| `plate_reverb` | decay 0.3…10 s, predelayMs 0…200, damping, lowCutHz 20…600, modulation · mix (.25) |
| `hall_reverb` | size, decay 0.5…20 s, diffusion, damping, predelayMs 0…250, modulation, shimmer, lowCutHz 20…600 · mix (.25) |

The registry entry order (index 0 = empty, then the order above) is also the value of the `sN_type` host parameter, so it must never be reordered.
