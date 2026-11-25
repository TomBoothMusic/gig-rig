#include "SongBook.h"
#include "effects/Registry.h"

#include <map>

namespace gigrig
{

namespace
{
    constexpr int numChains = EffectsRack::numChains;

    juce::String chainWordsFor (int c)      { return "chain " + juce::String (c + 1); }

    juce::String switchWords (int sw)       { return sw < 9 ? juce::String (sw + 1) : sw == 9 ? juce::String ("CTL3") : juce::String ("CTL4"); }

    int switchFromWords (const juce::String& w)
    {
        const auto t = w.trim();
        if (t.equalsIgnoreCase ("ctl3"))    return 9;
        if (t.equalsIgnoreCase ("ctl4"))    return 10;
        const int n = t.getIntValue();
        return t.containsOnly ("0123456789") && n >= 1 && n <= 9 ? n - 1 : -1;
    }

    juce::String squash (const juce::String& s)     { return s.toLowerCase().removeCharacters (" -_'"); }

    /** An effect type's settings, made once (making an amp loads a model, so not every time). */
    struct TypeFacts
    {
        std::vector<fx::ParamInfo> params;
        std::vector<std::string> textKeys;
    };

    const TypeFacts& factsFor (const std::string& type)
    {
        static std::map<std::string, TypeFacts> known;

        if (auto found = known.find (type); found != known.end())
            return found->second;

        TypeFacts f;

        if (auto e = fx::makeEffect (type))
        {
            f.params = e->params();
            f.textKeys = e->textKeys();
        }

        return known[type] = std::move (f);
    }

    juce::String number (float v)
    {
        auto s = juce::String (v, 4);

        if (s.containsChar ('.'))
        {
            while (s.endsWithChar ('0')) s = s.dropLastCharacters (1);
            if (s.endsWithChar ('.'))    s = s.dropLastCharacters (1);
        }

        return s;
    }

    /** Sets one of a slot's settings from a file's words. Returns an error, or an empty string. */
    juce::String applySetting (fx::SlotModel& slot, const juce::String& setting, const juce::String& value)
    {
        if (setting == "on")
        {
            slot.on = value != "0";
            return {};
        }

        const auto& facts = factsFor (slot.type);
        const auto want = squash (setting);

        for (const auto& p : facts.params)
        {
            if (squash (p.id) != want && squash (p.label) != want)
                continue;

            const auto v = value.trim();
            float number = 0.0f;
            bool ok = false;

            if (v.containsOnly ("0123456789.-+eE") && v.containsAnyOf ("0123456789"))
            {
                number = v.getFloatValue();
                ok = true;
            }
            else if (p.kind == fx::ParamInfo::Kind::choice)
            {
                for (size_t i = 0; i < p.choices.size() && ! ok; ++i)
                    if (squash (p.choices[i]) == squash (v))
                    {
                        number = (float) i;
                        ok = true;
                    }
            }
            else if (p.kind == fx::ParamInfo::Kind::toggle)
            {
                ok = true;
                number = v.equalsIgnoreCase ("on") || v.equalsIgnoreCase ("yes") ? 1.0f : 0.0f;
            }

            if (! ok)
                return "\"" + value + "\" is not a value " + juce::String (p.label) + " can take";

            slot.setParam (p.id, juce::jlimit (p.min, p.max, number));
            return {};
        }

        for (const auto& key : facts.textKeys)
            if (squash (key) == want)
            {
                slot.setText (key, value.trim().toStdString());
                return {};
            }

        return "it has no setting called \"" + setting + "\"";
    }

