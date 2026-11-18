# DSP design

All algorithms are original implementations. Nothing here is a measured capture of a specific product. The names describe musical character, not hardware being modelled.

## Common building blocks (`src/dsp/DspCommon.h`)

- **Biquad:** RBJ cookbook low/high/band-pass, notch, peak and shelves, in transposed direct form II with denormal flushing.
- **SVF:** Simper/Zavalishin topology-preserving state-variable filter. It stays stable under audio-rate cutoff modulation, so all swept filters (wah family) use it.
- **ADAA waveshapers:** first-order antiderivative anti-aliasing for `tanh` (antiderivative `log cosh`) and hard clipping (piecewise quadratic), with bias. This cuts aliasing in the drive pedals without oversampling or latency. Eco quality switches it off.
- **Delay lines:** power-of-two ring buffers with linear and 4-point Hermite interpolation.
- **Smoothers** on every gain-type parameter, with block-rate coefficient updates for filters.

## Amplifiers (`src/dsp/Amps`)

Each model runs entirely at the oversampled rate: 1x in Eco, 2x in Normal, 4x in High (JUCE polyphase IIR, integer latency). They share three building blocks but use different topologies:

- **TriodeStage:** gain, then a Miller-capacitance low-pass, then an asymmetric soft clipper (positive `tanh`; negative side clips earlier with "hardness" k: `tanh(k·u)/k`) with a bias offset for even harmonics, then a coupling DC blocker.
- **ToneStack:** low shelf, peaking mid and high shelf, with model-specific corner frequencies, ranges and mid offset.
- **PowerStage:** master drive into a push-pull (symmetric) or class-A (asymmetric) clipper. A supply-sag envelope (8 ms attack, 180 ms release) lowers the drive and the ceiling, and a presence shelf sits in the "feedback" path.

| Model | Topology |
|---|---|
| **Studio Clean** | 30 Hz input HP → bright shelf (fades out as gain rises) → 1 gentle stage → scooped tone stack (mid −3 dB baseline at 480 Hz) → recovery stage → high-headroom push-pull power |
| **Class A Chime** | 60 Hz HP → bright pentode-style stage (high negative hardness) → "top boost" tone stack (treble ±14 dB at 2.2 kHz) → fixed 3.2 kHz chime resonance → class-A power that breaks up early and sags strongly |
| **British Crunch** | 90 Hz tight HP → bright cap (+4 dB at 700 Hz) → 2 cascaded stages with opposite bias → cathode-follower soft limit → mid-forward stack (+2 dB at 720 Hz) → EL34-style push-pull with sag |
| **Hot Rod Lead** | 3 cascaded stages with progressively darker Miller filters (8 / 7 / 6.5 kHz) for compressed sustain → mid-hump stack (+3 dB at 900 Hz) → power → 6.5 kHz smoothing |
| **Modern Metal** | 160 Hz HP and −6 dB low shelf *before* distortion for tightness → 4 stages with a 110 Hz inter-stage HP → wide-range mid (±12 dB at 750 Hz) → resonance shelf (+4 dB at 90 Hz) restores body after distortion → stiff, low-sag power → 9.5 kHz fizz filter |
| **Bass DI / Amp** | 220 Hz Linkwitz-style band split: lows stay clean with optional sag compression, highs get the drive ("grit"), then they're recombined → bass-voiced stack (60 Hz / 500 Hz / 3 kHz) + air shelf → very high-headroom power |

The unit tests confirm the six models differ pairwise in spectral shape and harmonic distortion. They also check that Studio Clean stays under 5% THD at low gain, Modern Metal distorts far more than Studio Clean under the same settings, and Bass DI keeps a 55 Hz fundamental.

## Cabinets (`src/dsp/Cabinets`)

Factory cabinets are filter cascades of up to eight biquads per voicing: speaker resonance as a resonant high-pass, low-mid bump and dip, cone break-up peak, a notch above it and a steep roll-off. Open-back cabinets add a short negative comb for the rear wave. The **mic** control moves from on-axis (bright, strong presence peak) to edge (lower roll-off, less presence). Coefficients update without resetting state, so mic moves are smooth.

| Cabinet | Character |
|---|---|
| 1x12 open | 85 Hz resonance, light low end, 0.35 ms rear-wave comb |
| 2x12 open | 75 Hz, more low-mid, 0.5 ms comb |
| 4x12 closed | 70 Hz with high-Q thump, scooped 420 Hz, strong 2.3–3.4 kHz presence |
| 4x10 bass | 45 Hz, extended top for bass |
| 1x15 bass | 38 Hz, warm, early roll-off |
| Direct | no speaker filtering (only low/high cut) |
| Custom IR | JUCE `dsp::Convolution` (zero latency, non-uniform partitions). WAV/AIFF/FLAC, mono or stereo, leading silence trimmed, energy-normalised, truncated to 2 s with a 5 ms fade. Mic becomes an off-axis darkening shelf. If no IR is loaded it falls back to the 4x12 voicing. |

Low cut, high cut, level and wet/dry apply to every cabinet.

## Pedals (`src/dsp/effects`)

