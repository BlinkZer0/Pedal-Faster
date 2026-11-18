#include "PresetManager.h"

namespace pf
{
juce::File PresetManager::defaultRootDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Pedal Faster");
}

PresetManager::PresetManager (juce::File rootDir)
    : root (rootDir == juce::File() ? defaultRootDirectory() : rootDir)
{
    loadFavourites();
    rescanUser();
}

void PresetManager::rescanUser()
{
    userPresets.clear(); userFiles.clear();
    auto dir = userPresetDirectory();
    if (dir.isDirectory())
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false, "*.json");
        files.sort();
        for (auto& f : files)
        {
            Preset p;
            auto r = PresetSerializer::fromJson (f.loadFileAsString(), p);
            if (! r.ok) { DBG ("Skipping invalid preset " << f.getFullPathName()); continue; }
            p.factory = false;
            if (p.id.rfind ("factory.", 0) == 0 || findById (p.id) != nullptr) p.id = makeUserId (p.name);
            userFiles[p.id] = f;
            userPresets.push_back (std::move (p));
        }
    }
    sendChangeMessage();
}

const Preset* PresetManager::findById (const std::string& id) const
{
    for (auto& p : factory()) if (p.id == id) return &p;
    for (auto& p : userPresets) if (p.id == id) return &p;
    return nullptr;
}

bool PresetManager::isFactory (const std::string& id) const
{
    for (auto& p : factory()) if (p.id == id) return true;
    return false;
}

juce::StringArray PresetManager::categories() const
{
    juce::StringArray c;
    for (auto& b : factoryBanks()) c.add (b.name);
    for (auto& p : userPresets) c.addIfNotAlreadyThere (p.category);
    return c;
}

std::vector<const Preset*> PresetManager::query (const juce::String& search, const juce::String& category, Source source, Sort sort) const
{
    std::vector<const Preset*> out;
    juce::StringArray words; words.addTokens (search.toLowerCase(), " ", "\"");
    words.removeEmptyStrings();

    auto matches = [&] (const Preset& p)
    {
        if (category.isNotEmpty() && ! juce::String (p.category).equalsIgnoreCase (category)) return false;
        if (source == Source::Favourites && ! isFavourite (p.id)) return false;
        for (auto& w : words)
        {
            bool hit = juce::String (p.name).toLowerCase().contains (w) || juce::String (p.category).toLowerCase().contains (w)
                    || juce::String (p.description).toLowerCase().contains (w);
            for (auto& t : p.tags) hit = hit || juce::String (t).toLowerCase().contains (w);
            if (! hit) return false;
        }
        return true;
    };
    if (source != Source::User) for (auto& p : factory()) if (matches (p)) out.push_back (&p);
    if (source != Source::Factory) for (auto& p : userPresets) if (matches (p)) out.push_back (&p);

    auto bankIndex = [] (const Preset& p)
    {
        const auto& b = factoryBanks();
        for (size_t i = 0; i < b.size(); ++i) if (b[i].name == p.category) return (int) i;
        return 1000;
    };
    switch (sort)
    {
        case Sort::Bank: std::stable_sort (out.begin(), out.end(), [&] (auto* a, auto* b) { return bankIndex (*a) < bankIndex (*b); }); break;
        case Sort::Name: std::stable_sort (out.begin(), out.end(), [] (auto* a, auto* b) { return juce::String (a->name).compareNatural (b->name) < 0; }); break;
        case Sort::Category: std::stable_sort (out.begin(), out.end(), [] (auto* a, auto* b) { return a->category != b->category ? a->category < b->category : a->name < b->name; }); break;
        case Sort::Gain: std::stable_sort (out.begin(), out.end(), [] (auto* a, auto* b) { return a->gainLevel() < b->gainLevel(); }); break;
    }
    return out;
}

void PresetManager::setFavourite (const std::string& id, bool fav)
{
    if (fav) favourites.insert (id); else favourites.erase (id);
    saveFavourites();
    sendChangeMessage();
}

void PresetManager::loadFavourites()
{
    auto f = root.getChildFile ("favourites.json");
    if (! f.existsAsFile()) return;
    auto v = juce::JSON::parse (f.loadFileAsString());
    if (auto* a = v.getArray()) for (auto& x : *a) favourites.insert (x.toString().toStdString());
}

void PresetManager::saveFavourites() const
{
    juce::Array<juce::var> a;
    for (auto& f : favourites) a.add (juce::String (f));
    root.createDirectory();
    root.getChildFile ("favourites.json").replaceWithText (juce::JSON::toString (a));
}

