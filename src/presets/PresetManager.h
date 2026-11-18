#pragma once
// Preset library: immutable factory presets + user presets stored as JSON files in
// %APPDATA%\Pedal Faster\Presets (or a directory supplied for tests). Message thread only.

#include "FactoryPresets.h"
#include "PresetSerializer.h"
#include <juce_events/juce_events.h>
#include <set>

namespace pf
{
class PresetManager : public juce::ChangeBroadcaster
{
public:
    enum class Source { All = 0, Factory, User, Favourites };
    enum class Sort { Bank = 0, Name, Category, Gain };

    /** rootDir defaults to %APPDATA%\Pedal Faster */
    explicit PresetManager (juce::File rootDir = {});

    static juce::File defaultRootDirectory();
    juce::File userPresetDirectory() const { return root.getChildFile ("Presets"); }
    juce::File rootDirectory() const { return root; }

    const std::vector<Preset>& factory() const { return factoryPresets(); }
    const std::vector<Preset>& user() const { return userPresets; }
    const Preset* findById (const std::string& id) const;
    bool isFactory (const std::string& id) const;

    std::vector<const Preset*> query (const juce::String& search, const juce::String& category, Source source, Sort sort) const;
    juce::StringArray categories() const;

    bool isFavourite (const std::string& id) const { return favourites.count (id) > 0; }
    void setFavourite (const std::string& id, bool fav);

    /** Saves a user preset (new id when the preset is a factory one or has no id). Returns the stored id. */
    juce::Result saveUser (Preset p, std::string& storedId);
    juce::Result renameUser (const std::string& id, const juce::String& newName);
    juce::Result deleteUser (const std::string& id);
    juce::Result duplicate (const std::string& id, std::string& newId);

    juce::Result exportPreset (const Preset& p, const juce::File& file) const;
    juce::Result importPreset (const juce::File& file, std::string& newId);
    juce::Result exportBank (const juce::String& name, const std::vector<const Preset*>& presets, const juce::File& file) const;
    juce::Result importBank (const juce::File& file, int& imported);

    void rescanUser();

private:
    juce::File fileFor (const std::string& id) const;
    std::string makeUserId (const std::string& name) const;
    void loadFavourites();
    void saveFavourites() const;

    juce::File root;
    std::vector<Preset> userPresets;
    std::map<std::string, juce::File> userFiles;
    std::set<std::string> favourites;
};
} // namespace pf