| Pedal | Algorithm |
|---|---|
| Noise Gate | Peak detector (0.2/25 ms) → open/hold/close state machine with hysteresis → gain smoothed by the attack/release times, floor set by range |
| Compressor | Stereo-linked feed-forward, log-domain gain computer with soft knee, attack/release smoothing in dB, makeup gain |
| Clean Boost | Up to +24 dB into a high-headroom ADAA `tanh` (soft only near the rails) and a tilt EQ around about 800 Hz |
| Transparent Overdrive | Clean path blended with a 140 Hz high-passed, ADAA hard-clipped path (clean share falls as drive rises), then Baxandall-style bass and treble shelves |
| Mid-Focused Overdrive | Mid-emphasis peak → 720 Hz high-pass → ADAA soft clip, summed with the unclipped signal (keeps lows clean), then a 2-pole tone low-pass |
| Hard-Clipping Distortion | Op-amp model: gain up to about 1500 with **gain-dependent bandwidth** (GBW low-pass at 2.2 MHz / gain) → asymmetric ADAA hard clip (bias control) → tone low-pass → DC block |
| High-Gain Distortion | 115 Hz tight HP → three cascaded asymmetric ADAA stages with inter-stage low-pass and DC blocking → bass, middle, treble and presence EQ → fizz low-pass |
| Fuzz | 45 Hz HP → gain up to about 500 into a bias-shifted clipper → **starvation dead-zone** (low bias = gated sputter) → second clipper → mid-scooping LP/HP blend tone control |
| Octave Fuzz | Soft preamp → **ADAA full-wave rectifier** (antiderivative x·abs(x)/2) for the octave-up, blended with the straight signal → fuzz stage → tone |
| Bitcrusher | Fractional bit-depth quantiser + sample-and-hold rate reduction (1–64x) + post low-pass |
| Wah | Pedal position → exponential 350 Hz–2.2 kHz sweep (3 ranges) of a resonant SVF band-pass with a little low-pass bleed, optional pre-drive. CC 11 controls it. |
| Auto-Wah | LFO-swept SVF (free rate or tempo-synced) plus an envelope amount. Band-pass, low-pass or peak mode. Tempo-synced low-pass gives dubstep/DnB wobbles. |
| Envelope Filter | Attack/decay envelope follower moves the SVF cutoff up or down from a start point. Low-pass, band-pass or high-pass. |
| Graphic EQ | 7 constant-Q peaking bands an octave apart (100 Hz–6.4 kHz) ±12 dB, plus output |
| Parametric EQ | Low shelf, fully parametric mid and high shelf (Q sets the shelf slope), plus output |
| Phaser | 2–12 first-order all-pass stages swept exponentially by a triangle LFO, with feedback from the last stage. Stereo LFO offset. The slot's mix supplies the dry sum that creates the notches. |
| Flanger | 0.5–10 ms Hermite-interpolated delay with triangle modulation and ± feedback, stereo offset |
| Chorus | 1–4 voices of 5–30 ms modulated delay with staggered phases and rate multipliers, panned alternately, with a tone filter |
| Vibrato | Single modulated delay, 100% wet, depth scaled with rate so the pitch deviation stays constant. Sine, triangle or square shapes. |
| Tremolo | Amplitude modulation with 5 waveforms (the square is softened). Free rate or tempo-synced, **phase-locked to the host bar grid** while playing. Stereo phase offset for pan-tremolo. |
| Rotary Speaker | 800 Hz crossover into horn and drum rotors. Each has its own inertia (acceleration), Doppler (modulated delay) and amplitude modulation, picked up by two virtual mics. Slow, fast or brake, with drive. |
| Pitch Shifter | Two read taps rotating through a delay buffer with complementary Hann windows (sum = 1), ±24 semitones plus cents, adjustable grain |
| Octaver | Analogue-style: tracking low-pass → hysteresis zero-crossing flip-flops (−1 and −2 octaves) multiplied by the filtered signal, plus a full-wave rectifier for +1 octave, each with its own level |
| Harmonizer | **Pitch-tracked** two-voice shifter: an AMDF detector on a decimated buffer sets the grain window to a whole number of pitch periods, so splices fall on period boundaries (much less warble than the fixed-grain shifter). Interval and second voice from a musical list, plus level and stereo spread. |
| Slapback Delay | Single short tap (40–250 ms) with light feedback and band-limited repeats |
| Digital Delay | Up to 2 s, tempo sync, mono / stereo-offset / ping-pong, high-pass and low-pass filters in the feedback loop, glide on time changes |
| Tape Delay | Read head modulated by wow (0.55 Hz), flutter (6.3 Hz) and random drift. `tanh` saturation inside the loop allows controlled self-oscillation (feedback up to 1.05). Head-bump high-pass and tone low-pass. |
| Spring Reverb | Two "springs" (33 ms and 42 ms loops), each a cascade of 14 dispersive all-passes (chirp) with damping and RT60-derived feedback. A "drip" tap grows with the character control, and dwell drives the input. |
| Plate Reverb | Dattorro figure-eight tank: predelay, bandwidth low-pass, 4 input diffusers, modulated decay diffusers, damping, and the classic 7-tap stereo output, scaled to the sample rate |
| Hall Reverb | 8-line feedback delay network with a Householder matrix, size-scaled prime delay lengths, per-line damping, two modulated lines, an input diffusion chain and predelay. **Shimmer** feeds an octave-up granular shift of the tail back into the network, soft-limited for stability. |

### Stability and robustness tests

Every effect is tested at 44.1, 48 and 96 kHz with block sizes 32–4096, in Eco and Normal quality. Each must be silent in, silent out; finite and bounded; and audibly different from the input. Extreme feedback cases are tested over 12 seconds: tape at feedback 1.05 with full saturation, shimmer hall at a 20 s decay, flanger and phaser at 0.95 feedback, and a 10 s plate. All stay bounded, and the reverb tails decay.