    constexpr const char* actionWords[] = { "compare", "chain on off", "tap tempo", "whammy", "test take", "delay throw", "wah", "next amp" };
}

//==============================================================================
int chainFromWords (const juce::String& words, const juce::StringArray& chainNames)
{
    const auto w = words.trim();

    for (int c = 0; c < chainNames.size() && c < numChains; ++c)
        if (chainNames[c].isNotEmpty() && chainNames[c].trim().equalsIgnoreCase (w))
            return c;

    const auto digits = w.startsWithIgnoreCase ("chain") ? w.substring (5).trim() : w;

    if (digits.isNotEmpty() && digits.containsOnly ("0123456789"))
        if (const int n = digits.getIntValue(); n >= 1 && n <= numChains)
            return n - 1;

    return -1;
}

int slotFromWords (const fx::ChainModel& m, const juce::String& words)
{
    int nth = 1;
    auto name = words.trim();

    if (const int hash = name.lastIndexOfChar ('#'); hash > 0)
    {
        nth = juce::jmax (1, name.substring (hash + 1).trim().getIntValue());
        name = name.substring (0, hash);
    }

    const auto want = squash (name);

    for (int k = 0; k < (int) m.slots.size(); ++k)
    {
        const auto& type = m.slots[(size_t) k].type;
        const auto* info = fx::findEffectType (type);

        if (squash (juce::String (type)) == want || (info != nullptr && squash (juce::String (info->name)) == want))
            if (--nth == 0)
                return k;
    }

    return -1;
}

juce::String slotWords (const fx::ChainModel& m, int slot)
{
    if (slot < 0 || slot >= (int) m.slots.size())
        return {};

    int nth = 1;
    for (int k = 0; k < slot; ++k)
        if (m.slots[(size_t) k].type == m.slots[(size_t) slot].type)
            ++nth;

    return juce::String (m.slots[(size_t) slot].type) + (nth > 1 ? " #" + juce::String (nth) : juce::String());
}

juce::String switchActionWords (int action, const fx::ChainModel& chain)
{
    if (action >= 0 && action < (int) std::size (actionWords))
        return actionWords[action];

    if (action >= 10 && action - 10 < (int) chain.slots.size())
        return slotWords (chain, action - 10) + " on off";

    return {};
}

//==============================================================================
SongSounds SongSounds::fromText (const juce::String& text)
{
    SongSounds s;

    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.trim();

        if (line.isEmpty() || line.startsWithChar ('#'))
            continue;

        if (! line.containsChar ('='))
        {
            s.notUnderstood.add (line);
            continue;
        }

        const auto key = line.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto value = line.fromFirstOccurrenceOf ("=", false, false).trim();
        const auto low = key.toLowerCase();
        auto parts = juce::StringArray::fromTokens (value, "/", "");
        parts.trim();

        if (low == "program")
        {
            s.program = juce::jlimit (0, 128, value.getIntValue());
        }
        else if (low == "looper")
        {
            s.looper = value;
        }
        else if (low == "switches")
        {
            s.hasSwitches = true;       // "switches = none": this song takes every switch back for the looper
        }
        else if (low.startsWith ("switch"))
        {
            const int sw = switchFromWords (key.substring (6));

            if (sw < 0 || value.isEmpty())
            {
                s.notUnderstood.add (line);
                continue;
            }

            s.hasSwitches = true;
            s.switches.push_back (parts.size() >= 2 ? Switch { sw, parts[0], parts[1] } : Switch { sw, {}, value });
        }
        else if (low == "set")
        {
            if (parts.size() != 3)
            {
                s.notUnderstood.add (line);
                continue;
            }

            Setting set { parts[0], parts[1], {}, {} };

            if (parts[2].equalsIgnoreCase ("on") || parts[2].equalsIgnoreCase ("off"))
            {
                set.setting = "on";
                set.value = parts[2].equalsIgnoreCase ("on") ? "1" : "0";
            }
            else if (parts[2].containsChar ('='))
            {
                set.setting = parts[2].upToFirstOccurrenceOf ("=", false, false).trim();
                set.value = parts[2].fromFirstOccurrenceOf ("=", false, false).trim();
            }
            else
            {
                s.notUnderstood.add (line);
                continue;
            }

            s.settings.push_back (set);
        }
        else if (low == "amps")
        {
            if (parts.size() < 2)
            {
                s.notUnderstood.add (line);
                continue;
            }

            AmpList list { parts[0], parts };
            list.models.remove (0);
            s.amps.push_back (list);
        }
        else
        {
            // Anything else names a chain: one preset per line, stacked in the order written.
            const bool none = value.isEmpty() || value.equalsIgnoreCase ("(empty)") || value.equalsIgnoreCase ("nothing");
            ChainLine* found = nullptr;

            for (auto& c : s.chains)
                if (c.chain.equalsIgnoreCase (key))
                    found = &c;

            if (found == nullptr)
            {
                s.chains.push_back ({ key, {} });
                found = &s.chains.back();
            }

            if (! none)
                found->presets.add (value);
        }
    }

    return s;
}

juce::String SongSounds::toText (const juce::String& title, const juce::StringArray& chainNames) const
{
    juce::String t;
    t << "# GigRig song: " << title << "\n";
    t << "# What this song asks of the effects. Each 'chain' line names a preset from the Effect presets\n";
    t << "# folder (several lines for one chain stack them, like Add to chain). 'set' lines are this song's\n";
    t << "# own changes on top. A chain with no line here uses the Default sounds. Guide: docs/songs.md\n";

    for (int c = 0; c < chainNames.size(); ++c)
        if (chainNames[c].isNotEmpty())
            t << "# chain " << (c + 1) << " was called \"" << chainNames[c] << "\" when this was written\n";

    if (program > 0)            t << "program = " << program << "\n";
    if (looper.isNotEmpty())    t << "looper = " << looper << "\n";

    for (const auto& c : chains)
    {
        if (c.presets.isEmpty())
            t << c.chain << " = (empty)\n";

        for (const auto& p : c.presets)
            t << c.chain << " = " << p << "\n";
    }

    for (const auto& s : settings)
        t << "set = " << s.chain << " / " << s.effect << " / "
          << (s.setting == "on" ? juce::String (s.value == "0" ? "off" : "on") : s.setting + " = " + s.value) << "\n";

    if (hasSwitches && switches.empty())
        t << "switches = none\n";

    for (const auto& s : switches)
        t << "switch " << switchWords (s.sw) << " = " << (s.chain.isNotEmpty() ? s.chain + " / " : juce::String()) << s.action << "\n";

    for (const auto& a : amps)
        t << "amps = " << a.chain << " / " << a.models.joinIntoString (" / ") << "\n";

    return t;
}

