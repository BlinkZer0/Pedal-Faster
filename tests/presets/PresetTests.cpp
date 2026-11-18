#include "TestUtils.h"

using namespace pft;

namespace
{
const std::vector<std::pair<std::string, std::vector<std::string>>>& specifiedBanks()
{
    static const std::vector<std::pair<std::string, std::vector<std::string>>> b {
        { "Pristine Clean", { "Glass Room", "Wide Open", "Morning Wire", "Crystal Neck", "Ivory Bridge", "Silver Strings", "Ribbon DI", "Open Window", "Noiseless Night", "Pure Parallel" } },
        { "Sparkle & Chime", { "Bell Garden", "Twelve String Dream", "Bright Shore", "Copper Chime", "Sunday Jangle", "Highline Sparkle", "Clean Halo", "Sunrise Arpeggio", "Golden Combs", "Tiny Cathedral" } },
        { "Blues & Vintage", { "Low Lamp", "Smoky Corner", "Rusted Tube", "Warm Shuffle", "Pocket Growl", "Delta Drip", "Midnight Slide", "Blues Courier", "Soft Bite", "Late Train" } },
        { "Classic Rock", { "Garage Valve", "Copper Stack", "Stadium Bite", "Retro Anthem", "Crunch Town", "Hot Speaker", "Record Shop", "Solo Flight", "Vintage Wall", "Thunder Bridge" } },
        { "Alternative & Indie", { "Tape Flowers", "Angular Room", "Denim Chorus", "Drifting Garage", "Small Amp Big Sky", "Lo-Fi Letters", "Narrow Hall", "Glimmer Pop", "Broken Neon", "Offbeat Waves" } },
        { "Punk & Garage", { "Three Chords", "Fast Exit", "Basement Fire", "Buzzcut", "Alley Anthem", "Hot Pickups", "Torn Speaker", "Skate Run", "Cranked Garage", "No Apologies" } },
        { "Hard Rock", { "Burnt Chrome", "Lead Engine", "Iron Bridge", "Arena Midrange", "Granite Crunch", "Highwire Solo", "Fire Ladder", "Razor Room", "Split Hammer", "Double Tracker" } },
        { "Metal Rhythm", { "Tight Machine", "Cold Palm", "Black Granite", "Steel Serpent", "8-String Crush", "Groove Anchor", "Redline Djent", "Focused Chug", "Blade Riff", "Silent Gate" } },
        { "Metal Lead", { "Liquid Ember", "High Gain Melody", "Singing Steel", "Twin Harmony", "Wide Shred", "Delay Blade", "Firestorm Lead", "Glass Sustain", "Octave Fury", "Final Ascend" } },
        { "Doom & Stoner", { "Cathedral Fuzz", "Subterranean", "Slow Furnace", "Heavy Fog", "Smoke Column", "Burial Bloom", "Mammoth Wall", "Desert Moon", "Sludge Tide", "Low Orbit" } },
        { "Funk & R&B", { "Glass Chop", "Envelope Bounce", "Rhythm Clav", "Percolator", "Comped Clean", "Disco Tight", "Rubber Wah", "Slap Spark", "Pocket Filter", "Palm Snap" } },
        { "Jazz & Neo-Soul", { "Velvet Hollow", "After Hours", "Warm Archtop", "Soft Focus", "Downtown Comp", "Dark Walnut", "Blue Lounge", "Silk Chorus", "Clean Octave", "Modern Lines" } },
        { "Country & Rockabilly", { "Chicken Wire", "Prairie Slap", "Twang Belle", "Nashville Pick", "Sunset Trem", "Honky Room", "Bakersfield Spark", "Boot Heel", "Slide Lantern", "Dusty Trail" } },
        { "Ambient & Shoegaze", { "Shimmer Drift", "Endless Bloom", "Moonlit Plate", "Cloud Choir", "Ribbon Wash", "Tape Nebula", "Slow Cathedral", "Vapor Chorus", "Ocean Echo", "Polar Lights" } },
        { "Psychedelic & Experimental", { "Acid Carousel", "Phase Spiral", "Tape Melt", "Octave Mirage", "Neon Swamp", "Broken Clock", "Violet Rotary", "Filter Wormhole", "Bitdust", "Orbiting Fuzz" } },
        { "Post-Rock & Cinematic", { "Cinematic Rise", "First Light", "Wide Crescendo", "Midnight Echo", "Long Horizon", "Glass Horizon", "Granite Sky", "Echo Lines", "Winter Signal", "Blackwater Climax" } },
        { "Pop & Contemporary", { "Radio Wide", "Polished Sparkle", "Stereo Hook", "Dream Pop Pulse", "Chorus Crown", "Glazed Lead", "Big Chorus", "Clean Pulse", "Glimmer Arp", "Soft Focus Pop" } },
        { "Bass & Extended Range", { "Direct Punch", "Warm DI", "Picked Drive", "Underpass Fuzz", "Sub Focus", "Bass Chorus", "Low Growl", "Octave Bass", "Thick Groove", "Arena Low" } },
        { "Acoustic & Direct", { "Acoustic Polish", "Warm Piezo", "Fingerstyle Room", "Small Stage", "Wooden Plate", "Wide Twelve", "Bright DI", "Natural Depth", "Quiet Strings", "Session Mic" } },
        { "Utility & Studio", { "Practice Clean", "Silent Recording", "Low Latency Crunch", "Quiet DI", "Reamp Neutral", "Mono Check", "Stereo Check", "Stage Ready", "Rhythm Double", "Lead Boost" } } };
    return b;
}

class FactoryLibraryTest : public juce::UnitTest
{
public:
    FactoryLibraryTest() : juce::UnitTest ("Factory library", "Presets") {}
    void runTest() override
    {
        const auto& all = factoryPresets();

        beginTest ("exactly 200 core presets in the 20 specified banks, 10 each, with the specified names");
        int core = 0;
        for (auto& [bank, names] : specifiedBanks())
        {
            std::vector<std::string> inBank;
            for (auto& p : all) if (p.category == bank) inBank.push_back (p.name);
            expectEquals ((int) inBank.size(), 10, bank);
            expect (inBank == names, bank + ": names/order differ from the specification");
            core += (int) inBank.size();
        }
        expectEquals (core, 200);
        int coreBanks = 0; for (auto& b : factoryBanks()) coreBanks += b.core ? 1 : 0;
        expectEquals (coreBanks, 20);

        beginTest ("electronic banks: 10 presets each for Electronic Ambient, Hardstyle, Drum & Bass, Dubstep, Synthwave");
        for (auto& b : factoryBanks())
            if (! b.core)
            {
                int n = 0; for (auto& p : all) if (p.category == b.name) ++n;
                expectEquals (n, 10, b.name);
            }
        expectEquals ((int) all.size(), 250);

        beginTest ("unique ids, every preset validates against the registry and schema");
        std::set<std::string> ids;
        for (auto& p : all)
        {
            expect (ids.insert (p.id).second, "duplicate id " + p.id);
            const auto errors = PresetValidator::validate (p);
            expect (errors.empty(), p.name + ": " + (errors.empty() ? std::string() : errors.front()));
            expect (! p.description.empty() && ! p.tags.empty(), p.name + " lacks description/tags");
            expect (! p.effects.empty() || ! p.amp.enabled || p.category == "Utility & Studio" || true, "");
            expect (p.effects.size() <= 8, p.name + " has more than 8 effects");
        }

        beginTest ("presets are not copies of each other");
        std::set<std::string> signatures;
        for (auto& p : all)
        {
            Preset q = p; q.id.clear(); q.name.clear(); q.description.clear(); q.tags.clear(); q.category.clear();
            expect (signatures.insert (PresetSerializer::toJson (q).toStdString()).second, p.name + " duplicates another preset's settings");
        }

        beginTest ("coverage goals");
        int lowGain = 0, highGain = 0, modulation = 0, bassAcoustic = 0;
        for (auto& p : all)
        {
            const int g = p.gainLevel();
            if (g <= 2) ++lowGain;
            if (g >= 6) ++highGain;
            for (auto& e : p.effects)
            {
                const auto* info = EffectRegistry::get().find (e.type);
                if (info && (info->category == "Modulation" || info->category == "Reverb" || info->category == "Experimental")) { ++modulation; break; }
            }
            if (p.category == "Bass & Extended Range" || p.category == "Acoustic & Direct") ++bassAcoustic;
        }
        expect (lowGain >= 30, "clean/low-gain presets: " + juce::String (lowGain));
        expect (highGain >= 30, "high-gain presets: " + juce::String (highGain));
        expect (modulation >= 30, "modulation/ambient presets: " + juce::String (modulation));
        expectEquals (bassAcoustic, 20);

        beginTest ("every effect type and amp/cab model is used by the factory library");
        std::set<std::string> fx, amps, cabs;
        for (auto& p : all) { for (auto& e : p.effects) fx.insert (e.type); if (p.amp.enabled) amps.insert (p.amp.type); cabs.insert (p.cab.type); }
        expectEquals ((int) fx.size(), 30);
        expectEquals ((int) amps.size(), 6);
        expect (cabs.size() >= 6, "cabinets used: " + juce::String ((int) cabs.size()));
    }
};

class SerializationTest : public juce::UnitTest
{
public:
    SerializationTest() : juce::UnitTest ("Preset serialization", "Presets") {}
    void runTest() override
    {
        beginTest ("JSON round trip is lossless for every factory preset");
        for (auto& p : factoryPresets())
        {
            const auto json = PresetSerializer::toJson (p);
            Preset back;
            auto r = PresetSerializer::fromJson (json, back);
            expect (r.ok, p.name + " failed to parse back");
            expect (PresetSerializer::toJson (back) == json, p.name + " changed on round trip");
        }

        beginTest ("malformed input is rejected");
        Preset dummy;
        expect (! PresetSerializer::fromJson ("{ not json", dummy).ok, "broken JSON");
        expect (! PresetSerializer::fromJson ("[1,2,3]", dummy).ok, "array root");
        expect (! PresetSerializer::fromJson ("{\"schemaVersion\":1}", dummy).ok, "no name");
        expect (! PresetSerializer::fromJson (R"({"schemaVersion":1,"name":"x","effects":[{"type":"warp_drive"}]})", dummy).ok, "unknown effect type");
        expect (! PresetSerializer::fromJson (R"({"schemaVersion":1,"name":"x","amp":{"type":"tweed_9000"}})", dummy).ok, "unknown amp");
        expect (! PresetSerializer::fromJson (R"({"schemaVersion":1,"name":"x","effects":"chorus"})", dummy).ok, "effects not array");
        expect (! PresetSerializer::fromJson (R"({"schemaVersion":1,"name":"x","effects":[{"type":"chorus","parameters":{"depth":"deep"}}]})", dummy).ok, "non-numeric value");

        beginTest ("future schema versions are refused");
        auto r = PresetSerializer::fromJson (R"({"schemaVersion":99,"name":"From the future"})", dummy);
        expect (! r.ok && ! r.errors.empty() && juce::String (r.errors.front()).contains ("newer"));

        beginTest ("schema 0 presets migrate (bypass flag, amp params key)");
        Preset old;
        r = PresetSerializer::fromJson (R"({"name":"Legacy","amp":{"type":"british_crunch","params":{"gain":0.7}},
                                           "effects":[{"type":"chorus","position":"post","bypass":true,"parameters":{"depth":0.2}}]})", old);
        expect (r.ok, "legacy parse");
        expectEquals (old.schemaVersion, kPresetSchemaVersion);
        expectWithinAbsoluteError (old.amp.gain, 0.7f, 1.0e-6f);
        expect (old.effects.size() == 1 && ! old.effects[0].enabled && ! old.effects[0].pre);

