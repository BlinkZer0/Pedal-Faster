#include "PresetSerializer.h"
#include "../dsp/Amps/AmpModels.h"
#include "../dsp/Cabinets/Cabinet.h"
#include "../dsp/EffectRegistry.h"

namespace pf
{
//==============================================================================
int Preset::gainLevel() const
{
    static const float ampBase[]   { 0.0f, 1.0f, 2.0f, 3.5f, 4.5f, 0.5f };
    static const float ampWeight[] { 1.5f, 3.0f, 4.0f, 5.0f, 5.5f, 2.0f };
    const int a = std::max (0, ampTypeIndex (amp.type));
    float g = amp.enabled ? ampBase[a] + ampWeight[a] * amp.gain : 0.0f;
    float pedal = 0.0f;
    auto val = [] (const EffectState& e, const char* k, float def) { auto it = e.params.find (k); return it != e.params.end() ? it->second : def; };
    for (auto& e : effects)
    {
        if (! e.enabled) continue;
        if (e.type == "clean_boost")          pedal = std::max (pedal, 0.5f);
        else if (e.type == "transparent_od")  pedal = std::max (pedal, 1.0f + 2.0f * val (e, "drive", 0.4f));
        else if (e.type == "mid_overdrive")   pedal = std::max (pedal, 1.0f + 2.0f * val (e, "drive", 0.5f));
        else if (e.type == "hard_distortion") pedal = std::max (pedal, 2.0f + 3.0f * val (e, "gain", 0.5f));
        else if (e.type == "high_gain_distortion") pedal = std::max (pedal, 3.0f + 4.0f * val (e, "gain", 0.6f));
        else if (e.type == "fuzz")            pedal = std::max (pedal, 3.0f + 4.0f * val (e, "sustain", 0.6f));
        else if (e.type == "octave_fuzz")     pedal = std::max (pedal, 3.0f + 3.0f * val (e, "drive", 0.6f));
    }
    g = std::max (g, pedal) + 0.35f * std::min (g, pedal);
    return std::clamp ((int) std::lround (g), 0, 10);
}

bool Preset::hasTag (const std::string& t) const
{
    for (auto& x : tags) if (juce::String (x).equalsIgnoreCase (juce::String (t))) return true;
    return false;
}

//==============================================================================
namespace
{
juce::var obj() { return juce::var (new juce::DynamicObject()); }
void set (juce::var& o, const char* k, const juce::var& v) { o.getDynamicObject()->setProperty (k, v); }

float getF (const juce::var& o, const char* k, float def, ParseResult* r = nullptr, const char* ctx = "")
{
    if (! o.isObject() || ! o.hasProperty (k)) return def;
    const auto& v = o[k];
    if (v.isDouble() || v.isInt() || v.isInt64()) { const float f = (float) (double) v; if (std::isfinite (f)) return f; }
    if (r) r->errors.push_back (std::string (ctx) + k + " is not a number");
    return def;
}
bool getB (const juce::var& o, const char* k, bool def)
{
    if (! o.isObject() || ! o.hasProperty (k)) return def;
    const auto& v = o[k];
    return v.isBool() ? (bool) v : (v.isInt() ? (int) v != 0 : def);
}
std::string getS (const juce::var& o, const char* k, const std::string& def = {})
{
    if (! o.isObject() || ! o.hasProperty (k)) return def;
    return o[k].toString().toStdString();
}
} // namespace

juce::var PresetSerializer::toVar (const Preset& p)
{
    auto root = obj();
    set (root, "schemaVersion", p.schemaVersion);
    set (root, "id", juce::String (p.id));
    set (root, "name", juce::String (p.name));
    set (root, "category", juce::String (p.category));
    set (root, "description", juce::String (p.description));
    set (root, "author", juce::String (p.author));
    juce::Array<juce::var> tags; for (auto& t : p.tags) tags.add (juce::String (t));
    set (root, "tags", tags);
    set (root, "gainLevel", p.gainLevel());
    set (root, "inputGainDb", p.inputGainDb);

    auto gate = obj();
    set (gate, "enabled", p.gate.enabled); set (gate, "thresholdDb", p.gate.thresholdDb); set (gate, "releaseMs", p.gate.releaseMs);
    set (root, "gate", gate);

    auto amp = obj(), ap = obj();
    set (amp, "type", juce::String (p.amp.type)); set (amp, "enabled", p.amp.enabled);
    set (ap, "gain", p.amp.gain); set (ap, "bass", p.amp.bass); set (ap, "mid", p.amp.mid); set (ap, "treble", p.amp.treble);
    set (ap, "presence", p.amp.presence); set (ap, "master", p.amp.master); set (ap, "sag", p.amp.sag); set (ap, "trimDb", p.amp.trimDb);
    set (amp, "parameters", ap);
    set (root, "amp", amp);

    auto cab = obj();
    set (cab, "type", juce::String (p.cab.type)); set (cab, "enabled", p.cab.enabled); set (cab, "mic", p.cab.mic);
    set (cab, "lowCutHz", p.cab.lowCutHz); set (cab, "highCutHz", p.cab.highCutHz); set (cab, "levelDb", p.cab.levelDb); set (cab, "mix", p.cab.mix);
    if (! p.cab.irFile.empty()) set (cab, "irFile", juce::String (p.cab.irFile));
    set (root, "cabinet", cab);

    juce::Array<juce::var> fx;
    for (auto& e : p.effects)
    {
        auto o = obj(), params = obj();
        set (o, "slotId", juce::String (e.slotId)); set (o, "type", juce::String (e.type));
        set (o, "position", e.pre ? "pre" : "post"); set (o, "enabled", e.enabled);
        const auto* info = EffectRegistry::get().find (e.type);
        float mix = e.mix;
        if (mix < 0.0f) mix = info ? info->defaultMix : 1.0f;
        if (info && info->hasMix) set (o, "mix", mix);
        // write every parameter of the effect (defaults filled in) so the file is complete
        if (info)
            for (auto& spec : info->params)
            {
                auto it = e.params.find (spec.key);
                set (params, spec.key.c_str(), it != e.params.end() ? it->second : spec.defaultValue);
            }
        set (o, "parameters", params);
        fx.add (o);
    }
    set (root, "effects", fx);

    auto eq = obj();
    set (eq, "enabled", p.eq.enabled); set (eq, "lowDb", p.eq.lowDb); set (eq, "midDb", p.eq.midDb);
    set (eq, "midFreqHz", p.eq.midFreqHz); set (eq, "highDb", p.eq.highDb);
    set (root, "masterEq", eq);

    auto lim = obj();
    set (lim, "enabled", p.limiter.enabled); set (lim, "ceilingDb", p.limiter.ceilingDb);
    set (root, "limiter", lim);

    juce::Array<juce::var> macros;
    for (auto& m : p.macros)
    {
        auto o = obj();
        set (o, "macro", juce::String (macroIdFor (m.macro))); set (o, "target", juce::String (m.target)); set (o, "amount", m.amount);
        macros.add (o);
    }
    set (root, "macros", macros);
    if (p.tempoBpm > 0.0f) set (root, "tempoBpm", p.tempoBpm);
    set (root, "outputGainDb", p.outputGainDb);
    return root;
}

juce::String PresetSerializer::toJson (const Preset& p)
{
    return juce::JSON::toString (toVar (p), false, 4);
}

const char* PresetSerializer::macroIdFor (int m)
{
    static const char* ids[] { "drive", "tone", "space", "output" };
    return ids[std::clamp (m, 0, 3)];
}

ParseResult PresetSerializer::fromVar (const juce::var& v, Preset& out)
{
    ParseResult r;
    if (! v.isObject()) { r.errors.push_back ("preset is not a JSON object"); return r; }

    Preset p;
    const int version = v.hasProperty ("schemaVersion") ? (int) v["schemaVersion"] : 0;
    if (version > kPresetSchemaVersion)
    {
        r.errors.push_back ("preset schema version " + std::to_string (version) + " is newer than this plugin supports ("
                            + std::to_string (kPresetSchemaVersion) + ")");
        return r;
    }
    if (version < 0) { r.errors.push_back ("invalid schema version"); return r; }

    p.schemaVersion = kPresetSchemaVersion; // migrated in memory
    p.name = getS (v, "name");
    if (p.name.empty()) r.errors.push_back ("missing name");
    p.id = getS (v, "id");
    p.category = getS (v, "category", "User");
    p.description = getS (v, "description");
    p.author = getS (v, "author", "");
    if (v.hasProperty ("tags"))
    {
        if (auto* arr = v["tags"].getArray()) for (auto& t : *arr) p.tags.push_back (t.toString().toStdString());
        else r.errors.push_back ("tags must be an array");
    }
    p.inputGainDb = getF (v, "inputGainDb", 0.0f, &r);
    p.outputGainDb = getF (v, "outputGainDb", 0.0f, &r);
    p.tempoBpm = getF (v, "tempoBpm", 0.0f, &r);

    const auto& gate = v["gate"];
    p.gate.enabled = getB (gate, "enabled", false);
    p.gate.thresholdDb = getF (gate, "thresholdDb", -60.0f, &r, "gate.");
    p.gate.releaseMs = getF (gate, "releaseMs", 80.0f, &r, "gate.");

    const auto& amp = v["amp"];
    if (amp.isObject())
    {
        p.amp.type = getS (amp, "type", "studio_clean");
        if (ampTypeIndex (p.amp.type) < 0) r.errors.push_back ("unknown amp type '" + p.amp.type + "'");
        p.amp.enabled = getB (amp, "enabled", true);
        // schema 0 stored amp values under "params"
        const auto& ap = amp.hasProperty ("parameters") ? amp["parameters"] : amp["params"];
        p.amp.gain = getF (ap, "gain", p.amp.gain, &r, "amp."); p.amp.bass = getF (ap, "bass", p.amp.bass, &r, "amp.");
        p.amp.mid = getF (ap, "mid", p.amp.mid, &r, "amp."); p.amp.treble = getF (ap, "treble", p.amp.treble, &r, "amp.");
        p.amp.presence = getF (ap, "presence", p.amp.presence, &r, "amp."); p.amp.master = getF (ap, "master", p.amp.master, &r, "amp.");
        p.amp.sag = getF (ap, "sag", p.amp.sag, &r, "amp."); p.amp.trimDb = getF (ap, "trimDb", p.amp.trimDb, &r, "amp.");
    }
    else if (v.hasProperty ("amp")) r.errors.push_back ("amp must be an object");

    const auto& cab = v.hasProperty ("cabinet") ? v["cabinet"] : v["cab"];
    if (cab.isObject())
    {
        p.cab.type = getS (cab, "type", "closed_4x12");
        if (cabTypeIndex (p.cab.type) < 0) r.errors.push_back ("unknown cabinet type '" + p.cab.type + "'");
        p.cab.enabled = getB (cab, "enabled", true);
        p.cab.mic = getF (cab, "mic", p.cab.mic, &r, "cabinet."); p.cab.lowCutHz = getF (cab, "lowCutHz", p.cab.lowCutHz, &r, "cabinet.");
        p.cab.highCutHz = getF (cab, "highCutHz", p.cab.highCutHz, &r, "cabinet."); p.cab.levelDb = getF (cab, "levelDb", 0.0f, &r, "cabinet.");
        p.cab.mix = getF (cab, "mix", 1.0f, &r, "cabinet."); p.cab.irFile = getS (cab, "irFile");
    }

    if (v.hasProperty ("effects"))
    {
        auto* arr = v["effects"].getArray();
        if (arr == nullptr) r.errors.push_back ("effects must be an array");
        else
        {
            if (arr->size() > 8) r.errors.push_back ("more than 8 effects");
            for (auto& e : *arr)
            {
                if (! e.isObject()) { r.errors.push_back ("effect entry is not an object"); continue; }
                EffectState s;
                s.type = getS (e, "type");
                s.slotId = getS (e, "slotId");
                const auto pos = getS (e, "position", "pre");
                if (pos != "pre" && pos != "post") r.errors.push_back ("effect position must be 'pre' or 'post'");
                s.pre = pos != "post";
                s.enabled = e.hasProperty ("bypass") ? ! getB (e, "bypass", false) : getB (e, "enabled", true); // schema 0 used "bypass"
                s.mix = getF (e, "mix", -1.0f, &r, "effect.");
                const auto* info = EffectRegistry::get().find (s.type);
                if (info == nullptr || info->index == 0) { r.errors.push_back ("unknown effect type '" + s.type + "'"); continue; }
                if (auto* po = e["parameters"].getDynamicObject())
                    for (auto& prop : po->getProperties())
                    {
                        const std::string key = prop.name.toString().toStdString();
                        if (key == "mix") { s.mix = (float) (double) prop.value; continue; }
                        if (info->findParam (key) < 0) { r.warnings.push_back ("ignored unknown parameter '" + key + "' on " + s.type); continue; }
                        if (! (prop.value.isDouble() || prop.value.isInt() || prop.value.isInt64())) { r.errors.push_back (key + " is not a number"); continue; }
                        s.params[key] = (float) (double) prop.value;
                    }
                if (arr->size() <= 8) p.effects.push_back (std::move (s));
            }
        }
    }

    const auto& eq = v["masterEq"];
    p.eq.enabled = getB (eq, "enabled", true);
    p.eq.lowDb = getF (eq, "lowDb", 0.0f, &r, "masterEq."); p.eq.midDb = getF (eq, "midDb", 0.0f, &r, "masterEq.");
    p.eq.midFreqHz = getF (eq, "midFreqHz", 800.0f, &r, "masterEq."); p.eq.highDb = getF (eq, "highDb", 0.0f, &r, "masterEq.");
    const auto& lim = v["limiter"];
    p.limiter.enabled = getB (lim, "enabled", true);
    p.limiter.ceilingDb = getF (lim, "ceilingDb", -0.5f, &r, "limiter.");

    if (auto* arr = v["macros"].getArray())
        for (auto& m : *arr)
        {
            MacroMapping mm;
            const auto id = getS (m, "macro");
            mm.macro = id == "drive" ? 0 : id == "tone" ? 1 : id == "space" ? 2 : id == "output" ? 3 : -1;
            mm.target = getS (m, "target");
            mm.amount = getF (m, "amount", 0.25f, &r, "macro.");
            if (mm.macro < 0 || mm.target.empty()) { r.errors.push_back ("invalid macro mapping"); continue; }
            p.macros.push_back (mm);
        }

    if (p.id.empty())
        p.id = "user." + juce::String (p.name).toLowerCase().replaceCharacters (" /\\", "---").retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789-._").toStdString();

    // range checks are clamped on apply; report them as warnings here (strict checks: PresetValidator)
    for (auto& e : PresetValidator::validate (p)) r.warnings.push_back (e);

    r.ok = r.errors.empty();
    if (r.ok) out = std::move (p);
    return r;
}

ParseResult PresetSerializer::fromJson (const juce::String& json, Preset& out)
{
    juce::var v;
    auto res = juce::JSON::parse (json, v);
    if (res.failed()) { ParseResult r; r.errors.push_back ("invalid JSON: " + res.getErrorMessage().toStdString()); return r; }
    return fromVar (v, out);
}

juce::String PresetSerializer::bankToJson (const juce::String& bankName, const std::vector<Preset>& presets)
{
    auto root = obj();
    set (root, "bankFormat", 1);
    set (root, "name", bankName);
    juce::Array<juce::var> arr;
    for (auto& p : presets) arr.add (toVar (p));
    set (root, "presets", arr);
    return juce::JSON::toString (root, false, 4);
}

ParseResult PresetSerializer::bankFromJson (const juce::String& json, std::vector<Preset>& out)
{
    ParseResult r;
    juce::var v;
    auto res = juce::JSON::parse (json, v);
    if (res.failed()) { r.errors.push_back ("invalid JSON: " + res.getErrorMessage().toStdString()); return r; }
    if (! v.isObject() || (int) v["bankFormat"] < 1) { r.errors.push_back ("not a Pedal Faster bank file"); return r; }
    if ((int) v["bankFormat"] > 1) { r.errors.push_back ("bank format is newer than this plugin supports"); return r; }
    auto* arr = v["presets"].getArray();
    if (arr == nullptr) { r.errors.push_back ("bank has no presets array"); return r; }
    for (auto& pv : *arr)
    {
        Preset p;
        auto pr = fromVar (pv, p);
        if (pr.ok) out.push_back (std::move (p));
        else for (auto& e : pr.errors) r.errors.push_back (e);
    }
    r.ok = ! out.empty();
    return r;
}

//==============================================================================
std::vector<std::string> PresetValidator::validate (const Preset& p)
{
    std::vector<std::string> e;
    auto range = [&e] (const std::string& what, float v, float lo, float hi)
    {
        if (! std::isfinite (v) || v < lo - 1.0e-4f || v > hi + 1.0e-4f)
            e.push_back (what + " = " + std::to_string (v) + " is outside [" + std::to_string (lo) + ", " + std::to_string (hi) + "]");
    };
    if (p.name.empty()) e.push_back ("missing name");
    if (p.id.empty()) e.push_back ("missing id");
    if (p.schemaVersion != kPresetSchemaVersion) e.push_back ("unsupported schema version");
    range ("inputGainDb", p.inputGainDb, -24, 24);
    range ("outputGainDb", p.outputGainDb, -24, 12);
    range ("gate.thresholdDb", p.gate.thresholdDb, -90, -20);
    range ("gate.releaseMs", p.gate.releaseMs, 5, 500);
    if (ampTypeIndex (p.amp.type) < 0) e.push_back ("unknown amp type " + p.amp.type);
    for (auto [n, v] : { std::pair<const char*, float> { "gain", p.amp.gain }, { "bass", p.amp.bass }, { "mid", p.amp.mid },
                         { "treble", p.amp.treble }, { "presence", p.amp.presence }, { "master", p.amp.master }, { "sag", p.amp.sag } })
        range (std::string ("amp.") + n, v, 0, 1);
    range ("amp.trimDb", p.amp.trimDb, -24, 12);
    if (cabTypeIndex (p.cab.type) < 0) e.push_back ("unknown cabinet type " + p.cab.type);
    range ("cabinet.mic", p.cab.mic, 0, 1);
    range ("cabinet.lowCutHz", p.cab.lowCutHz, 20, 400);
    range ("cabinet.highCutHz", p.cab.highCutHz, 2000, 20000);
    range ("cabinet.levelDb", p.cab.levelDb, -24, 12);
    range ("cabinet.mix", p.cab.mix, 0, 1);
    range ("masterEq.lowDb", p.eq.lowDb, -12, 12); range ("masterEq.midDb", p.eq.midDb, -12, 12);
    range ("masterEq.midFreqHz", p.eq.midFreqHz, 200, 5000); range ("masterEq.highDb", p.eq.highDb, -12, 12);
    range ("limiter.ceilingDb", p.limiter.ceilingDb, -12, 0);
    if (p.tempoBpm != 0.0f) range ("tempoBpm", p.tempoBpm, 40, 240);
    if (p.effects.size() > 8) e.push_back ("more than 8 effects");
    std::vector<std::string> slotIds;
    for (auto& fx : p.effects)
    {
        const auto* info = EffectRegistry::get().find (fx.type);
        if (info == nullptr || info->index == 0) { e.push_back ("unknown effect type " + fx.type); continue; }
        if (! fx.slotId.empty())
        {
            if (std::find (slotIds.begin(), slotIds.end(), fx.slotId) != slotIds.end()) e.push_back ("duplicate slotId " + fx.slotId);
            slotIds.push_back (fx.slotId);
        }
        if (fx.mix >= 0.0f) range (fx.type + ".mix", fx.mix, 0, 1);
        for (auto& [k, v] : fx.params)
        {
            const int idx = info->findParam (k);
            if (idx < 0) { e.push_back ("unknown parameter " + fx.type + "." + k); continue; }
            const auto& spec = info->params[(size_t) idx];
            range (fx.type + "." + k, v, spec.realMin(), spec.realMax());
        }
    }
    for (auto& m : p.macros)
    {
        if (m.macro < 0 || m.macro > 3) e.push_back ("invalid macro index");
        range ("macro.amount", m.amount, -1, 1);
    }
    return e;
}
} // namespace pf