//==============================================================================
ResolvedSounds resolveSounds (const SongSounds& song, const SongSounds* fallback, const juce::StringArray& chainNames,
                              const std::array<fx::ChainModel, EffectsRack::numChains>& now, const PresetLoader& loadPreset)
{
    ResolvedSounds r;
    const SongSounds* sources[] = { &song, fallback };

    // Each chain: the song's own presets, else the default's.
    for (int si = 0; si < 2; ++si)
    {
        if (sources[si] == nullptr)
            continue;

        for (const auto& line : sources[si]->chains)
        {
            const int c = chainFromWords (line.chain, chainNames);

            if (c < 0)
            {
                r.problems.add ("There is no chain called \"" + line.chain + "\" (line \"" + line.chain + " = ...\").");
                continue;
            }

            if (r.has[(size_t) c])
                continue;

            fx::ChainModel built;
            bool first = true, missing = false;

            for (const auto& name : line.presets)
            {
                fx::ChainModel m;

                if (! loadPreset (name, m))
                {
                    // Better the sound from the last song than an empty chain in the middle of a gig.
                    r.problems.add ("The preset \"" + name + "\" is not in the Effect presets folder, so " + chainWordsFor (c)
                                    + (si == 0 ? " was not set by this song." : " was not set by the default sounds."));
                    missing = true;
                    break;
                }

                if (first)
                    built = m;
                else if (const int added = built.addFrom (m, fx::Chain::maxSlots); added < (int) m.slots.size())
                    r.problems.add ("\"" + name + "\" did not all fit: " + chainWordsFor (c) + " holds " + juce::String (fx::Chain::maxSlots) + " effects.");

                first = false;
            }

            if (missing)
                continue;       // the default may still name it; otherwise the chain is left as it was

            r.has[(size_t) c] = true;
            r.named[(size_t) c] = true;
            r.from[(size_t) c] = si + 1;
            r.model[(size_t) c] = built;
            r.recipe[(size_t) c] = line.presets;
        }
    }

    // Settings: the default's only on chains the default built; the song's own on any chain
    // (a chain nobody named is changed as it stands).
    for (int si = 1; si >= 0; --si)
    {
        if (sources[si] == nullptr)
            continue;

        for (const auto& s : sources[si]->settings)
        {
            const int c = chainFromWords (s.chain, chainNames);
            const auto where = "\"set = " + s.chain + " / " + s.effect + " / ...\": ";

            if (c < 0)
            {
                r.problems.add (where + "there is no chain called \"" + s.chain + "\".");
                continue;
            }

            if (si == 1 && r.from[(size_t) c] != 2)
                continue;

            if (! r.has[(size_t) c])
            {
                r.has[(size_t) c] = true;
                r.model[(size_t) c] = now[(size_t) c];
            }

            auto& m = r.model[(size_t) c];
            const int k = slotFromWords (m, s.effect);

            if (k < 0)
            {
                r.problems.add (where + chainWordsFor (c) + " has no effect called \"" + s.effect + "\".");
                continue;
            }

            if (const auto error = applySetting (m.slots[(size_t) k], s.setting, s.value); error.isNotEmpty())
                r.problems.add (where + error + ".");
        }
    }

    const auto modelOf = [&] (int c) -> const fx::ChainModel& { return r.has[(size_t) c] ? r.model[(size_t) c] : now[(size_t) c]; };

    // Switches: all of the song's, or all of the default's.
    if (const auto* src = song.hasSwitches ? &song : fallback != nullptr && fallback->hasSwitches ? fallback : nullptr)
    {
        r.hasSwitches = true;

        for (const auto& s : src->switches)
        {
            const auto words = s.action.trim().toLowerCase();
            const auto where = "\"switch " + switchWords (s.sw) + " = ...\": ";
            EffectsRack::PedalAssign a { s.sw, 0, -1 };

            for (int i = 0; i < (int) std::size (actionWords); ++i)
                if (words == actionWords[i])
                    a.action = i;

            const bool anyChain = a.action == 2 || a.action == 4;

            if (! anyChain)
            {
                a.chain = chainFromWords (s.chain, chainNames);

                if (a.chain < 0)
                {
                    r.problems.add (where + (s.chain.isEmpty() ? juce::String ("it needs a chain, as in \"chain 1 / next amp\".")
                                                              : "there is no chain called \"" + s.chain + "\"."));
                    continue;
                }
            }

            if (a.action < 0 && words.endsWith (" on off"))
                if (const int k = slotFromWords (modelOf (a.chain), s.action.trim().dropLastCharacters (7)); k >= 0)
                    a.action = 10 + k;

            if (a.action < 0)
            {
                r.problems.add (where + "\"" + s.action + "\" is not something a switch can do here.");
                continue;
            }

            if ((int) r.switches.size() >= EffectsRack::numPedalAssigns)
            {
                r.problems.add (where + "only " + juce::String (EffectsRack::numPedalAssigns) + " switches can go to the effects; this one was left out.");
                continue;
            }

            r.switches.push_back (a);
        }
    }

    // The amp-cycling lists, chain by chain.
    for (int si = 1; si >= 0; --si)
        if (sources[si] != nullptr)
            for (const auto& a : sources[si]->amps)
                if (const int c = chainFromWords (a.chain, chainNames); c >= 0)
                    r.amps[(size_t) c] = a.models;
                else
                    r.problems.add ("\"amps = " + a.chain + " / ...\": there is no chain called \"" + a.chain + "\".");

    return r;
}

