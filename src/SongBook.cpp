#include "SongBook.h"
#include "LooperLayout.h"
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

    constexpr const char* actionWords[] = { "compare", "chain on off", "tap tempo", "whammy", "test take", "delay throw", "wah", "next amp", "harmony", "toe switch" };
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

    if (action == EffectsRack::actionPedalToe)
        return "pedal toe";

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
        else if (low.startsWith ("looper "))
        {
            s.layoutLines.add (line);       // the song's own looper layout; read by layout::fromLines when the song loads
        }
        else if (low == "tempo")
        {
            s.tempo = value.getDoubleValue();

            if (s.tempo < 30.0 || s.tempo > 300.0)
            {
                s.tempo = 0.0;
                s.notUnderstood.add (line + "  (a tempo is 30 to 300 beats a minute)");
            }
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
        else if (low == "send")
        {
            // "send = Vocal / Vocal plate = -6" or "... = off"
            const auto level = parts.size() == 2 ? parts[1].fromLastOccurrenceOf ("=", false, false).trim() : juce::String();

            if (parts.size() != 2 || ! parts[1].containsChar ('=') || parts[0].isEmpty()
                || ! (level.equalsIgnoreCase ("off") || level.containsAnyOf ("0123456789")))
            {
                s.notUnderstood.add (line + "  (a send line reads: send = channel / reverb or delay = level in dB, or off)");
                continue;
            }

            s.sends.push_back ({ parts[0], parts[1].upToLastOccurrenceOf ("=", false, false).trim(),
                                 level.equalsIgnoreCase ("off") ? mixOffDb : juce::jlimit (mixOffDb, 10.0f, level.getFloatValue()) });
        }
        else if (low == "mixer effect")
        {
            // "mixer effect = Vocal delay / tapedelay / time = 121 / feedback = 20"
            if (parts.size() < 2 || parts[0].isEmpty() || parts[1].isEmpty() || parts[1].containsChar ('='))
            {
                s.notUnderstood.add (line + "  (a mixer effect line reads: mixer effect = which one / the reverb or delay's id / setting = value / ...)");
                continue;
            }

            MixerEffect e { parts[0], parts[1].toLowerCase(), {} };
            bool good = true;

            for (int p = 2; p < parts.size(); ++p)
            {
                const auto key = parts[p].upToFirstOccurrenceOf ("=", false, false).trim();
                const auto value = parts[p].fromFirstOccurrenceOf ("=", false, false).trim();
                good = good && key.isNotEmpty() && value.containsAnyOf ("0123456789");
                e.values.push_back ({ key, value.getFloatValue() });
            }

            if (! good)
            {
                s.notUnderstood.add (line + "  (each setting reads: name = number)");
                continue;
            }

            s.mixerEffects.push_back (e);
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
    if (tempo > 0.0)            t << "tempo = " << juce::String (tempo, 1) << "\n";

    if (! layoutLines.isEmpty())
    {
        t << "# This song's own looper layout, written by the builder on the remote screen (docs/remote.md).\n";
        for (const auto& l : layoutLines)
            t << l << "\n";
    }

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

    for (const auto& x : sends)
        t << "send = " << x.channel << " / " << x.effect << " = " << (x.db <= mixOffDb ? juce::String ("off") : juce::String (x.db, 1)) << "\n";

    for (const auto& x : mixerEffects)
    {
        t << "mixer effect = " << x.effect << " / " << x.type;
        for (const auto& v : x.values)
            t << " / " << v.first << " = " << juce::String (v.second, 2);
        t << "\n";
    }

    return t;
}

//==============================================================================
std::vector<Mixer::SongSend> resolveSends (const SongSounds& song, const SongSounds* fallback, const MixerSetup& mixer, juce::StringArray& problems)
{
    const auto numbered = [] (const juce::String& words, const juce::StringArray& starts, int count)
    {
        auto w = words.trim().toLowerCase();

        for (const auto& start : starts)
            if (w.startsWith (start))
                w = w.substring (start.length()).trim();

        return w.containsOnly ("0123456789") && w.isNotEmpty() && w.getIntValue() >= 1 && w.getIntValue() <= count ? w.getIntValue() - 1 : -1;
    };
    const auto channelOf = [&] (const juce::String& words)
    {
        for (int c = 0; c < numMixChannels; ++c)
            if (mixer.channel[(size_t) c].name.trim().equalsIgnoreCase (words.trim()))
                return c;

        return numbered (words, { "channel", "ch" }, numMixChannels);
    };
    const auto effectOf = [&] (const juce::String& words)
    {
        for (int f = 0; f < numMixFx; ++f)
            if (mixer.fx[(size_t) f].name.trim().equalsIgnoreCase (words.trim()))
                return f;

        return numbered (words, { "effect", "fx" }, numMixFx);
    };

    std::vector<Mixer::SongSend> sends;
    const auto add = [&] (const SongSounds& from, const char* whose)
    {
        for (const auto& x : from.sends)
        {
            const int c = channelOf (x.channel), f = effectOf (x.effect);

            if (c < 0 || f < 0)
            {
                problems.add (juce::String (whose) + " \"send = " + x.channel + " / " + x.effect + "\": the mixer has no "
                              + (c < 0 ? "channel called \"" + x.channel + "\"" : "reverb or delay called \"" + x.effect + "\"") + " in the act that is loaded, so the line was left out.");
                continue;
            }

            if (std::none_of (sends.begin(), sends.end(), [c, f] (const Mixer::SongSend& s) { return s.channel == c && s.fx == f; }))
                sends.push_back ({ c, f, x.db });
        }
    };

    add (song, "The song's line");

    if (fallback != nullptr)
        add (*fallback, "The Default sounds line");

    return sends;
}

int mixerEffectFromWords (const juce::String& words, const MixerSetup& mixer)
{
    for (int f = 0; f < numMixFx; ++f)
        if (mixer.fx[(size_t) f].name.trim().equalsIgnoreCase (words.trim()))
            return f;

    auto w = words.trim().toLowerCase();
    for (const auto* start : { "effect", "fx" })
        if (w.startsWith (start))
            w = w.substring ((int) std::strlen (start)).trim();

    return w.containsOnly ("0123456789") && w.isNotEmpty() && w.getIntValue() >= 1 && w.getIntValue() <= numMixFx ? w.getIntValue() - 1 : -1;
}

namespace
{
    // The name a file can use for one of the mixer's things: its own, or "<kind> N" when the name is empty, has / or = in it, or is used twice.
    template <typename List>
    juce::String mixerWords (const List& list, int index, const char* kind)
    {
        const auto& name = list[(size_t) index].name;
        const bool unique = name.trim().isNotEmpty() && ! name.containsAnyOf ("/=")
                            && std::count_if (list.begin(), list.end(), [&name] (const auto& o) { return o.name.trim().equalsIgnoreCase (name.trim()); }) == 1;
        return unique ? name.trim() : juce::String (kind) + " " + juce::String (index + 1);
    }

    bool isMixerReverb (const std::string& type)   { return type == "plate" || type == "room" || type == "spring"; }
    bool isMixerDelay (const std::string& type)    { return type == "digitaldelay" || type == "tapedelay" || type == "analogdelay"; }
}

SongSounds::MixerEffect mixerEffectLine (int f, const fx::ChainModel& effect, const MixerSetup& mixer)
{
    SongSounds::MixerEffect e { mixerWords (mixer.fx, f, "fx"), {}, {} };

    if (effect.slots.empty())
        return e;

    e.type = juce::String (effect.slots[0].type);

    if (const auto made = fx::makeEffect (effect.slots[0].type))
        for (const auto& info : made->params())
            e.values.push_back ({ juce::String (info.id), effect.slots[0].param (info.id, info.def) });

    return e;
}

void putMixerEffect (SongSounds& s, const SongSounds::MixerEffect& e, const MixerSetup& mixer)
{
    const int f = mixerEffectFromWords (e.effect, mixer);
    std::erase_if (s.mixerEffects, [&] (const SongSounds::MixerEffect& x) { return mixerEffectFromWords (x.effect, mixer) == f; });
    s.mixerEffects.push_back (e);
}

MovedToMixer moveToMixer (const SongSounds& song, const juce::StringArray& chainNames,
                          const std::array<int, EffectsRack::numChains>& chainChannel, const MixerSetup& mixer, const PresetLoader& loadPreset)
{
    MovedToMixer out;
    out.song = song;

    if (! song.notUnderstood.isEmpty())
    {
        out.notes.add ("left alone: it has lines the app could not read, and rewriting the file would lose them");
        return out;
    }

    const std::array<fx::ChainModel, EffectsRack::numChains> nothing;
    const auto r = resolveSounds (song, nullptr, chainNames, nothing, loadPreset);

    // The mixer reverb or delay a channel's effect goes to: a vocal's to the one with "vocal" in
    // its name, anything else's to the one without.
    const bool namesTell = std::any_of (mixer.fx.begin(), mixer.fx.end(), [] (const MixFxSetup& x) { return x.name.containsIgnoreCase ("vocal"); });
    const auto target = [&mixer, namesTell] (bool vocal, bool delay)
    {
        for (int f = 0; namesTell && f < numMixFx; ++f)
        {
            const auto& x = mixer.fx[(size_t) f];

            if (! x.effect.slots.empty() && x.name.containsIgnoreCase ("vocal") == vocal
                && (delay ? isMixerDelay (x.effect.slots[0].type) : isMixerReverb (x.effect.slots[0].type)))
                return f;
        }

        // Renamed, or set to something else in this venue: the places the mixer starts with
        // (1 the vocal's reverb, 2 the vocal's delay, 3 the instruments' room). The song's line
        // says what the effect is, so what the venue has there does not matter.
        return vocal ? (delay ? 1 : 0) : 2;
    };
    for (int c = 0; c < EffectsRack::numChains; ++c)
    {
        const int ch = chainChannel[(size_t) c];

        if (ch < 0 || ! r.has[(size_t) c] || ! r.named[(size_t) c])
            continue;

        const auto& model = r.model[(size_t) c];

        if (std::any_of (model.slots.begin(), model.slots.end(), [] (const fx::SlotModel& s) { return s.type == "amp" || s.type == "bass9"; }))
            continue;

        const auto words = "chain " + juce::String (c + 1);
        const bool vocal = mixer.channel[(size_t) ch].name.containsIgnoreCase ("vocal");

        for (const bool delay : { false, true })
        {
            if (delay && ! vocal)
                continue;       // an instrument's delay stays in its chain

            const int f = target (vocal, delay);

            if (f < 0)
                continue;

            if (std::any_of (out.song.mixerEffects.begin(), out.song.mixerEffects.end(), [&] (const auto& x) { return mixerEffectFromWords (x.effect, mixer) == f; }))
                continue;

            int slot = -1;
            for (int i = 0; i < (int) model.slots.size() && slot < 0; ++i)
            {
                const auto& s = model.slots[(size_t) i];

                if (s.on && (delay ? isMixerDelay (s.type) && s.param ("throw", 0.0f) < 0.5f : isMixerReverb (s.type)))
                    slot = i;
            }

            if (std::find (out.sends.begin(), out.sends.end(), std::pair<int, int> { ch, f }) == out.sends.end())
                out.sends.push_back ({ ch, f });

            if (slot < 0)
            {
                // The song has none switched on. The send belongs to the venue, so the song says
                // "none" with the effect's own level at nothing.
                fx::ChainModel quiet = mixer.fx[(size_t) f].effect;
                quiet.slots.resize (1);
                quiet.slots[0].setParam (delay ? "mix" : "reverb", 0.0f);
                out.song.mixerEffects.push_back (mixerEffectLine (f, quiet, mixer));
                out.changed = true;
                continue;
            }

            fx::ChainModel one;
            one.slots = { model.slots[(size_t) slot] };
            one.slots[0].on = true;
            if (delay)
                one.slots[0].setParam ("throw", 0.0f);
            out.song.mixerEffects.push_back (mixerEffectLine (f, one, mixer));

            std::erase_if (out.song.settings, [&] (const SongSounds::Setting& x)
            {
                return x.setting == "on" && chainFromWords (x.chain, chainNames) == c && slotFromWords (model, x.effect) == slot;
            });
            out.song.settings.push_back ({ words, slotWords (model, slot), "on", "0" });
            out.changed = true;
            out.notes.add (words + "'s " + slotWords (model, slot) + " to " + mixerWords (mixer.fx, f, "fx"));

            if (delay)
                for (int i = slot + 1; i < (int) model.slots.size(); ++i)
                    if (model.slots[(size_t) i].on && isMixerDelay (model.slots[(size_t) i].type) && model.slots[(size_t) i].param ("throw", 0.0f) < 0.5f)
                        out.notes.add ("(" + words + " has a second delay switched on, " + slotWords (model, i) + ": it stays in the chain)");
        }
    }

    return out;
}

std::vector<Mixer::SongFx> resolveMixerEffects (const SongSounds& song, const SongSounds* fallback, const MixerSetup& mixer, juce::StringArray& problems)
{
    const auto effectOf = [&] (const juce::String& words) { return mixerEffectFromWords (words, mixer); };

    std::vector<Mixer::SongFx> list;
    const auto add = [&] (const SongSounds& from, const char* whose)
    {
        for (const auto& x : from.mixerEffects)
        {
            const int f = effectOf (x.effect);
            const auto said = juce::String (whose) + " \"mixer effect = " + x.effect + " / " + x.type + "\": ";

            if (f < 0)
            {
                problems.add (said + "the mixer has no reverb or delay called \"" + x.effect + "\" in the venue that is loaded, so the line was left out.");
                continue;
            }

            if (std::find (mixerFxChoices().begin(), mixerFxChoices().end(), x.type.toStdString()) == mixerFxChoices().end())
            {
                problems.add (said + "\"" + x.type + "\" is not a reverb or delay the mixer has, so the line was left out.");
                continue;
            }

            if (std::any_of (list.begin(), list.end(), [f] (const Mixer::SongFx& s) { return s.fx == f; }))
                continue;

            fx::SlotModel slot;
            slot.type = x.type.toStdString();
            const auto made = fx::makeEffect (slot.type);

            for (const auto& v : x.values)
            {
                if (made != nullptr && made->paramIndex (v.first.toStdString()) < 0)
                {
                    problems.add (said + "that effect has no setting called \"" + v.first + "\", so that one was left out.");
                    continue;
                }

                slot.setParam (v.first.toStdString(), v.second);
            }

            fx::ChainModel m;
            m.slots = { slot };
            list.push_back ({ f, m });
        }
    };

    add (song, "The song's line");

    if (fallback != nullptr)
        add (*fallback, "The Default sounds line");

    return list;
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

            if (words == "pedal toe")
                a.action = EffectsRack::actionPedalToe;

            const bool anyChain = a.action == 2 || a.action == 4 || a.action == EffectsRack::actionPedalToe;

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

        const auto at = built.stackPoint();

        if (built.addFrom (m, fx::Chain::maxSlots) < (int) m.slots.size())
            return {};

        owners.insert (owners.begin() + (std::ptrdiff_t) at, m.slots.size(), i);
    }

    return owners;
}