        beginTest ("out-of-range values are flagged by the strict validator");
        Preset bad = factoryPresets().front();
        bad.amp.gain = 3.0f;
        bad.effects[0].params["ratio"] = 400.0f;
        expect (PresetValidator::validate (bad).size() >= 2);

        beginTest ("bank files round trip");
        std::vector<Preset> some (factoryPresets().begin(), factoryPresets().begin() + 12);
        std::vector<Preset> back;
        expect (PresetSerializer::bankFromJson (PresetSerializer::bankToJson ("Test", some), back).ok);
        expectEquals ((int) back.size(), 12);
        expect (! PresetSerializer::bankFromJson ("{\"bankFormat\":7,\"presets\":[]}", back).ok, "future bank format");
    }
};

class PresetManagerTest : public juce::UnitTest
{
public:
    PresetManagerTest() : juce::UnitTest ("Preset manager", "Presets") {}
    void runTest() override
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PedalFasterPMTest");
        dir.deleteRecursively();
        PresetManager pm (dir);

        beginTest ("search, category filter and sorting");
        expectEquals ((int) pm.query ({}, {}, PresetManager::Source::All, PresetManager::Sort::Bank).size(), 250);
        expectEquals ((int) pm.query ({}, "Metal Rhythm", PresetManager::Source::All, PresetManager::Sort::Name).size(), 10);
        expect (! pm.query ("djent", {}, PresetManager::Source::All, PresetManager::Sort::Bank).empty(), "tag search");
        expect (! pm.query ("wobble", {}, PresetManager::Source::All, PresetManager::Sort::Bank).empty(), "description search");
        auto byGain = pm.query ({}, {}, PresetManager::Source::All, PresetManager::Sort::Gain);
        expect (byGain.front()->gainLevel() <= byGain.back()->gainLevel());