ChainAsPresets describeChain (const fx::ChainModel& now, const juce::StringArray& recipe, const juce::String& chainWords, const PresetLoader& loadPreset)
{
    ChainAsPresets d;

    if (recipe.isEmpty())
        return d;

    fx::ChainModel base;

    for (int i = 0; i < recipe.size(); ++i)
    {
        fx::ChainModel m;

        if (! loadPreset (recipe[i], m))
            return d;

        if (i == 0)
            base = m;
        else if (base.addFrom (m, fx::Chain::maxSlots) < (int) m.slots.size())
            return d;
    }

    if (base.slots.size() != now.slots.size())
        return d;

    for (size_t k = 0; k < now.slots.size(); ++k)
        if (base.slots[k].type != now.slots[k].type)
            return d;

    d.matches = true;

    for (size_t k = 0; k < now.slots.size(); ++k)
    {
        const auto& a = base.slots[k];
        const auto& b = now.slots[k];
        const auto effect = slotWords (now, (int) k);
        const auto& facts = factsFor (b.type);

        if (a.on != b.on)
            d.changes.push_back ({ chainWords, effect, "on", b.on ? "1" : "0" });

        for (const auto& p : facts.params)
        {
            const float was = a.param (p.id, p.def), is = b.param (p.id, p.def);

            if (std::abs (was - is) > 1.0e-4f * juce::jmax (1.0f, p.max - p.min))
                d.changes.push_back ({ chainWords, effect, p.id, number (is) });
        }

        for (const auto& key : facts.textKeys)
            if (const auto is = b.text (key); ! is.empty() && is != a.text (key))
                d.changes.push_back ({ chainWords, effect, key, is });
    }

    return d;
}

PresetLoader presetLoaderFor (const juce::File& folder)
{
    return [folder] (const juce::String& name, fx::ChainModel& m)
    {
        const auto file = folder.getChildFile (juce::File::createLegalFileName (name) + ".txt");

        if (! file.existsAsFile())
            return false;

        m = fx::ChainModel::fromText (file.loadFileAsString().toStdString());
        return true;
    };
}

std::vector<int> slotOwners (const juce::StringArray& recipe, const PresetLoader& loadPreset)
{
    // The same stacking as ChainModel::addFrom, carried out on name tags.
    fx::ChainModel built;
    std::vector<int> owners;

    for (int i = 0; i < recipe.size(); ++i)
    {
        fx::ChainModel m;

        if (! loadPreset (recipe[i], m))
            return {};

        if (i == 0)
        {
            built = m;
            owners.assign (m.slots.size(), 0);
            continue;
        }

        auto at = built.slots.size();
        for (size_t k = 0; k < built.slots.size(); ++k)
            if (built.slots[k].type == "amp")
            {
                at = k;
                break;
            }

        if (built.addFrom (m, fx::Chain::maxSlots) < (int) m.slots.size())
            return {};

        owners.insert (owners.begin() + (std::ptrdiff_t) at, m.slots.size(), i);
    }

    return owners;
}