// "Update its presets" after an effect was added to or taken out of a chain built from presets.
// Each effect that was there when the chain loaded stays with the preset it came from. A new
// effect joins the first preset (the base), unless it sits between two effects of one other
// preset. The presets are only written if stacking them again gives exactly the chain on screen.
static juce::String saveNewShapeToPresets (EffectsRack& rack, int c, const juce::File& presetFolder, const juce::StringArray& recipe,
                                           const std::vector<int>& owners, const PresetLoader& load)
{
    const auto& model = rack.chain (c).model();
    const auto& base = rack.baseline (c);
    const juce::String cannot = "Nothing was saved: ";
    const juce::String useSaveAs = " Use Save as for a new preset.";

    if (owners.empty() || owners.size() != base.slots.size())
        return cannot + "the presets this chain was built from have changed on disk since it was loaded. Load the song again first.";

    // Line the chain on screen up with the chain as loaded (longest run of the same effects in the same order).
    const auto n = base.slots.size(), m = model.slots.size();
    std::vector<std::vector<int>> len (n + 1, std::vector<int> (m + 1, 0));

    for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;)
            len[i][j] = base.slots[i].type == model.slots[j].type ? len[i + 1][j + 1] + 1 : juce::jmax (len[i + 1][j], len[i][j + 1]);

    std::vector<int> wasAt (m, -1);

    for (size_t i = 0, j = 0; i < n && j < m;)
    {
        if (base.slots[i].type == model.slots[j].type && len[i][j] == len[i + 1][j + 1] + 1)
            wasAt[j++] = (int) i++;
        else if (len[i + 1][j] >= len[i][j + 1])
            ++i;
        else
            ++j;
    }

    std::vector<fx::ChainModel> next;
    std::vector<bool> touched;

    // Returns an empty string when the presets, stacked again, give the chain on screen.
    auto attempt = [&] (int mode) -> juce::String
    {
    std::vector<int> owner (m, 0);

    for (size_t j = 0; j < m; ++j)
        if (wasAt[j] >= 0)
            owner[j] = owners[(size_t) wasAt[j]];

    for (size_t j = 0; j < m; ++j)
    {
        if (wasAt[j] >= 0)
            continue;

        int before = -1, after = -1;
        for (size_t k = j; k-- > 0 && before < 0;)
            if (wasAt[k] >= 0) before = owner[k];
        for (size_t k = j + 1; k < m && after < 0; ++k)
            if (wasAt[k] >= 0) after = owner[k];

        // Between two presets: the base first, then the preset of the effect after it, then of the one before.
        owner[j] = before >= 0 && after >= 0 ? (before == after ? before : (mode == 0 ? 0 : mode == 1 ? after : before)) : 0;
    }

    // The presets as they would be saved.
    next.assign ((size_t) recipe.size(), {});
    touched.assign ((size_t) recipe.size(), false);
    std::vector<size_t> kept ((size_t) recipe.size(), 0);

    for (int i = 0; i < recipe.size(); ++i)
    {
        fx::ChainModel onDisk;

        if (! load (recipe[i], onDisk))
            return cannot + "the preset \"" + recipe[i] + "\" is no longer in the folder.";

        for (size_t j = 0; j < m; ++j)
        {
            if (owner[j] != i)
                continue;

            const auto& is = model.slots[j];

            if (wasAt[j] < 0)
            {
                next[(size_t) i].slots.push_back (is);       // a new effect, as it is set now
                touched[(size_t) i] = true;
                continue;
            }

            // An effect that was there: the preset's own copy, plus only what was changed since loading.
            size_t inPreset = 0;
            for (int k = 0; k < wasAt[j]; ++k)
                inPreset += owners[(size_t) k] == i ? 1 : 0;

            if (inPreset >= onDisk.slots.size() || onDisk.slots[inPreset].type != is.type)
                return cannot + "the preset \"" + recipe[i] + "\" has changed on disk since the chain was loaded. Load the song again first.";

            auto into = onDisk.slots[inPreset];
            const auto& was = base.slots[(size_t) wasAt[j]];
            const auto& facts = factsFor (is.type);

            if (was.on != is.on)
            {
                into.on = is.on;
                touched[(size_t) i] = true;
            }

            for (const auto& p : facts.params)
                if (const float v = is.param (p.id, p.def); p.id != std::string ("pedal") && std::abs (v - was.param (p.id, p.def)) > 1.0e-4f * juce::jmax (1.0f, p.max - p.min))
                {
                    into.setParam (p.id, v);
                    touched[(size_t) i] = true;
                }

            for (const auto& key : facts.textKeys)
                if (const auto v = is.text (key); ! v.empty() && v != was.text (key))
                {
                    into.setText (key, v);
                    touched[(size_t) i] = true;
                }

            next[(size_t) i].slots.push_back (into);
            ++kept[(size_t) i];
        }

        if (kept[(size_t) i] != onDisk.slots.size())
            touched[(size_t) i] = true;                      // an effect was taken out of this preset

        if (next[(size_t) i].slots.empty())
            return cannot + "every effect of the preset \"" + recipe[i] + "\" was taken out of the chain, so the chain is no longer made of it." + useSaveAs;
    }

    // Would loading the presets again give the chain on screen? If not, nothing is written.
    fx::ChainModel again = next[0];
    bool fits = true;

    for (int i = 1; i < recipe.size() && fits; ++i)
        fits = again.addFrom (next[(size_t) i], fx::Chain::maxSlots) == (int) next[(size_t) i].slots.size();

    fits = fits && again.slots.size() == m;

    for (size_t j = 0; fits && j < m; ++j)
        fits = again.slots[j].type == model.slots[j].type;

    if (fits)
        return {};

    return cannot + "with the effects where they are now, the chain no longer splits into " + recipe.joinIntoString (" + ")
               + " (a stacked preset always lands in front of the amp, and of a volume pedal right before it)." + useSaveAs;
    };

    juce::String problem;

    for (int mode = 0; mode < 3; ++mode)
        if (problem = attempt (mode); problem.isEmpty())
            break;

    if (problem.isNotEmpty())
        return problem;

    juce::StringArray changed;

    for (int i = 0; i < recipe.size(); ++i)
    {
        if (! touched[(size_t) i])
            continue;

        const auto file = presetFolder.getChildFile (juce::File::createLegalFileName (recipe[i]) + ".txt");

        if (! file.replaceWithText ("# GigRig effect preset \"" + recipe[i] + "\" (build " + juce::String (GIGRIG_BUILD_LABEL) + ")\n" + juce::String (next[(size_t) i].toText())))
            return "Could not write " + file.getFullPathName();

        changed.add ("\"" + recipe[i] + "\"");
    }

    rack.setBaseline (c, model);

    return "Saved the chain's new line-up into " + changed.joinIntoString (" and ") + ". Every song that names " + (changed.size() == 1 ? "it" : "them")
           + " gets the change when it is next loaded.";
}


