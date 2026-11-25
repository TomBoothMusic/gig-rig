#pragma once

#include "AudioEngine.h"

#include <array>
#include <functional>
#include <vector>

namespace gigrig
{

/** What one song asks of the effects (built 25 Nov 2026, the song-linking batch; the guide is
    docs/songs.md). It is a small text file in Documents/GigRig/Songs, one per entry in the
    Song box, that Tom or Claude can write by hand:

        program = 11
        looper = Wicked Game
        chain 1 = Comp & GE7 for Electric
        chain 1 = Clean amp standard Verb & Modulations
        set = chain 1 / spring / reverb = 3.5
        set = chain 1 / tremolo / off
        switch CTL4 = chain 1 / next amp
        switch CTL3 = chain 3 / delay throw
        amps = chain 1 / Dream 65 - Clean / Dream 65 - Break Up

    A song NAMES presets (the files in Documents/GigRig/Effect presets); it does not copy them.
    So editing a preset changes every song that names it. 'set' lines are the song's own
    changes on top. Chains are "chain 1" to "chain 4", or the chain's own name on the Effects tab. */
struct SongSounds
{
    struct ChainLine    { juce::String chain; juce::StringArray presets; };
    struct Setting      { juce::String chain, effect, setting, value; };     // setting "on": value "1" or "0"
    struct Switch       { int sw = -1; juce::String chain, action; };
    struct AmpList      { juce::String chain; juce::StringArray models; };

    int program = 0;                    // 1 to 128 as Songbook Pro and the RC-600 count them, 0 = none
    juce::String looper;                // the looper layout to use (a built-in song's name); empty = its own, or the general one
    std::vector<ChainLine> chains;      // in file order; a chain named twice stacks its presets, as "Add to chain" does
    std::vector<Setting> settings;
    bool hasSwitches = false;           // true when the file says anything about switches (even "switches = none")
    std::vector<Switch> switches;
    std::vector<AmpList> amps;
    juce::StringArray notUnderstood;    // lines the reader could not use, for the report

    bool saysAnything() const noexcept  { return ! chains.empty() || ! settings.empty() || hasSwitches || ! amps.empty(); }

    juce::String toText (const juce::String& title, const juce::StringArray& chainNames = {}) const;
    static SongSounds fromText (const juce::String& text);
};

/** A song's sounds worked out against the presets and the chains as they are now. */
struct ResolvedSounds
{
    std::array<bool, EffectsRack::numChains> has {};                    // false: leave that chain as it is
    std::array<bool, EffectsRack::numChains> named {};                  // true: built from presets (recipe says which)
    std::array<int, EffectsRack::numChains> from {};                    // 0 = nobody named it, 1 = the song, 2 = the default
    std::array<fx::ChainModel, EffectsRack::numChains> model;
    std::array<juce::StringArray, EffectsRack::numChains> recipe;
    std::array<juce::StringArray, EffectsRack::numChains> amps;
    bool hasSwitches = false;
    std::vector<EffectsRack::PedalAssign> switches;
    juce::StringArray problems;                                         // a preset that is missing, a setting no effect has...
};

using PresetLoader = std::function<bool (const juce::String& name, fx::ChainModel&)>;

/** Which chain a file means by "chain 2", "2" or a chain's own name. -1 = none. */
int chainFromWords (const juce::String& words, const juce::StringArray& chainNames);

/** The slot a file means by "tapedelay", "Tape delay" or "Channel EQ #2". -1 = none. */
int slotFromWords (const fx::ChainModel&, const juce::String& words);

/** "eq" for the first Graphic EQ in a chain, "eq #2" for the second. */
juce::String slotWords (const fx::ChainModel&, int slot);

/** Works a song out. For each chain the song's own lines win; a chain the song does not name
    takes the fallback's (the default sounds); a chain neither names is left alone ('now' is what
    the chains hold, used for switches that point into such a chain). Switches and amp lists the
    same way. Pure: reads presets only through the loader, so the automatic builds can check it. */
ResolvedSounds resolveSounds (const SongSounds& song, const SongSounds* fallback, const juce::StringArray& chainNames,
                              const std::array<fx::ChainModel, EffectsRack::numChains>& now, const PresetLoader& loadPreset);

/** Turns a chain as it stands into "these presets, plus these changes". When the chain no
    longer has the presets' effects in the presets' order, there is nothing to name: 'matches'
    comes back false and the caller saves the chain as a preset of the song's own. */
struct ChainAsPresets
{
    bool matches = false;
    std::vector<SongSounds::Setting> changes;
};
ChainAsPresets describeChain (const fx::ChainModel& now, const juce::StringArray& recipe, const juce::String& chainWords, const PresetLoader& loadPreset);

/** Which preset (its place in the recipe) each slot of a chain built from the recipe came from,
    slot by slot. Empty when a preset is missing or would not all fit. */
std::vector<int> slotOwners (const juce::StringArray& recipe, const PresetLoader& loadPreset);

/** Writes a chain's settings as they stand back into the presets it was built from, each preset
    getting its own effects. This is how a preset is edited once for every song that names it.
    Refuses (and says why) when the chain no longer has its presets' effects in their order.
    Returns a line for the screen. */
juce::String saveBackToPresets (EffectsRack&, int chain, const juce::File& presetFolder);

/** Reads presets from a folder by name. */
PresetLoader presetLoaderFor (const juce::File& presetFolder);

/** "next amp", "delay throw"...: the words a song file uses for what a switch does. */
juce::String switchActionWords (int action, const fx::ChainModel& chain);

//==============================================================================
/** The song list: the built-in looper layouts plus every file in the songs folder, and the one
    place a song is loaded from (the Song box, a Songbook Pro program change, a file changed on
    disk). Loading sets the looper's layout and the effects together. Message thread. */
class SongBook
{
public:
    struct Entry
    {
        juce::String name;
        int program = 0;            // 1 to 128, 0 = none
        int layout = 0;             // index into the built-in songs: the looper layout it uses
        bool builtIn = false;       // has a looper layout of its own
        bool hasFile = false;
        SongSounds sounds;
    };