juce::String saveBackToPresets (EffectsRack& rack, int c, const juce::File& presetFolder)
{
    const auto recipe = rack.recipe (c);
    const auto& model = rack.chain (c).model();
    const auto& base = rack.baseline (c);
    const auto load = presetLoaderFor (presetFolder);
    const juce::String cannot = "Nothing was saved: ";

    if (recipe.isEmpty())
        return cannot + "this chain was not built from presets (use Load and Add to chain first, or Save as for a new preset).";

    for (int i = 0; i < recipe.size(); ++i)
        for (int j = i + 1; j < recipe.size(); ++j)
            if (recipe[i].equalsIgnoreCase (recipe[j]))
                return cannot + "the preset \"" + recipe[i] + "\" is in this chain twice, so the app cannot tell which copy to save.";

    const auto owners = slotOwners (recipe, load);
    bool sameShape = owners.size() == model.slots.size() && base.slots.size() == model.slots.size() && describeChain (model, recipe, {}, load).matches;

    for (size_t k = 0; sameShape && k < model.slots.size(); ++k)
        sameShape = base.slots[k].type == model.slots[k].type;

    if (! sameShape)
        return cannot + "effects were added, removed or moved since the presets were loaded, so the chain no longer splits into them. Use Save as for a new preset.";

    // Only what was changed since the chain was loaded goes into the presets. What a song set
    // on top of a preset (its own 'set' lines) was already there when it loaded, so it stays out.
    juce::StringArray changed;

    for (int i = 0; i < recipe.size(); ++i)
    {
        fx::ChainModel preset;

        if (! load (recipe[i], preset))
            return cannot + "the preset \"" + recipe[i] + "\" is no longer in the folder.";

        size_t at = 0;
        bool touched = false;

        for (size_t k = 0; k < owners.size(); ++k)
        {
            if (owners[k] != i)
                continue;

            if (at >= preset.slots.size())
                break;

            auto& into = preset.slots[at++];
            const auto& was = base.slots[k];
            const auto& is = model.slots[k];
            const auto& facts = factsFor (is.type);

            if (was.on != is.on)
            {
                into.on = is.on;
                touched = true;
            }

            for (const auto& p : facts.params)
                if (const float v = is.param (p.id, p.def); std::abs (v - was.param (p.id, p.def)) > 1.0e-4f * juce::jmax (1.0f, p.max - p.min))
                {
                    into.setParam (p.id, v);
                    touched = true;
                }

            for (const auto& key : facts.textKeys)
                if (const auto v = is.text (key); ! v.empty() && v != was.text (key))
                {
                    into.setText (key, v);
                    touched = true;
                }
        }

        if (! touched)
            continue;

        const auto file = presetFolder.getChildFile (juce::File::createLegalFileName (recipe[i]) + ".txt");

        if (! file.replaceWithText ("# GigRig effect preset \"" + recipe[i] + "\" (build " + juce::String (GIGRIG_BUILD_LABEL) + ")\n" + juce::String (preset.toText())))
            return "Could not write " + file.getFullPathName();

        changed.add ("\"" + recipe[i] + "\"");
    }

    rack.setBaseline (c, model);

    return changed.isEmpty() ? juce::String ("Nothing has been changed since this chain was loaded, so there is nothing to save.")
                             : "Saved what you changed into " + changed.joinIntoString (" and ") + ". Every song that names " + (changed.size() == 1 ? "it" : "them")
                               + " gets the change when it is next loaded.";
}

//==============================================================================
SongBook::SongBook (AudioEngine& e, const juce::File& songs, const juce::File& presets)
    : engine (e), songFolder (songs), presetFolder (presets)
{
    rescan();
    loadedSong = builtInSong (0);
}

juce::File SongBook::defaultSongFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GigRig").getChildFile ("Songs");
}

PresetLoader SongBook::loader() const     { return presetLoaderFor (presetFolder); }

juce::StringArray SongBook::chainNames() const
{
    juce::StringArray names;

    for (int c = 0; c < numChains; ++c)
        names.add (engine.effects().routing (c).name);

    return names;
}

juce::String SongBook::stamp() const
{
    juce::String s;
    auto files = songFolder.findChildFiles (juce::File::findFiles, false, "*.txt");
    files.sort();

    for (const auto& f : files)
        s << f.getFileName() << "|" << f.getLastModificationTime().toMilliseconds() << "|" << f.getSize() << "\n";

    return s;
}

int SongBook::entryNamed (const juce::String& name) const
{
    for (int i = 0; i < numEntries(); ++i)
        if (entries[(size_t) i].name.equalsIgnoreCase (name.trim()))
            return i;

    return -1;
}

int SongBook::entryForProgram (int program) const
{
    if (program <= 0)
        return -1;

    // A file's own entry wins over a built-in one with the same number.
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < numEntries(); ++i)
            if (entries[(size_t) i].program == program && entries[(size_t) i].builtIn == (pass == 1))
                return i;

    return -1;
}

void SongBook::rescan()
{
    const auto wasLoaded = current < numEntries() ? entries[(size_t) current].name : juce::String();
    entries.clear();
    defaultSounds = {};

    for (int i = 0; i < numBuiltInSongs(); ++i)
    {
        const auto& s = builtInSong (i);
        Entry e;
        e.name = s.title();
        e.program = s.rcMemory;
        e.layout = i;
        e.builtIn = true;
        entries.push_back (e);
    }

    const auto layoutNamed = [] (const juce::String& name)
    {
        for (int i = 0; i < numBuiltInSongs(); ++i)
            if (builtInSong (i).title().equalsIgnoreCase (name.trim()))
                return i;

        return -1;
    };

    auto files = songFolder.findChildFiles (juce::File::findFiles, false, "*.txt");
    files.sort();

    for (const auto& f : files)
    {
        const auto name = f.getFileNameWithoutExtension();
        auto sounds = SongSounds::fromText (f.loadFileAsString());

        if (name.equalsIgnoreCase (defaultFileName))
        {
            defaultSounds = sounds;
            continue;
        }

        if (const int at = entryNamed (name); at >= 0)
        {
            auto& e = entries[(size_t) at];
            e.hasFile = true;
            if (sounds.program > 0)
                e.program = sounds.program;
            e.sounds = sounds;
            continue;
        }

        Entry e;
        e.name = name;
        e.program = sounds.program;
        e.hasFile = true;
        e.layout = layoutNamed (sounds.looper.isNotEmpty() ? sounds.looper : juce::String (generalLayout));

        if (e.layout < 0)
        {
            sounds.notUnderstood.add ("looper = " + sounds.looper + "  (no looper layout has that name, so the general one is used)");
            e.layout = juce::jmax (0, layoutNamed (generalLayout));
        }

        e.sounds = sounds;
        entries.push_back (e);
    }

    folderStamp = stamp();
    current = juce::jmax (0, entryNamed (wasLoaded));
}