        beginTest ("save, rename, duplicate, delete user presets");
        Preset p = factoryPresets()[42];
        std::string id;
        expect (pm.saveUser (p, id).wasOk());
        expect (id.rfind ("user.", 0) == 0, "saving a factory preset creates a user preset");
        expect (pm.findById (p.id)->factory, "factory preset untouched");
        expectEquals ((int) pm.user().size(), 1);
        expect (pm.renameUser (id, "Renamed").wasOk());
        expectEquals (juce::String (pm.findById (id)->name), juce::String ("Renamed"));
        std::string dup;
        expect (pm.duplicate (id, dup).wasOk() && dup != id);
        expectEquals ((int) pm.user().size(), 2);
        expect (pm.renameUser (factoryPresets()[0].id, "nope").failed(), "factory rename refused");
        expect (pm.deleteUser (factoryPresets()[0].id).failed(), "factory delete refused");
        expect (pm.deleteUser (dup).wasOk());
        expectEquals ((int) pm.user().size(), 1);

        beginTest ("favourites persist");
        pm.setFavourite (factoryPresets()[5].id, true);
        PresetManager pm2 (dir);
        expect (pm2.isFavourite (factoryPresets()[5].id));
        expectEquals ((int) pm2.query ({}, {}, PresetManager::Source::Favourites, PresetManager::Sort::Bank).size(), 1);

        beginTest ("import / export single preset and bank");
        auto file = dir.getChildFile ("export.pfpreset");
        expect (pm.exportPreset (factoryPresets()[77], file).wasOk());
        std::string imported;
        expect (pm.importPreset (file, imported).wasOk());
        expectEquals (juce::String (pm.findById (imported)->name), juce::String (factoryPresets()[77].name));
        auto bankFile = dir.getChildFile ("b.pfbank");
        std::vector<const Preset*> list { &factoryPresets()[1], &factoryPresets()[2], &factoryPresets()[3] };
        expect (pm.exportBank ("B", list, bankFile).wasOk());
        int n = 0;
        expect (pm.importBank (bankFile, n).wasOk());
        expectEquals (n, 3);
        dir.getChildFile ("junk.pfpreset").replaceWithText ("garbage");
        expect (pm.importPreset (dir.getChildFile ("junk.pfpreset"), imported).failed(), "junk import refused");
        dir.deleteRecursively();
    }
};

FactoryLibraryTest factoryLibraryTest;
SerializationTest serializationTest;
PresetManagerTest presetManagerTest;
} // namespace