    SongBook (AudioEngine&, const juce::File& songFolder, const juce::File& presetFolder);

    static juce::File defaultSongFolder();      // Documents/GigRig/Songs
    static constexpr const char* defaultFileName = "Default sounds";
    static constexpr const char* generalLayout = "Dirty Slap";      // RC-600 memory 1: Tom's general set-up

    /** Reads the folder again. Keeps the loaded song loaded (by name). */
    void rescan();
    /** Rescans when a file in the folder was added, removed or changed. Call about once a second.
        Returns true if it did. A changed file for the loaded song applies its sounds again. */
    bool rescanIfChanged();

    int numEntries() const noexcept                 { return (int) entries.size(); }
    const Entry& entry (int index) const            { return entries[(size_t) index]; }
    int currentIndex() const noexcept               { return current; }
    const Entry& currentEntry() const               { return entries[(size_t) current]; }
    const SongSetup& song() const noexcept          { return loadedSong; }
    bool hasDefault() const noexcept                { return defaultSounds.saysAnything(); }
    int entryNamed (const juce::String& name) const;
    /** The entry a program number loads: the one that carries it, else -1. */
    int entryForProgram (int program) const;

    /** Loads an entry: the looper's layout (every loop is cleared, as on the RC-600) and the sounds.
        withSounds false sets only the looper's layout (the app's start: the effects come back
        from the rack's own memory instead). */
    void load (int index, const juce::String& why, bool withSounds = true);
    /** Applies the loaded song's sounds again without touching the loops. */
    void applySounds (const juce::String& why);
    /** A program change (1 to 128). The entry that carries the number loads; a number no entry
        carries loads the general set-up (memory 1's looper layout with the default sounds), as
        Tom's shared memories do. The entry already loaded only has its sounds set afresh. */
    void programChange (int program, const juce::String& from);

    /** Writes the loaded song's file from the chains as they stand: for each chain that is on,
        the presets it was built from plus what was changed since, and the switches. */
    juce::String keepForCurrent();
    /** The same into the default sounds: what every song without lines of its own uses. */
    juce::String keepAsDefault();
    /** Removes the loaded song's sound lines (its program number and looper layout stay). */
    juce::String forgetCurrent();
    /** A new entry: the loaded song's looper layout and the sounds as they stand, under a new
        name and program number (0 = none). Returns an error, or an empty string. */
    juce::String addEntry (const juce::String& name, int program);

    /** One line for the screen: "Wicked Game (program 11): its own sounds on chains 1 and 3, default on chain 2." */
    juce::String statusLine() const;
    juce::String lastNote() const                   { return note; }
    juce::StringArray lastProblems() const          { return problems; }
    /** The last few program changes heard, oldest first: "14:02:11  program 11 from WIDI Jack: Wicked Game". */
    juce::StringArray programLines() const          { return programLog; }
    juce::String reportText() const;

    /** After anything changed (a song loaded, the list rescanned), so the screens can follow. */
    std::function<void()> onChanged;

    juce::File folder() const                       { return songFolder; }
    PresetLoader loader() const;

private:
    SongSounds capture (const juce::String& songName, juce::StringArray& notes);
    juce::StringArray chainNames() const;
    juce::String stamp() const;
    bool writeSong (const juce::String& name, const SongSounds&);
    void changed();

    AudioEngine& engine;
    juce::File songFolder, presetFolder;
    std::vector<Entry> entries;
    SongSounds defaultSounds;
    int current = 0;
    SongSetup loadedSong;
    juce::String note, soundsFrom, folderStamp;
    juce::StringArray problems, programLog;
    std::array<int, EffectsRack::numChains> chainFrom {};   // 0 = left alone, 1 = the song's own lines, 2 = the default sounds

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongBook)
};

} // namespace gigrig