juce::String saveChainAsPreset (EffectsRack& rack, int chain, const juce::File& presetFolder, const juce::String& typedName, bool& saved)
{
    saved = false;
    const auto name = juce::File::createLegalFileName (typedName.trim());

    if (name.isEmpty())
        return "Type a name for the preset first.";

    if (const auto recipe = rack.recipe (chain); recipe.size() > 1 && recipe.contains (name, true))
        return "Not saved: this chain is made of " + recipe.joinIntoString (" + ") + ", and saving all of it as \"" + name
             + "\" would put the other presets' effects inside it. Press Update its presets instead, or type a new name.";

    presetFolder.createDirectory();
    const auto file = presetFolder.getChildFile (name + ".txt");
    saved = file.replaceWithText ("# GigRig effect preset \"" + name + "\" (build " + juce::String (GIGRIG_BUILD_LABEL) + ")\n"
                                  + juce::String (rack.chain (chain).model().toText()));

    if (! saved)
        return "Could not save " + file.getFullPathName();

    rack.setRecipe (chain, juce::StringArray (name));     // the chain is now exactly this preset
    rack.setBaseline (chain, rack.chain (chain).model());
    rack.markChanged();
    return "Saved the preset \"" + name + "\".";
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
        return saveNewShapeToPresets (rack, c, presetFolder, recipe, owners, load);

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
                if (const float v = is.param (p.id, p.def); p.id != std::string ("pedal") && std::abs (v - was.param (p.id, p.def)) > 1.0e-4f * juce::jmax (1.0f, p.max - p.min))
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

        if (! sounds.layoutLines.isEmpty())
        {
            SongSetup scratch;
            layout::fromLines (sounds.layoutLines, scratch, sounds.notUnderstood);
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
    loadedSong = layoutOf (index);
    tried = false;
    engine.looper().setSong (loadedSong);

    if (withSounds)
        applySounds (why);
    else
        changed();

    if (onLoaded != nullptr)
        onLoaded();
}

SongSetup SongBook::layoutOf (int index) const
{
    if (index < 0 || index >= numEntries())
        return builtInSong (0);

    const auto& e = entries[(size_t) index];
    auto song = builtInSong (e.layout);

    if (! e.builtIn)
    {
        std::memset (song.name, 0, sizeof (song.name));
        e.name.copyToUTF8 (song.name, sizeof (song.name) - 1);
        song.rcMemory = 0;
    }

    // The song's own looper lines (written by the builder on the remote screen) sit on top of
    // the layout it started from. Lines that cannot be used were reported when the folder was read.
    if (! e.sounds.layoutLines.isEmpty())
    {
        juce::StringArray ignored;
        layout::fromLines (e.sounds.layoutLines, song, ignored);
    }

    // The song's own "tempo =" line replaces the tempo saved in the RC-600 memory as the
    // looper's guide (Yellow was saved at double speed, so its first loop counted twice the bars).
    if (e.sounds.tempo > 0.0)
    {
        song.savedBpm = e.sounds.tempo;
        song.tempoGuide = true;
    }

    return song;
}

void SongBook::tryLayout (const SongSetup& wanted)
{
    auto song = wanted;
    std::memcpy (song.name, loadedSong.name, sizeof (song.name));
    song.rcMemory = loadedSong.rcMemory;
    loadedSong = song;
    tried = true;
    engine.looper().setSong (loadedSong);
    note = "Trying a looper layout for \"" + currentEntry().name + "\". Nothing is saved yet.";
    changed();
}

juce::String SongBook::saveLayout (const SongSetup& wanted, bool withTempo)
{
    const auto e = currentEntry();
    auto s = e.sounds;
    s.notUnderstood.clear();
    s.layoutLines = layout::toLines (wanted);

    if (withTempo && wanted.savedBpm >= 30.0 && wanted.savedBpm <= 300.0)
        s.tempo = wanted.savedBpm;

    if (! e.hasFile)
    {
        if (! e.builtIn)
            s.looper = builtInSong (e.layout).title();
        if (! e.builtIn || e.program != builtInSong (e.layout).rcMemory)
            s.program = e.program;
    }

    if (! writeSong (e.name, s))
        return note = "Could not write the song's file in " + songFolder.getFullPathName();

    // Was this very layout being tried? Then the looper already has it and the loops can stay.
    const auto same = [] (const SongSetup& a, const SongSetup& b) { return layout::toLines (a) == layout::toLines (b) && juce::approximatelyEqual (a.savedBpm, b.savedBpm); };
    auto mine = wanted;
    if (! withTempo)
        mine.savedBpm = loadedSong.savedBpm;
    const bool keepLoops = tried && same (mine, loadedSong);

    rescan();

    if (keepLoops)
    {
        tried = false;
        note = "Saved the looper layout for \"" + e.name + "\". The loops were left alone.";
        changed();
    }
    else
    {
        load (current, "its looper layout was saved", false);
        note = "Saved the looper layout for \"" + e.name + "\".";
        changed();
    }

    return {};
}

juce::String SongBook::addEntryWithLayout (const juce::String& wanted, int program, const SongSetup& song, bool withTempo)
{
    if (const auto error = addEntry (wanted, program); error.isNotEmpty())
        return error;

    return saveLayout (song, withTempo);
}

juce::String SongBook::forgetLayout()
{
    const auto e = currentEntry();

    if (e.sounds.layoutLines.isEmpty())
    {
        if (tried)
            load (current, "the tried layout was dropped", false);

        return note = "\"" + e.name + "\" has no looper layout of its own in its file.";
    }

    auto s = e.sounds;
    s.notUnderstood.clear();
    s.layoutLines.clear();

    if (! writeSong (e.name, s))
        return note = "Could not write the song's file in " + songFolder.getFullPathName();

    rescan();
    load (current, "its own looper layout was taken out", false);
    note = "\"" + e.name + "\" is back on the looper layout it had before.";
    changed();
    return {};
}

void SongBook::applySounds (const juce::String& why)
{
    engine.effects().ampNote.clear();       // what the amp switch last did belongs to the sounds that were there before

    auto& rack = engine.effects();
    const auto& e = currentEntry();
    std::array<fx::ChainModel, EffectsRack::numChains> now;

    for (int c = 0; c < numChains; ++c)
        now[(size_t) c] = rack.chain (c).model();

    auto r = resolveSounds (e.sounds, hasDefault() ? &defaultSounds : nullptr, chainNames(), now, loader());
    int moved = 0, rebuilt = 0;

    // Where a volume pedal sits is where the pedal is, never what a preset or song happened to
    // save (Tom, 27 Nov 2026: some songs loaded with the electric pedal at 100 and the acoustic's
    // at 0, so both played at full until the pedal moved). Once the pedal has moved, its place;
    // before that, where the volume pedals on screen are now.
    float pedalAt = rack.livePedal();

    if (! rack.pedalHasMoved())
    {
        bool found = false;

        for (int c = 0; c < numChains && ! found; ++c)
            for (const auto& slot : now[(size_t) c].slots)
                if (slot.type == "volume")
                {
                    pedalAt = slot.param ("pedal", pedalAt);
                    found = true;
                    break;
                }
    }

    for (int c = 0; c < numChains; ++c)
        for (auto& slot : r.model[(size_t) c].slots)
            if (slot.type == "volume")
                slot.setParam ("pedal", pedalAt);

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

    // The song's sends to the mixer's reverbs and delays: on top of the act, gone with the next
    // song, and never a change to the venue (the reverbs and delays themselves).
    engine.mixer().setSongSends (resolveSends (e.sounds, hasDefault() ? &defaultSounds : nullptr, engine.mixer().setup(), problems));
    engine.mixer().setSongFx (resolveMixerEffects (e.sounds, hasDefault() ? &defaultSounds : nullptr, engine.mixer().setup(), problems));

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

        if (onLoaded != nullptr)
            onLoaded();
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

    {
        // The mixer sends that belong to the song, by the channel's and the effect's names
        // (by number where a name is empty or used twice).
        const auto& mix = engine.mixer().setup();
        const auto unique = [] (const juce::String& name, const auto& list)
        {
            return name.trim().isNotEmpty() && ! name.containsAnyOf ("/=") && std::count_if (list.begin(), list.end(), [&name] (const auto& x) { return x.name.trim().equalsIgnoreCase (name.trim()); }) == 1;
        };

        for (const auto& x : engine.mixer().songSends())
        {
            const auto& ch = mix.channel[(size_t) x.channel].name;
            const auto& fxName = mix.fx[(size_t) x.fx].name;
            s.sends.push_back ({ unique (ch, mix.channel) ? ch.trim() : "channel " + juce::String (x.channel + 1),
                                 unique (fxName, mix.fx) ? fxName.trim() : "fx " + juce::String (x.fx + 1), x.db });
        }
    }

    // The mixer reverbs and delays that belong to the song: which one, what it is, and every setting.
    for (const auto& x : engine.mixer().songFx())
        if (! x.effect.slots.empty())
            s.mixerEffects.push_back (mixerEffectLine (x.fx, x.effect, engine.mixer().setup()));

    s.hasSwitches = true;

    for (int i = 0; i < EffectsRack::numPedalAssigns; ++i)
        if (const auto a = rack.pedalAssign (i); a.sw >= 0)
            if (const auto words = switchActionWords (a.action, rack.chain (a.chain).model()); words.isNotEmpty())
                s.switches.push_back ({ a.sw, a.action == 2 || a.action == 4 || a.action == EffectsRack::actionPedalToe ? juce::String() : chainWordsFor (a.chain), words });

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

    s.tempo = e.sounds.tempo;
    s.layoutLines = e.sounds.layoutLines;

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

juce::String SongBook::keepMixerEffect (int f, bool asDefault)
{
    auto& mixer = engine.mixer();

    if (f < 0 || f >= numMixFx || mixer.effectNow (f).slots.empty())
        return note = "There is no reverb or delay there to save.";

    const auto effect = mixer.effectNow (f);
    const auto e = currentEntry();
    auto s = asDefault ? defaultSounds : e.sounds;
    const bool songHasOwn = std::any_of (e.sounds.mixerEffects.begin(), e.sounds.mixerEffects.end(),
                                         [&] (const auto& x) { return mixerEffectFromWords (x.effect, mixer.setup()) == f; });
    s.notUnderstood.clear();
    putMixerEffect (s, mixerEffectLine (f, effect, mixer.setup()), mixer.setup());

    if (! asDefault && ! e.hasFile)
    {
        if (! e.builtIn)
            s.looper = builtInSong (e.layout).title();
        if (! e.builtIn || e.program != builtInSong (e.layout).rcMemory)
            s.program = e.program;
    }

    if (! writeSong (asDefault ? juce::String (defaultFileName) : e.name, s))
        return note = "Could not write the file in " + songFolder.getFullPathName();

    rescan();

    // The effect on screen now is the song's (or the default's, which every song without its own follows).
    if (! asDefault || ! songHasOwn)
        mixer.setSongFx (f, effect);

    const auto name = mixer.setup().fx[(size_t) f].name;
    note = asDefault ? "Saved " + name + " as it is now into the default sounds: every song without its own " + name + " uses it."
                       + (songHasOwn ? " \"" + e.name + "\" has its own, so it keeps its own." : juce::String())
                     : "Saved " + name + " as it is now for \"" + e.name + "\". Nothing else in the song's file was changed.";
    changed();
    return note;
}

bool dropMixerEffect (SongSounds& s, int f, const MixerSetup& mixer)
{
    return std::erase_if (s.mixerEffects, [&] (const SongSounds::MixerEffect& x) { return mixerEffectFromWords (x.effect, mixer) == f; }) > 0;
}

juce::String SongBook::dropMixerEffect (int f)
{
    auto& mixer = engine.mixer();

    if (f < 0 || f >= numMixFx)
        return note = "There is no reverb or delay there.";

    const auto name = mixer.setup().fx[(size_t) f].name;
    const auto e = currentEntry();
    auto s = e.sounds;
    const auto unread = s.notUnderstood;
    s.notUnderstood.clear();

    if (e.hasFile && ! unread.isEmpty())
        return note = "\"" + e.name + "\" has a line the app does not understand, so its file was left alone. See the Songs tab.";

    const bool had = e.hasFile && gigrig::dropMixerEffect (s, f, mixer.setup());

    if (had && ! writeSong (e.name, s))
        return note = "Could not write the file in " + songFolder.getFullPathName();

    if (had)
        rescan();

    // What this effect is now: the default sounds' where they have one, else the venue's own.
    juce::StringArray unused;
    const auto fromDefault = resolveMixerEffects (SongSounds(), hasDefault() ? &defaultSounds : nullptr, mixer.setup(), unused);
    const auto it = std::find_if (fromDefault.begin(), fromDefault.end(), [f] (const Mixer::SongFx& x) { return x.fx == f; });

    if (it != fromDefault.end())    mixer.setSongFx (f, it->effect);
    else                            mixer.clearSongFx (f);

    const juce::String now = it != fromDefault.end() ? "the default sounds' " + name : "the venue's own " + name + " (the default sounds have none)";
    note = had ? "\"" + e.name + "\" no longer has its own " + name + ", so it follows " + now + ". Nothing else in the song's file was changed."
               : "\"" + e.name + "\" had no " + name + " of its own in its file. It is set back to " + now + ".";
    changed();
    return note;
}

juce::File SongBook::newestBackup() const
{
    auto folders = songFolder.getParentDirectory().findChildFiles (juce::File::findDirectories, false, songFolder.getFileName() + " backup *");
    folders.sort();
    return folders.isEmpty() ? juce::File() : folders.getLast();
}

juce::String SongBook::moveSoundsToMixer()
{
    auto& mixer = engine.mixer();
    const auto& rack = engine.effects();
    const auto& setup = mixer.setup();
    moveLog.clear();

    // Which mixer channel each chain feeds: the first channel in use that takes its socket.
    std::array<int, numChains> chainChannel;
    chainChannel.fill (-1);

    juce::StringArray feeds;

    for (int c = 0; c < numChains; ++c)
    {
        const auto route = rack.routing (c);

        if (! route.on)
            continue;

        for (int ch = numMixChannels - 1; ch >= 0; --ch)
            if (setup.channel[(size_t) ch].on && setup.channel[(size_t) ch].socket == route.source)
                chainChannel[(size_t) c] = ch;

        // No channel on its socket: the channel that has the chain's own name.
        if (chainChannel[(size_t) c] < 0 && route.name.trim().isNotEmpty())
            for (int ch = numMixChannels - 1; ch >= 0; --ch)
                if (setup.channel[(size_t) ch].on && setup.channel[(size_t) ch].name.trim().equalsIgnoreCase (route.name.trim()))
                    chainChannel[(size_t) c] = ch;

        const int ch = chainChannel[(size_t) c];
        feeds.add ("chain " + juce::String (c + 1) + " (" + route.name + ", socket " + juce::String (route.source + 1) + ") "
                   + (ch >= 0 ? "feeds the mixer's " + setup.channel[(size_t) ch].name + " channel"
                              : juce::String ("feeds no mixer channel that is on, so nothing of it was moved")));
    }

    moveLog.add ("How the chains meet the mixer: " + feeds.joinIntoString ("; ") + ".");

    if (std::all_of (chainChannel.begin(), chainChannel.end(), [] (int ch) { return ch < 0; }))
        return note = "Nothing was moved: no effects chain that is on feeds a mixer channel that is on. On the Mixer tab, each channel's 'Comes from' box must say the socket its chain is on. " + feeds.joinIntoString ("; ") + ".";

    const auto backup = songFolder.getParentDirectory().getChildFile (songFolder.getFileName() + " backup " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"));

    if (! songFolder.isDirectory() || ! songFolder.copyDirectoryTo (backup))
        return note = "Nothing was moved: could not copy the songs folder to " + backup.getFullPathName() + " first.";

    const auto names = chainNames();
    const auto load = loader();
    int moved = 0, failed = 0;
    std::vector<std::pair<int, int>> sends;
    const auto noteSends = [&sends] (const MovedToMixer& m)
    {
        for (const auto& s : m.sends)
            if (std::find (sends.begin(), sends.end(), s) == sends.end())
                sends.push_back (s);
    };

    if (hasDefault())
    {
        const auto m = moveToMixer (defaultSounds, names, chainChannel, setup, load);
        noteSends (m);

        if (m.changed && ! writeSong (defaultFileName, m.song))
            ++failed;
        else if (m.changed)
            ++moved;

        if (! m.notes.isEmpty())
            moveLog.add ("Default sounds: " + m.notes.joinIntoString ("; "));
    }

    const auto list = entries;

    for (const auto& e : list)
    {
        if (! e.hasFile || ! e.sounds.saysAnything())
            continue;

        const auto m = moveToMixer (e.sounds, names, chainChannel, setup, load);
        noteSends (m);

        if (m.changed && ! writeSong (e.name, m.song))
            ++failed;
        else if (m.changed)
            ++moved;

        if (! m.notes.isEmpty())
            moveLog.add (e.name + ": " + m.notes.joinIntoString ("; "));
    }

    // The send level is the venue's choice, not the song's (Tom, 27 Nov 2026). A send that is off
    // would leave the moved reverb or delay unheard, so it is opened at 0 dB, where the sound is
    // what it was in the chain. Sends already open are left as they are.
    juce::StringArray opened;

    for (const auto& [ch, f] : sends)
    {
        if (mixer.hasSongSend (ch, f) || mixer.setup().channel[(size_t) ch].fxDb[(size_t) f] > mixOffDb)
            continue;

        auto next = mixer.setup().channel[(size_t) ch];
        next.fxDb[(size_t) f] = 0.0f;
        mixer.setChannel (ch, next);
        opened.add (mixer.setup().channel[(size_t) ch].name + " to " + mixer.setup().fx[(size_t) f].name);
    }

    // A mixer reverb or delay that is switched off would leave the moved sound unheard too.
    juce::StringArray switchedOn;

    for (const auto& pair : sends)
    {
        const int f = pair.second;

        if (mixer.setup().fx[(size_t) f].on)
            continue;

        auto next = mixer.setup().fx[(size_t) f];
        next.on = true;
        mixer.setFx (f, next);
        switchedOn.add (next.name);
    }

    if (! switchedOn.isEmpty())
        moveLog.add ("Mixer effects switched on (they were off): " + switchedOn.joinIntoString (", ") + ". Save your venue on the Mixer tab to keep them on.");

    if (! opened.isEmpty())
        moveLog.add ("Sends opened at 0 dB (they were off): " + opened.joinIntoString (", ") + ". Save your act on the Mixer tab to keep them.");

    rescan();
    applySounds ("reverbs and delays were moved to the mixer");
    note = "Moved to the mixer in " + juce::String (moved) + " song file" + (moved == 1 ? "" : "s") + "."
         + (failed > 0 ? " " + juce::String (failed) + " could not be written." : juce::String())
         + (moved == 0 ? " " + feeds.joinIntoString ("; ") + "." + (sends.empty() ? juce::String (" No song names a preset with a reverb or delay switched on for the vocal or the acoustic.") : juce::String (" The songs already had their mixer lines.")) : juce::String())
         + (switchedOn.isEmpty() ? juce::String() : " Switched on in the mixer: " + switchedOn.joinIntoString (", ") + " (save your venue to keep them on).")
         + (opened.isEmpty() ? juce::String() : " Sends opened at 0 dB: " + opened.joinIntoString (", ") + " (save your act on the Mixer tab to keep them).")
         + " The files as they were are in \"" + backup.getFileName() + "\" beside the songs folder; 'Put the songs back' undoes it."
         + (mixer.isOn() ? juce::String() : " The mixer is switched OFF: these reverbs and delays are only heard with the mixer on.");
    changed();
    return note;
}

juce::String SongBook::restoreSongsBackup()
{
    const auto backup = newestBackup();

    if (! backup.isDirectory())
        return note = "There is no backup to put back: 'Move reverbs and delays to the mixer' makes one each time it is pressed.";

    int count = 0;

    for (const auto& f : backup.findChildFiles (juce::File::findFiles, false, "*.txt"))
        if (f.copyFileTo (songFolder.getChildFile (f.getFileName())))
            ++count;

    moveLog.clear();
    rescan();
    applySounds ("the song files were put back from a backup");
    note = "Put back " + juce::String (count) + " song files from \"" + backup.getFileName() + "\".";
    changed();
    return note;
}

juce::StringArray SongBook::unusedPresets() const
{
    juce::StringArray used;

    const auto take = [&used] (const SongSounds& sounds)
    {
        for (const auto& line : sounds.chains)
            for (const auto& name : line.presets)
                used.addIfNotAlreadyThere (juce::File::createLegalFileName (name.trim()), true);
    };

    take (defaultSounds);

    for (const auto& e : entries)
        take (e.sounds);

    for (int c = 0; c < numChains; ++c)
        for (const auto& name : engine.effects().recipe (c))
            used.addIfNotAlreadyThere (juce::File::createLegalFileName (name.trim()), true);

    juce::StringArray unused;

    for (const auto& f : presetFolder.findChildFiles (juce::File::findFiles, false, "*.txt"))
        if (! used.contains (f.getFileNameWithoutExtension(), true))
            unused.add (f.getFileNameWithoutExtension());

    unused.sortNatural();
    return unused;
}

namespace
{
    const char* const notUsedPrefix = "Not used (moved ";

    juce::File newestNotUsedFolder (const juce::File& presetFolder)
    {
        juce::File newest;

        for (const auto& d : presetFolder.findChildFiles (juce::File::findDirectories, false, juce::String (notUsedPrefix) + "*"))
            if (! newest.isDirectory() || d.getFileName().compareNatural (newest.getFileName()) > 0)
                newest = d;

        return newest;
    }
}

juce::String SongBook::tidyPresets()
{
    rescan();       // the song files as they are on disk right now
    const auto unused = unusedPresets();
    const auto now = juce::Time::currentTimeMillis();

    if (unused.isEmpty())
    {
        tidyOffered.clear();
        return note = "Nothing to tidy: every preset in the folder is named by a song, by the default sounds or by a chain on screen.";
    }

    if (unused != tidyOffered || now - tidyOfferedAt > 60000)
    {
        tidyOffered = unused;
        tidyOfferedAt = now;
        return note = juce::String (unused.size()) + " preset" + (unused.size() == 1 ? " is" : "s are") + " not named by any song, by the default sounds or by a chain on screen: "
                    + unused.joinIntoString (", ") + ". Press 'Tidy unused presets' again within a minute to move "
                    + (unused.size() == 1 ? "it" : "them") + " into a 'Not used' folder inside the presets folder. Nothing is deleted, and 'Put the presets back' undoes it.";
    }

    tidyOffered.clear();
    const auto folder = presetFolder.getChildFile (juce::String (notUsedPrefix) + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S") + ")");

    if (! folder.createDirectory())
        return note = "Nothing was moved: could not make the folder " + folder.getFullPathName();

    int moved = 0;
    juce::StringArray stuck;

    for (const auto& name : unused)
    {
        const auto from = presetFolder.getChildFile (name + ".txt");

        if (from.moveFileTo (folder.getChildFile (from.getFileName())))
            ++moved;
        else
            stuck.add (name);
    }

    note = "Moved " + juce::String (moved) + " unused preset" + (moved == 1 ? "" : "s") + " into \"" + folder.getFileName() + "\" inside the presets folder. "
         + juce::String (presetFolder.findChildFiles (juce::File::findFiles, false, "*.txt").size()) + " presets are left. 'Put the presets back' undoes this."
         + (stuck.isEmpty() ? juce::String() : " Could not move: " + stuck.joinIntoString (", ") + ".");
    changed();
    return note;
}

juce::String SongBook::restoreTidiedPresets()
{
    const auto folder = newestNotUsedFolder (presetFolder);

    if (! folder.isDirectory())
        return note = "There is nothing to put back: 'Tidy unused presets' has not moved any presets aside.";

    int back = 0;
    juce::StringArray kept;

    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.txt"))
    {
        const auto to = presetFolder.getChildFile (f.getFileName());

        if (to.existsAsFile())
            kept.add (f.getFileNameWithoutExtension());     // a preset of that name was made since: both are kept
        else if (f.moveFileTo (to))
            ++back;
    }

    if (folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        folder.deleteFile();

    note = "Put " + juce::String (back) + " preset" + (back == 1 ? "" : "s") + " back from \"" + folder.getFileName() + "\"."
         + (kept.isEmpty() ? juce::String() : " Left where they are, because a preset of the same name exists now: " + kept.joinIntoString (", ") + ".");
    rescan();
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
    s.tempo = e.sounds.tempo;
    s.looper = e.sounds.looper;
    s.layoutLines = e.sounds.layoutLines;
    const auto file = songFolder.getChildFile (juce::File::createLegalFileName (e.name) + ".txt");
    const bool ok = e.builtIn && s.program == 0 && s.layoutLines.isEmpty() ? file.deleteFile() : writeSong (e.name, s);

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
    s.layoutLines = currentEntry().sounds.layoutLines;      // a copy of a song with its own layout keeps that layout
    s.tempo = currentEntry().sounds.tempo;

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

juce::String SongBook::checkEverySong (bool everySetting)
{
    juce::String t;
    int withProblems = 0, withFiles = 0;

    for (int i = 0; i < numEntries(); ++i)
    {
        load (i, "checking every song");
        const auto& e = entries[(size_t) i];
        withFiles += e.hasFile ? 1 : 0;
        juce::StringArray lines (problems);

        for (const auto& l : e.sounds.notUnderstood)
            lines.add ("Line not understood: " + l);

        withProblems += lines.isEmpty() ? 0 : 1;
        t << (lines.isEmpty() ? "ok       " : "PROBLEM  ") << statusLine() << "\n";

        for (const auto& l : lines)
            t << "           " << l << "\n";

        if (everySetting)
        {
            auto& rack = engine.effects();

            for (int c = 0; c < EffectsRack::numChains; ++c)
            {
                const auto& m = rack.chain (c).model();

                for (int k = 0; k < (int) m.slots.size(); ++k)
                {
                    const auto& slot = m.slots[(size_t) k];
                    t << "    chain " << (c + 1) << " slot " << (k + 1) << " " << juce::String (slot.type) << (slot.on ? " on" : " off");

                    if (auto* fx = rack.chain (c).effect (k))
                        for (int p = 0; p < (int) fx->params().size(); ++p)
                            t << " " << juce::String (fx->params()[(size_t) p].id) << "=" << juce::String ((double) fx->getParam (p), 9);

                    for (const auto& [key, text] : slot.texts)
                        t << " " << juce::String (key) << "=\"" << juce::String (text) << "\"";

                    t << "\n";
                }
            }
        }
    }

    if (numEntries() > 0)
        load (0, "checking every song");

    return "Songs checked: " + juce::String (numEntries()) + " (" + juce::String (withFiles) + " with a file of their own), "
             + juce::String (withProblems) + " with a problem. Default sounds " + (hasDefault() ? "set" : "not set") + ".\n" + t;
}

juce::String SongBook::reportText() const
{
    const auto& rack = engine.effects();
    const auto& e = currentEntry();
    juce::String t;
    t << "--- Songs ---\n";
    t << "Song loaded: " << statusLine() << "\n";
    t << "Looper layout: " << builtInSong (e.layout).title() << (e.builtIn ? "" : " (borrowed: this entry has no looper layout of its own)") << "\n";
    t << "Tempo guide for the first loop: "
      << (e.sounds.tempo > 0.0 ? juce::String (e.sounds.tempo, 1) + " (the song's own tempo line)"
                               : e.builtIn && loadedSong.savedBpm > 0.0 ? juce::String (loadedSong.savedBpm, 1) + " (as saved in the RC-600 memory)"
                                                                         : juce::String ("none (worked out from the loop's length)")) << "\n";
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

    for (const auto& line : moveLog)
        t << "Moved to the mixer: " << line << "\n";

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