bool SongBook::rescanIfChanged()
{
    if (stamp() == folderStamp)
        return false;

    const auto before = currentEntry().sounds.toText ({}) + defaultSounds.toText ({});
    rescan();

    if (currentEntry().sounds.toText ({}) + defaultSounds.toText ({}) != before)
        applySounds ("its file changed");
    else
        changed();

    return true;
}

void SongBook::changed()
{
    if (onChanged != nullptr)
        onChanged();
}

void SongBook::load (int index, const juce::String& why, bool withSounds)
{
    if (index < 0 || index >= numEntries())
        return;

    current = index;
    const auto& e = entries[(size_t) index];
    loadedSong = builtInSong (e.layout);

    if (! e.builtIn)
    {
        std::memset (loadedSong.name, 0, sizeof (loadedSong.name));
        e.name.copyToUTF8 (loadedSong.name, sizeof (loadedSong.name) - 1);
        loadedSong.rcMemory = 0;
    }

    engine.looper().setSong (loadedSong);

    if (withSounds)
        applySounds (why);
    else
        changed();
}

void SongBook::applySounds (const juce::String& why)
{
    auto& rack = engine.effects();
    const auto& e = currentEntry();
    std::array<fx::ChainModel, EffectsRack::numChains> now;

    for (int c = 0; c < numChains; ++c)
        now[(size_t) c] = rack.chain (c).model();

    const auto r = resolveSounds (e.sounds, hasDefault() ? &defaultSounds : nullptr, chainNames(), now, loader());
    int moved = 0, rebuilt = 0;

    for (int c = 0; c < numChains; ++c)
    {
        chainFrom[(size_t) c] = r.from[(size_t) c];
        rack.setAmpCycle (c, r.amps[(size_t) c]);

        if (! r.has[(size_t) c])
            continue;

        (rack.chain (c).setModelGently (r.model[(size_t) c]) ? moved : rebuilt)++;
        rack.setBaseline (c, r.model[(size_t) c]);

        if (r.named[(size_t) c])
            rack.setRecipe (c, r.recipe[(size_t) c]);
    }

    if (r.hasSwitches)
        for (int i = 0; i < EffectsRack::numPedalAssigns; ++i)
            rack.setPedalAssign (i, i < (int) r.switches.size() ? r.switches[(size_t) i] : EffectsRack::PedalAssign {});

    rack.markChanged();
    problems = r.problems;

    for (const auto& line : e.sounds.notUnderstood)
        problems.add ("Line not understood in the song's file: " + line);

    for (const auto& line : defaultSounds.notUnderstood)
        problems.add ("Line not understood in the Default sounds file: " + line);

    note = "\"" + e.name + "\" loaded (" + why + "). ";

    if (moved + rebuilt == 0)
        note << "It names no sounds and there are no default sounds, so the effects stay as they were.";
    else
        note << juce::String (moved + rebuilt) << (moved + rebuilt == 1 ? " chain set" : " chains set")
             << (rebuilt > 0 ? " (" + juce::String (rebuilt) + " with different effects, so their delays and reverbs ring out)" : juce::String (" (same effects as before, only settings moved)"))
             << ".";

    changed();
}

void SongBook::programChange (int program, const juce::String& from)
{
    if (program < 1 || program > 128)
        return;

    int index = entryForProgram (program);
    juce::String line = juce::Time::getCurrentTime().toString (false, true, true, true) + "  program " + juce::String (program)
                      + (from.isNotEmpty() ? " from " + from : juce::String()) + ": ";

    if (index < 0)
    {
        index = juce::jmax (0, entryNamed (generalLayout));
        line << "no entry has that number, so the general set-up (" << entries[(size_t) index].name << ") was used";
    }
    else
    {
        line << entries[(size_t) index].name;
    }

    if (index == current)
    {
        line << " (already loaded: the loops were left alone and the sounds set afresh)";
        applySounds ("program " + juce::String (program) + " again");
    }
    else
    {
        load (index, "program " + juce::String (program));
    }

    programLog.add (line);

    while (programLog.size() > 8)
        programLog.remove (0);

    changed();
}

