#include "EffectRegistry.h"
#include "effects/Delay.h"
#include "effects/Dynamics.h"
#include "effects/Filters.h"
#include "effects/Gain.h"
#include "effects/Modulation.h"
#include "effects/Pitch.h"
#include "effects/Reverb.h"

namespace pf
{
namespace
{
template <typename T>
std::function<std::unique_ptr<Effect>()> make() { return [] { return std::make_unique<T>(); }; }

EffectInfo info (const char* id, const char* name, const char* cat, bool hasMix, float defMix,
                 std::vector<ParamSpec> params, std::function<std::unique_ptr<Effect>()> create)
{
    EffectInfo e;
    e.id = id; e.name = name; e.category = cat; e.hasMix = hasMix; e.defaultMix = defMix;
    e.params = std::move (params); e.create = std::move (create);
    return e;
}
} // namespace

const EffectRegistry& EffectRegistry::get()
{
    static const EffectRegistry r;
    return r;
}

EffectRegistry::EffectRegistry()
{
    const auto& sync = syncChoices();

    infos.push_back (info ("empty", "Empty", "None", false, 1.0f, {}, nullptr));

    // ---- Dynamics
    infos.push_back (info ("noise_gate", "Noise Gate", "Dynamics", false, 1.0f, {
        pLin ("thresholdDb", "Threshold", -90, -10, -55, "dB"), pLog ("attackMs", "Attack", 0.1f, 50, 1, "ms"),
        pLin ("holdMs", "Hold", 0, 500, 40, "ms"), pLog ("releaseMs", "Release", 5, 1000, 120, "ms"),
        pLin ("hysteresisDb", "Hysteresis", 0, 20, 6, "dB"), pLin ("rangeDb", "Range", -90, 0, -80, "dB") }, make<NoiseGate>()));
    infos.push_back (info ("compressor", "Compressor", "Dynamics", true, 1.0f, {
        pLin ("thresholdDb", "Threshold", -60, 0, -20, "dB"), pLog ("ratio", "Ratio", 1, 20, 4, ":1"),
        pLog ("attackMs", "Attack", 0.1f, 100, 10, "ms"), pLog ("releaseMs", "Release", 10, 1000, 150, "ms"),
        pLin ("makeupDb", "Makeup", 0, 24, 4, "dB"), pLin ("kneeDb", "Knee", 0, 12, 6, "dB") }, make<Compressor>()));

    // ---- Gain
    infos.push_back (info ("clean_boost", "Clean Boost", "Gain", false, 1.0f, {
        pLin ("gainDb", "Gain", 0, 24, 8, "dB"), pPct ("tone", "Tone", 0.5f), pLin ("outputDb", "Output", -24, 12, 0, "dB") }, make<CleanBoost>()));
    infos.push_back (info ("transparent_od", "Transparent Overdrive", "Gain", false, 1.0f, {
        pPct ("drive", "Drive", 0.4f), pPct ("bass", "Bass", 0.5f), pPct ("treble", "Treble", 0.5f), pPct ("level", "Level", 0.6f) }, make<TransparentOverdrive>()));
    infos.push_back (info ("mid_overdrive", "Mid-Focused Overdrive", "Gain", false, 1.0f, {
        pPct ("drive", "Drive", 0.5f), pPct ("tone", "Tone", 0.5f), pPct ("mid", "Mid Emphasis", 0.5f), pPct ("level", "Level", 0.6f) }, make<MidOverdrive>()));
    infos.push_back (info ("hard_distortion", "Hard-Clipping Distortion", "Gain", false, 1.0f, {
        pPct ("gain", "Gain", 0.5f), pPct ("tone", "Tone", 0.5f), pLin ("bias", "Bias", -1, 1, 0), pPct ("level", "Level", 0.55f) }, make<HardClipDistortion>()));
    infos.push_back (info ("high_gain_distortion", "High-Gain Distortion", "Gain", false, 1.0f, {
        pPct ("gain", "Gain", 0.6f), pPct ("bass", "Bass", 0.5f), pPct ("middle", "Middle", 0.5f), pPct ("treble", "Treble", 0.5f),
        pPct ("presence", "Presence", 0.5f), pPct ("level", "Level", 0.5f) }, make<HighGainDistortion>()));
    infos.push_back (info ("fuzz", "Fuzz", "Gain", false, 1.0f, {
        pPct ("sustain", "Sustain", 0.6f), pPct ("tone", "Tone", 0.5f), pPct ("bias", "Bias", 0.5f), pPct ("level", "Output", 0.55f) }, make<Fuzz>()));
    infos.push_back (info ("octave_fuzz", "Octave Fuzz", "Gain", false, 1.0f, {
        pPct ("drive", "Drive", 0.6f), pPct ("octave", "Octave Blend", 0.6f), pPct ("tone", "Tone", 0.5f), pPct ("level", "Output", 0.55f) }, make<OctaveFuzz>()));

    // ---- Experimental
    infos.push_back (info ("bitcrusher", "Bitcrusher", "Experimental", true, 1.0f, {
        pLin ("bits", "Bit Depth", 1, 16, 8, "bit"), pLog ("downsample", "Rate Reduction", 1, 64, 4, "x"),
        pPct ("tone", "Tone", 0.8f), pLin ("outputDb", "Output", -24, 6, 0, "dB") }, make<Bitcrusher>()));

    // ---- Filter
    infos.push_back (info ("wah", "Wah", "Filter", true, 1.0f, {
        pPct ("position", "Frequency", 0.5f), pPct ("resonance", "Resonance", 0.5f), pPct ("drive", "Drive", 0.0f),
        pChoice ("range", "Range", { "Classic", "Low", "High" }, 0) }, make<Wah>()));
    infos.push_back (info ("auto_wah", "Auto-Wah", "Filter", true, 1.0f, {
        pLog ("rateHz", "Rate", 0.05f, 12, 1.5f, "Hz"), pChoice ("sync", "Sync", sync, 0), pPct ("depth", "Depth", 0.7f),
        pPct ("resonance", "Resonance", 0.4f), pPct ("sensitivity", "Sensitivity", 0.2f), pPct ("centre", "Centre", 0.45f),
        pChoice ("mode", "Mode", { "Band-pass", "Low-pass", "Peak" }, 0) }, make<AutoWah>()));
    infos.push_back (info ("envelope_filter", "Envelope Filter", "Filter", true, 1.0f, {
        pPct ("sensitivity", "Sensitivity", 0.5f), pLog ("attackMs", "Attack", 1, 100, 8, "ms"), pLog ("decayMs", "Decay", 20, 2000, 250, "ms"),
        pLog ("q", "Q", 0.5f, 15, 4), pChoice ("direction", "Direction", { "Up", "Down" }, 0),
        pChoice ("mode", "Mode", { "Low-pass", "Band-pass", "High-pass" }, 0), pPct ("base", "Range Start", 0.1f) }, make<EnvelopeFilter>()));

    // ---- EQ
    infos.push_back (info ("graphic_eq", "Graphic EQ", "EQ", false, 1.0f, {
        pLin ("band100", "100 Hz", -12, 12, 0, "dB"), pLin ("band200", "200 Hz", -12, 12, 0, "dB"), pLin ("band400", "400 Hz", -12, 12, 0, "dB"),
        pLin ("band800", "800 Hz", -12, 12, 0, "dB"), pLin ("band1600", "1.6 kHz", -12, 12, 0, "dB"), pLin ("band3200", "3.2 kHz", -12, 12, 0, "dB"),
        pLin ("band6400", "6.4 kHz", -12, 12, 0, "dB"), pLin ("outputDb", "Output", -12, 12, 0, "dB") }, make<GraphicEQ>()));
    infos.push_back (info ("parametric_eq", "Parametric EQ", "EQ", false, 1.0f, {
        pLog ("lowFreq", "Low Freq", 30, 600, 120, "Hz"), pLin ("lowGain", "Low Gain", -15, 15, 0, "dB"), pLog ("lowQ", "Low Q", 0.3f, 2, 0.7f),
        pLog ("midFreq", "Mid Freq", 150, 6000, 800, "Hz"), pLin ("midGain", "Mid Gain", -15, 15, 0, "dB"), pLog ("midQ", "Mid Q", 0.3f, 10, 1),
        pLog ("highFreq", "High Freq", 1000, 16000, 4000, "Hz"), pLin ("highGain", "High Gain", -15, 15, 0, "dB"), pLog ("highQ", "High Q", 0.3f, 2, 0.7f),
        pLin ("outputDb", "Output", -12, 12, 0, "dB") }, make<ParametricEQ>()));

    // ---- Modulation
    infos.push_back (info ("phaser", "Phaser", "Modulation", true, 0.5f, {
        pLog ("rateHz", "Rate", 0.02f, 10, 0.5f, "Hz"), pPct ("depth", "Depth", 0.7f), pLin ("feedback", "Feedback", -0.95f, 0.95f, 0.3f),
        pChoice ("stages", "Stages", { "2", "4", "6", "8", "10", "12" }, 1), pPct ("centre", "Centre", 0.5f), pPct ("stereo", "Stereo", 0.5f) }, make<Phaser>()));
    infos.push_back (info ("flanger", "Flanger", "Modulation", true, 0.5f, {
        pLog ("rateHz", "Rate", 0.02f, 5, 0.3f, "Hz"), pPct ("depth", "Depth", 0.7f), pLin ("feedback", "Feedback", -0.95f, 0.95f, 0.5f),
        pLog ("delayMs", "Delay", 0.5f, 10, 2.5f, "ms"), pPct ("stereo", "Stereo", 0.5f) }, make<Flanger>()));
    infos.push_back (info ("chorus", "Chorus", "Modulation", true, 0.5f, {
        pLog ("rateHz", "Rate", 0.05f, 5, 0.8f, "Hz"), pPct ("depth", "Depth", 0.5f), pChoice ("voices", "Voices", { "1", "2", "3", "4" }, 1),
        pPct ("width", "Stereo Width", 0.7f), pLin ("delayMs", "Delay", 5, 30, 12, "ms"), pPct ("tone", "Tone", 0.8f) }, make<Chorus>()));
    infos.push_back (info ("vibrato", "Vibrato", "Modulation", true, 1.0f, {
        pLog ("rateHz", "Rate", 0.1f, 12, 5, "Hz"), pPct ("depth", "Depth", 0.4f), pChoice ("shape", "Shape", { "Sine", "Triangle", "Square" }, 0) }, make<Vibrato>()));
    infos.push_back (info ("tremolo", "Tremolo", "Modulation", false, 1.0f, {
        pLog ("rateHz", "Rate", 0.1f, 20, 5, "Hz"), pChoice ("sync", "Sync", sync, 0), pPct ("depth", "Depth", 0.5f),
        pChoice ("waveform", "Waveform", { "Sine", "Triangle", "Square", "Saw Down", "Saw Up" }, 0), pPct ("stereo", "Stereo Phase", 0.0f) }, make<Tremolo>()));
    infos.push_back (info ("rotary", "Rotary Speaker", "Modulation", true, 1.0f, {
        pChoice ("speed", "Speed", { "Slow", "Fast", "Brake" }, 0), pPct ("acceleration", "Acceleration", 0.4f), pPct ("width", "Stereo Width", 0.8f),
        pPct ("drive", "Drive", 0.1f), pPct ("balance", "Horn/Drum", 0.55f) }, make<RotarySpeaker>()));

    // ---- Pitch
    infos.push_back (info ("pitch_shifter", "Pitch Shifter", "Pitch", true, 1.0f, {
        pInt ("semitones", "Semitones", -24, 24, 12, "st"), pLin ("cents", "Cents", -100, 100, 0, "ct"), pLog ("grainMs", "Grain", 15, 100, 45, "ms") }, make<PitchShifter>()));
    infos.push_back (info ("octaver", "Octaver", "Pitch", false, 1.0f, {
        pPct ("sub1", "Sub Octave", 0.6f), pPct ("sub2", "Sub 2 Octave", 0.0f), pPct ("up", "Upper Octave", 0.0f),
        pPct ("direct", "Direct", 0.8f), pPct ("tracking", "Tracking", 0.5f) }, make<Octaver>()));
    infos.push_back (info ("harmonizer", "Harmonizer", "Pitch", true, 1.0f, {
        pChoice ("interval", "Interval", Harmonizer::intervalNames(), 8), pChoice ("voice2", "Voice 2", Harmonizer::voice2Names(), 0),
        pPct ("level", "Harmony Level", 0.6f), pPct ("spread", "Spread", 0.5f) }, make<Harmonizer>()));

    // ---- Delay
    infos.push_back (info ("slapback", "Slapback Delay", "Delay", true, 0.3f, {
        pLin ("timeMs", "Time", 40, 250, 110, "ms"), pPct ("feedback", "Feedback", 0.1f), pPct ("tone", "Tone", 0.6f) }, make<SlapbackDelay>()));
    infos.push_back (info ("digital_delay", "Digital Delay", "Delay", true, 0.3f, {
        pLog ("timeMs", "Time", 10, 2000, 400, "ms"), pChoice ("sync", "Sync", sync, 0), pPct ("feedback", "Feedback", 0.35f),
        pChoice ("stereo", "Stereo", { "Mono", "Stereo", "Ping-Pong" }, 1), pPct ("spread", "Spread", 0.5f),
        pLog ("lowCutHz", "Low Cut", 20, 1000, 80, "Hz"), pLog ("highCutHz", "High Cut", 1000, 20000, 9000, "Hz") }, make<DigitalDelay>()));
    infos.push_back (info ("tape_delay", "Tape Delay", "Delay", true, 0.3f, {
        pLog ("timeMs", "Time", 40, 1500, 350, "ms"), pChoice ("sync", "Sync", sync, 0), pLin ("feedback", "Feedback", 0, 1.05f, 0.4f),
        pPct ("saturation", "Saturation", 0.4f), pPct ("wow", "Wow", 0.3f), pPct ("flutter", "Flutter", 0.3f), pPct ("tone", "Tone", 0.5f) }, make<TapeDelay>()));

    // ---- Reverb
    infos.push_back (info ("spring_reverb", "Spring Reverb", "Reverb", true, 0.25f, {
        pLin ("decay", "Decay", 0.5f, 5, 2, "s"), pPct ("tone", "Tone", 0.5f), pPct ("character", "Spring Character", 0.5f), pPct ("dwell", "Dwell", 0.3f) }, make<SpringReverb>()));
    infos.push_back (info ("plate_reverb", "Plate Reverb", "Reverb", true, 0.25f, {
        pLog ("decay", "Decay", 0.3f, 10, 2.5f, "s"), pLin ("predelayMs", "Predelay", 0, 200, 10, "ms"), pPct ("damping", "Damping", 0.4f),
        pLog ("lowCutHz", "Low Cut", 20, 600, 120, "Hz"), pPct ("modulation", "Modulation", 0.5f) }, make<PlateReverb>()));
    infos.push_back (info ("hall_reverb", "Hall Reverb", "Reverb", true, 0.25f, {
        pPct ("size", "Size", 0.6f), pLog ("decay", "Decay", 0.5f, 20, 3.5f, "s"), pPct ("diffusion", "Diffusion", 0.7f), pPct ("damping", "Damping", 0.4f),
        pLin ("predelayMs", "Predelay", 0, 250, 20, "ms"), pPct ("modulation", "Modulation", 0.4f), pPct ("shimmer", "Shimmer", 0.0f),
        pLog ("lowCutHz", "Low Cut", 20, 600, 100, "Hz") }, make<HallReverb>()));

    for (size_t i = 0; i < infos.size(); ++i)
        infos[i].index = (int) i;
}

const EffectInfo* EffectRegistry::find (const std::string& id) const noexcept
{
    for (auto& i : infos) if (i.id == id) return &i;
    return nullptr;
}

std::vector<std::string> EffectRegistry::categories() const
{
    std::vector<std::string> c;
    for (auto& i : infos)
        if (i.index > 0 && std::find (c.begin(), c.end(), i.category) == c.end()) c.push_back (i.category);
    return c;
}
} // namespace pf