std::string PresetManager::makeUserId (const std::string& name) const
{
    auto base = juce::String (name).toLowerCase().replaceCharacters (" /\\", "---").retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789-_");
    if (base.isEmpty()) base = "preset";
    for (int i = 0;; ++i)
    {
        auto id = ("user." + base + (i > 0 ? "-" + juce::String (i + 1) : juce::String())).toStdString();
        if (findById (id) == nullptr) return id;
    }
}

juce::File PresetManager::fileFor (const std::string& id) const
{
    auto it = userFiles.find (id);
    if (it != userFiles.end()) return it->second;
    auto name = juce::File::createLegalFileName (juce::String (id).fromFirstOccurrenceOf ("user.", false, false));
    return userPresetDirectory().getChildFile (name + ".json");
}

juce::Result PresetManager::saveUser (Preset p, std::string& storedId)
{
    if (p.name.empty()) return juce::Result::fail ("Preset needs a name");
    const bool overwrite = ! p.id.empty() && ! isFactory (p.id) && userFiles.count (p.id) > 0;
    if (! overwrite) p.id = makeUserId (p.name);
    p.factory = false;
    if (p.category.empty()) p.category = "User";
    if (p.author.empty() || p.author == "Pedal Faster") p.author = "User";
    auto errors = PresetValidator::validate (p);
    if (! errors.empty()) return juce::Result::fail ("Preset is invalid: " + juce::String (errors.front()));

    userPresetDirectory().createDirectory();
    auto f = fileFor (p.id);
    if (! f.replaceWithText (PresetSerializer::toJson (p))) return juce::Result::fail ("Could not write " + f.getFullPathName());
    storedId = p.id;
    rescanUser();
    return juce::Result::ok();
}

juce::Result PresetManager::renameUser (const std::string& id, const juce::String& newName)
{
    if (isFactory (id)) return juce::Result::fail ("Factory presets can't be renamed - save a copy first");
    for (auto& p : userPresets)
        if (p.id == id)
        {
            Preset copy = p; copy.name = newName.toStdString();
            auto f = fileFor (id);
            if (! f.replaceWithText (PresetSerializer::toJson (copy))) return juce::Result::fail ("Could not write file");
            rescanUser();
            return juce::Result::ok();
        }
    return juce::Result::fail ("Preset not found");
}

juce::Result PresetManager::deleteUser (const std::string& id)
{
    if (isFactory (id)) return juce::Result::fail ("Factory presets can't be deleted");
    auto it = userFiles.find (id);
    if (it == userFiles.end()) return juce::Result::fail ("Preset not found");
    if (! it->second.deleteFile()) return juce::Result::fail ("Could not delete file");
    favourites.erase (id); saveFavourites();
    rescanUser();
    return juce::Result::ok();
}

juce::Result PresetManager::duplicate (const std::string& id, std::string& newId)
{
    auto* p = findById (id);
    if (p == nullptr) return juce::Result::fail ("Preset not found");
    Preset copy = *p;
    copy.id.clear(); copy.name += " Copy";
    if (copy.factory) { copy.category = copy.category; copy.factory = false; }
    return saveUser (copy, newId);
}

juce::Result PresetManager::exportPreset (const Preset& p, const juce::File& file) const
{
    return file.replaceWithText (PresetSerializer::toJson (p)) ? juce::Result::ok() : juce::Result::fail ("Could not write " + file.getFullPathName());
}

juce::Result PresetManager::importPreset (const juce::File& file, std::string& newId)
{
    Preset p;
    auto r = PresetSerializer::fromJson (file.loadFileAsString(), p);
    if (! r.ok) return juce::Result::fail (r.errors.empty() ? "Invalid preset" : juce::String (r.errors.front()));
    p.id.clear();
    return saveUser (p, newId);
}

juce::Result PresetManager::exportBank (const juce::String& name, const std::vector<const Preset*>& presets, const juce::File& file) const
{
    std::vector<Preset> copies;
    for (auto* p : presets) copies.push_back (*p);
    return file.replaceWithText (PresetSerializer::bankToJson (name, copies)) ? juce::Result::ok() : juce::Result::fail ("Could not write bank");
}

juce::Result PresetManager::importBank (const juce::File& file, int& imported)
{
    std::vector<Preset> list;
    auto r = PresetSerializer::bankFromJson (file.loadFileAsString(), list);
    imported = 0;
    if (! r.ok) return juce::Result::fail (r.errors.empty() ? "Invalid bank" : juce::String (r.errors.front()));
    for (auto& p : list)
    {
        p.id.clear();
        std::string id;
        if (saveUser (p, id).wasOk()) ++imported;
    }
    return juce::Result::ok();
}
} // namespace pf