//==============================================================================
SongSounds SongBook::capture (const juce::String& songName, juce::StringArray& notes)
{
    auto& rack = engine.effects();
    SongSounds s;
    const auto load = loader();

    for (int c = 0; c < numChains; ++c)
    {
        const auto route = rack.routing (c);

        if (! route.on)
            continue;

        const auto& model = rack.chain (c).model();
        const auto words = chainWordsFor (c);
        auto recipe = rack.recipe (c);

        if (model.slots.empty())
        {
            s.chains.push_back ({ words, {} });
            continue;
        }

        auto d = describeChain (model, recipe, words, load);

        if (! d.matches)
        {
            // The chain was built or rearranged by hand: there are no presets to name, so it
            // becomes a preset of this song's own.
            const auto name = juce::File::createLegalFileName (songName + " - " + (route.name.trim().isNotEmpty() ? route.name.trim() : words));
            presetFolder.createDirectory();

            if (presetFolder.getChildFile (name + ".txt").replaceWithText ("# GigRig effect preset \"" + name + "\" (build " + juce::String (GIGRIG_BUILD_LABEL)
                                                                           + "), saved for the song \"" + songName + "\"\n" + juce::String (model.toText())))
            {
                notes.add (words + " (" + route.name + ") no longer matched its presets effect for effect, so it was saved as a new preset called \"" + name + "\".");
                recipe = juce::StringArray (name);
                rack.setRecipe (c, recipe);
                d.changes.clear();
            }
            else
            {
                notes.add ("Could not write the preset \"" + name + "\", so " + words + " was left out.");
                continue;
            }
        }

        s.chains.push_back ({ words, recipe });
        s.settings.insert (s.settings.end(), d.changes.begin(), d.changes.end());

        if (const auto amps = rack.ampCycle (c); ! amps.isEmpty())
            s.amps.push_back ({ words, amps });
    }

    s.hasSwitches = true;

    for (int i = 0; i < EffectsRack::numPedalAssigns; ++i)
        if (const auto a = rack.pedalAssign (i); a.sw >= 0)
            if (const auto words = switchActionWords (a.action, rack.chain (a.chain).model()); words.isNotEmpty())
                s.switches.push_back ({ a.sw, a.action == 2 || a.action == 4 ? juce::String() : chainWordsFor (a.chain), words });

    return s;
}

bool SongBook::writeSong (const juce::String& name, const SongSounds& s)
{
    songFolder.createDirectory();
    return songFolder.getChildFile (juce::File::createLegalFileName (name) + ".txt").replaceWithText (s.toText (name, chainNames()));
}

juce::String SongBook::keepForCurrent()
{
    const auto e = currentEntry();
    juce::StringArray notes;
    auto s = capture (e.name, notes);

    // What is the same as the default is left out, so this song keeps following the default there.
    if (hasDefault())
    {
        // One chain's lines from a file, with the chain always written "chain N", so the song's
        // (always written that way) can be compared with a default written by hand with names.
        const auto names = chainNames();
        const auto linesFor = [&names] (const SongSounds& src, int c)
        {
            SongSounds one;
            const auto mine = [&] (const juce::String& chain) { return chainFromWords (chain, names) == c; };
            const auto words = chainWordsFor (c);
            for (const auto& x : src.chains)   if (mine (x.chain)) one.chains.push_back ({ words, x.presets });
            for (const auto& x : src.settings) if (mine (x.chain)) one.settings.push_back ({ words, x.effect, x.setting, x.value });
            for (const auto& x : src.amps)     if (mine (x.chain)) one.amps.push_back ({ words, x.models });
            return one.chains.empty() ? juce::String() : one.toText ({});
        };
        const auto sameChain = [&] (int c)
        {
            const auto mine = linesFor (s, c);
            return mine.isNotEmpty() && mine == linesFor (defaultSounds, c);
        };

        for (int c = 0; c < numChains; ++c)
        {
            const auto words = chainWordsFor (c);

            if (! sameChain (c))
                continue;

            std::erase_if (s.chains,   [&] (const auto& x) { return x.chain == words; });
            std::erase_if (s.settings, [&] (const auto& x) { return x.chain == words; });
            std::erase_if (s.amps,     [&] (const auto& x) { return x.chain == words; });
            notes.add (words + " is the same as the default, so the song keeps following the default there.");
        }

        SongSounds mine, theirs;
        mine.hasSwitches = theirs.hasSwitches = true;
        mine.switches = s.switches;
        theirs.switches = defaultSounds.switches;

        if (defaultSounds.hasSwitches && mine.toText ({}) == theirs.toText ({}))
        {
            s.hasSwitches = false;
            s.switches.clear();
            notes.add ("The switches are the same as the default's, so the song keeps following the default's switches.");
        }
    }
    else if (s.switches.empty())
    {
        s.hasSwitches = false;
    }

    if (! e.builtIn || e.program != builtInSong (e.layout).rcMemory)
        s.program = e.program;

    if (! e.builtIn)
        s.looper = builtInSong (e.layout).title();

    if (! writeSong (e.name, s))
        return note = "Could not write the song's file in " + songFolder.getFullPathName();

    rescan();
    applySounds ("its sounds were kept");
    note = "Kept for \"" + e.name + "\": " + (s.saysAnything() ? juce::String ("the sounds as they are now.") : juce::String ("nothing of its own (everything matches the default)."))
         + (notes.isEmpty() ? juce::String() : " " + notes.joinIntoString (" "));
    changed();
    return note;
}

juce::String SongBook::keepAsDefault()
{
    juce::StringArray notes;
    auto s = capture ("Default", notes);

    if (s.switches.empty())
        s.hasSwitches = false;

    if (! writeSong (defaultFileName, s))
        return note = "Could not write the default sounds in " + songFolder.getFullPathName();

    rescan();
    applySounds ("the default sounds were kept");
    note = "Kept as the default sounds: every song without lines of its own for a chain now uses these."
         + (notes.isEmpty() ? juce::String() : " " + notes.joinIntoString (" "));
    changed();
    return note;
}

juce::String SongBook::forgetCurrent()
{
    const auto e = currentEntry();

    if (! e.hasFile)
        return note = "\"" + e.name + "\" has no sounds of its own to forget.";

    SongSounds s;
    s.program = e.sounds.program;
    s.looper = e.sounds.looper;
    const auto file = songFolder.getChildFile (juce::File::createLegalFileName (e.name) + ".txt");
    const bool ok = e.builtIn && s.program == 0 ? file.deleteFile() : writeSong (e.name, s);

    if (! ok)
        return note = "Could not change " + file.getFullPathName();

    rescan();
    applySounds ("its own sounds were forgotten");
    note = "\"" + e.name + "\" no longer has sounds of its own" + (hasDefault() ? juce::String (", so it uses the default sounds.") : juce::String (". The effects stay as they are."));
    changed();
    return note;
}

juce::String SongBook::addEntry (const juce::String& wanted, int program)
{
    const auto name = juce::File::createLegalFileName (wanted.trim());

    if (name.isEmpty())
        return "Type a name for the new entry first.";

    if (name.equalsIgnoreCase (defaultFileName) || entryNamed (name) >= 0)
        return "There is already an entry called \"" + name + "\".";

    if (program < 0 || program > 128)
        return "The program number must be from 1 to 128 (or 0 for none).";

    if (const int other = entryForProgram (program); other >= 0)
        return "Program " + juce::String (program) + " already loads \"" + entries[(size_t) other].name + "\".";

    juce::StringArray notes;
    auto s = capture (name, notes);

    if (s.switches.empty())
        s.hasSwitches = false;

    s.program = program;
    s.looper = builtInSong (currentEntry().layout).title();

    if (! writeSong (name, s))
        return "Could not write the new entry in " + songFolder.getFullPathName();

    rescan();
    load (entryNamed (name), "new entry");
    note = "New entry \"" + name + "\"" + (program > 0 ? " on program " + juce::String (program) : juce::String())
         + ", with the looper layout of \"" + s.looper + "\" and the sounds as they are now." + (notes.isEmpty() ? juce::String() : " " + notes.joinIntoString (" "));
    changed();
    return {};
}

//==============================================================================
juce::String SongBook::statusLine() const
{
    const auto& e = currentEntry();
    juce::StringArray own, def, alone;

    for (int c = 0; c < numChains; ++c)
        (chainFrom[(size_t) c] == 1 ? own : chainFrom[(size_t) c] == 2 ? def : alone).add (juce::String (c + 1));

    juce::String t = e.name + (e.program > 0 ? " (program " + juce::String (e.program) + ")" : juce::String (" (no program number)")) + ": ";
    juce::StringArray parts;

    if (! own.isEmpty())    parts.add ("its own sounds on chain " + own.joinIntoString (", "));
    if (! def.isEmpty())    parts.add ("the default sounds on chain " + def.joinIntoString (", "));
    if (! alone.isEmpty())  parts.add ("chain " + alone.joinIntoString (", ") + " left as set by hand");

    return t + parts.joinIntoString ("; ") + ".";
}

juce::String SongBook::reportText() const
{
    const auto& rack = engine.effects();
    const auto& e = currentEntry();
    juce::String t;
    t << "--- Songs ---\n";
    t << "Song loaded: " << statusLine() << "\n";
    t << "Looper layout: " << builtInSong (e.layout).title() << (e.builtIn ? "" : " (borrowed: this entry has no looper layout of its own)") << "\n";
    t << "Songs folder: " << songFolder.getFullPathName() << " (" << (int) std::count_if (entries.begin(), entries.end(), [] (const Entry& x) { return x.hasFile; })
      << " song files, default sounds " << (hasDefault() ? "set" : "not set") << ")\n";

    for (int c = 0; c < numChains; ++c)
        if (const auto recipe = rack.recipe (c); ! recipe.isEmpty())
            t << "Chain " << (c + 1) << " presets: " << recipe.joinIntoString ("  +  ")
              << (rack.ampCycle (c).isEmpty() ? juce::String() : "   (amp switch steps through: " + rack.ampCycle (c).joinIntoString (", ") + ")") << "\n";

    if (e.hasFile && e.sounds.saysAnything())
        for (const auto& line : juce::StringArray::fromLines (e.sounds.toText (e.name)))
            if (line.isNotEmpty() && ! line.startsWithChar ('#'))
                t << "  file: " << line << "\n";

    if (note.isNotEmpty())
        t << "Last: " << note << "\n";

    for (const auto& p : problems)
        t << "PROBLEM: " << p << "\n";

    if (rack.ampNote.isNotEmpty())
        t << "Amp switch: " << rack.ampNote << "\n";

    t << "Program changes heard: " << (programLog.isEmpty() ? "none yet" : "") << "\n";

    for (const auto& line : programLog)
        t << "  " << line << "\n";

    juce::StringArray numbers;

    for (const auto& x : entries)
        if (x.program > 0)
            numbers.add (juce::String (x.program) + " " + x.name);

    t << "Program numbers: " << numbers.joinIntoString (", ") << "\n";
    return t;
}

} // namespace gigrig
