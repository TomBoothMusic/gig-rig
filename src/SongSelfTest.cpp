#include "AmpModels.h"
#include "ReconnectWatch.h"
#include "SongBook.h"
#include "effects/Registry.h"
#include "effects/amp/NamAmp.h"

#include <cmath>
#include <functional>
#include <map>

// Checks for the song-linking batch (25 Nov 2026), run by the automatic builds
// (GigRig --self-test): delays and reverbs ringing out when a chain changes, the song
// files (reading, writing, working out against presets and the default sounds), the song
// book (the Song box, program changes, keeping sounds), the amp-cycling switch and the
// watch that opens the mixer again after it goes away.

namespace gigrig
{

namespace
{
    using Check = std::function<void (bool, const juce::String&)>;
    constexpr double rate = 96000.0;
    constexpr int block = 64;
    constexpr double twoPi = 6.283185307179586;

    fx::SlotModel slot (const char* type, std::vector<std::pair<std::string, float>> params = {}, bool on = true)
    {
        return { type, on, std::move (params), {} };
    }

    fx::ChainModel chainOf (std::vector<fx::SlotModel> slots)
    {
        fx::ChainModel m;
        m.slots = std::move (slots);
        return m;
    }

    /** Runs a chain for some blocks: a 330 Hz tone while 'playing', silence otherwise. */
    struct ChainBench
    {
        fx::Chain chain;
        juce::int64 t = 0;
        std::vector<float> buffer = std::vector<float> ((size_t) block, 0.0f);

        ChainBench()    { chain.prepare (rate, block); }

        std::vector<float> run (double seconds, bool playing)
        {
            std::vector<float> heard;
            const int blocks = (int) std::ceil (seconds * rate / block);
            heard.reserve ((size_t) (blocks * block));

            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < block; ++i, ++t)
                    buffer[(size_t) i] = playing ? (float) (0.2 * std::sin (twoPi * 330.0 * (double) t / rate)) : 0.0f;

                chain.process (buffer.data(), block);
                heard.insert (heard.end(), buffer.begin(), buffer.end());

                if (b % 16 == 15)
                    chain.collectGarbage();     // as the app's timer does
            }

            chain.collectGarbage();
            return heard;
        }
    };

    float peak (const std::vector<float>& x, double fromSeconds = 0.0, double toSeconds = 1.0e9)
    {
        float p = 0.0f;
        const auto from = (size_t) juce::jlimit (0.0, (double) x.size(), fromSeconds * rate);
        const auto to = (size_t) juce::jlimit (0.0, (double) x.size(), toSeconds * rate);
        for (size_t i = from; i < to; ++i)
            p = std::max (p, std::abs (x[i]));
        return p;
    }

    float biggestStep (const std::vector<float>& x)
    {
        float s = 0.0f;
        for (size_t i = 1; i < x.size(); ++i)
            s = std::max (s, std::abs (x[i] - x[i - 1]));
        return s;
    }

    juce::String db (float gain)    { return juce::String (juce::Decibels::gainToDecibels (gain, -200.0f), 1) + " dB"; }

    fx::ChainModel delayChain()
    {
        return chainOf ({ slot ("boost", { { "boost", 0.0f }, { "bright", 0.0f } }),
                          slot ("digitaldelay", { { "follow", 0.0f }, { "time", 250.0f }, { "feedback", 60.0f }, { "mix", 50.0f }, { "mod", 0.0f } }) });
    }

    fx::ChainModel plainChain (float boostDb = 0.0f)
    {
        return chainOf ({ slot ("boost", { { "boost", boostDb }, { "bright", 0.0f } }) });
    }
}

void runSongSelfTest (const Check& check)
{
    // ---- delays and reverbs ring out when a chain's effects are changed ----
    {
        // Playing through a delay, then the chain changes to one without it and the playing stops.
        ChainBench b;
        b.chain.setModel (delayChain());
        b.run (0.1, true);                      // the first set-up's own change-over
        const auto before = b.run (1.0, true);
        b.chain.setModel (plainChain());
        const auto across = b.run (0.05, true);
        const bool ringingAtOnce = b.chain.tailRinging();
        const auto after = b.run (3.0, false);

        check (ringingAtOnce, "tails: a chain with a delay that is changed starts ringing out");
        check (peak (after, 0.2, 0.5) > 0.01f && peak (after, 0.45, 0.75) > 0.005f,
               "tails: the old delay's repeats carry on after the chain changes (" + db (peak (after, 0.2, 0.5)) + " a quarter of a second on, "
               + db (peak (after, 0.45, 0.75)) + " after half a second)");
        check (biggestStep (across) < biggestStep (before) * 1.5f + 0.002f,
               "tails: the change-over itself does not click (biggest step " + juce::String (biggestStep (across), 4) + " against " + juce::String (biggestStep (before), 4) + " before)");
        check (peak (across) < peak (before) * 1.6f, "tails: the change-over is no louder than the playing was (" + db (peak (across)) + " against " + db (peak (before)) + ")");

        const auto later = b.run (9.0, false);
        check (! b.chain.tailRinging() && peak (later, 8.5) == 0.0f, "tails: the old effects are let go once the repeats have died away, and then there is silence");

        // The new chain is what is heard from the first moment: +6 dB shows within 20 ms.
        ChainBench c;
        c.chain.setModel (delayChain());
        c.run (0.5, true);
        c.chain.setModel (plainChain (6.0f));
        const auto fresh = c.run (0.05, true);
        check (peak (fresh, 0.03, 0.05) > 0.2f * 1.6f, "tails: the new effects are in within 20 ms while the old ones ring (" + db (peak (fresh, 0.03, 0.05)) + ")");

        // Another change while ringing: the tail steps aside in 10 ms so the new change is not held up.
        c.chain.setModel (plainChain (12.0f));
        c.run (0.3, true);
        const auto third = c.run (0.05, true);
        check (! c.chain.tailRinging() && peak (third) > 0.2f * 3.5f,
               "tails: a second change while the first is still ringing takes over at once (" + db (peak (third)) + ", ringing " + juce::String ((int) c.chain.tailRinging()) + ")");

        // A chain with no delay or reverb changes over in 10 ms, as before.
        ChainBench d;
        d.chain.setModel (plainChain (6.0f));
        d.run (0.2, true);
        d.chain.setModel (plainChain (0.0f));
        d.run (0.002, true);
        const bool plain = ! d.chain.tailRinging();
        const auto settled = d.run (0.1, false);
        check (plain && peak (settled, 0.02) == 0.0f, "tails: a chain with no delay or reverb still changes over in 10 ms and nothing lingers");

        // A delay that is switched off in the old chain does not ring.
        ChainBench e;
        auto off = delayChain();
        off.slots[1].on = false;
        e.chain.setModel (off);
        e.run (0.5, true);
        e.chain.setModel (plainChain());
        e.run (0.02, true);
        check (! e.chain.tailRinging(), "tails: a delay that was switched off has nothing to ring out");

        // The rig setting: tails off cuts them in 10 ms.
        ChainBench f;
        f.chain.setTailsRingOut (false);
        f.chain.setModel (delayChain());
        f.run (1.0, true);
        f.chain.setModel (plainChain());
        f.run (0.02, true);
        const auto cut = f.run (1.0, false);
        check (! f.chain.tailRinging() && peak (cut, 0.05) == 0.0f, "tails: with ringing out switched off, a changed chain's delay stops within 10 ms");

        // A delay that never dies away is faded out and let go at the longest time.
        ChainBench g;
        auto endless = delayChain();
        endless.slots[1].setParam ("feedback", 95.0f);
        g.chain.setModel (endless);
        g.run (2.0, true);
        g.chain.setModel (plainChain());
        const auto ring = g.run (fx::Chain::maxTailSeconds - 2.0, false);
        const bool stillGoing = g.chain.tailRinging() && peak (ring, fx::Chain::maxTailSeconds - 3.0) > 0.001f;
        const auto end = g.run (2.5, false);
        check (stillGoing && ! g.chain.tailRinging() && peak (end, 2.2) == 0.0f && biggestStep (end) < 0.01f,
               "tails: repeats that will not die are faded away by " + juce::String (fx::Chain::maxTailSeconds, 0) + " seconds, without a click");

        // The reverbs and the other delays count as having tails; a drive does not.
        juce::StringArray withTails;
        for (const auto& type : fx::effectTypes())
            if (auto made = type.make(); made->hasTail())
                withTails.add (type.id);
        withTails.sort (true);
        check (withTails.joinIntoString (" ") == "analogdelay digitaldelay plate room spring tapedelay",
               "tails: the delays and reverbs are the effects that ring out (" + withTails.joinIntoString (", ") + ")");
    }

    // ---- a song change that keeps the same effects only moves their settings ----
    {
        ChainBench b;
        b.chain.setModel (delayChain());
        b.run (0.3, true);
        auto* delayBefore = b.chain.effect (1);
        auto same = delayChain();
        same.slots[1].setParam ("mix", 20.0f);
        same.slots[0].on = false;
        const bool gentle = b.chain.setModelGently (same);
        b.run (0.01, true);
        check (gentle && b.chain.effect (1) == delayBefore && ! b.chain.tailRinging()
                 && std::abs (b.chain.effect (1)->getParam (b.chain.effect (1)->paramIndex ("mix")) - 20.0f) < 0.001f
                 && ! b.chain.model().slots[0].on && std::abs (b.chain.model().slots[1].param ("mix", 0.0f) - 20.0f) < 0.001f,
               "songs: a change to a chain with the same effects in the same order moves the settings and restarts nothing");
        check (! b.chain.setModelGently (plainChain()) && b.chain.model().slots.size() == 1,
               "songs: a change to different effects builds the new chain");
    }

    // ---- song files: reading and writing ----
    const juce::StringArray chainNames { "Electric", "Acoustic", "Vocal", "Chain 4" };

    {
        const juce::String text =
            "# a song written by hand\n"
            "program = 11\n"
            "looper = Wicked Game\n"
            "chain 1 = Comp & GE7 for Electric\n"
            "chain 1 = TubeScreamer + Klon Boost\n"
            "Vocal = Starter - Vocal\n"
            "chain 4 = (empty)\n"
            "set = chain 1 / ts / drive = 7.5\n"
            "set = Vocal / Vocal harmony / off\n"
            "switch 9 = chain 1 / next amp\n"
            "switch CTL3 = vocal / delay throw\n"
            "switch 8 = tap tempo\n"
            "amps = chain 1 / Dream 65 - Clean / Dream 65 - Driven\n"
            "this line means nothing\n";
        const auto s = SongSounds::fromText (text);

        check (s.program == 11 && s.looper == "Wicked Game", "song file: the program number and the looper layout are read");
        check (s.chains.size() == 3 && s.chains[0].presets.size() == 2 && s.chains[0].presets[1] == "TubeScreamer + Klon Boost" && s.chains[2].presets.isEmpty(),
               "song file: two lines for one chain stack its presets, a preset's name may hold a plus sign, and (empty) empties a chain");
        check (s.settings.size() == 2 && s.settings[0].setting == "drive" && s.settings[0].value == "7.5" && s.settings[1].setting == "on" && s.settings[1].value == "0",
               "song file: 'set' lines carry a value or on/off");
        check (s.hasSwitches && s.switches.size() == 3 && s.switches[1].sw == 9 && s.switches[1].action == "delay throw" && s.switches[2].chain.isEmpty(),
               "song file: switch lines are read, CTL3 by name, tap tempo without a chain");
        check (s.amps.size() == 1 && s.amps[0].models.size() == 2, "song file: the amp switch's list is read");
        check (s.notUnderstood.size() == 1, "song file: a line that means nothing is kept for the report, not dropped silently");

        const auto again = SongSounds::fromText (s.toText ("Wicked Game", chainNames));
        check (again.toText ("x") == s.toText ("x") && again.program == 11 && again.switches.size() == 3,
               "song file: what the app writes reads back the same");
        check (SongSounds::fromText ("switches = none\n").hasSwitches && ! SongSounds::fromText ("program = 3\n").saysAnything(),
               "song file: 'switches = none' takes every switch back for the looper; a file with only a program number names no sounds");

        check (chainFromWords ("chain 3", chainNames) == 2 && chainFromWords ("vocal", chainNames) == 2 && chainFromWords ("2", chainNames) == 1
                 && chainFromWords ("Chain 4", chainNames) == 3 && chainFromWords ("drums", chainNames) == -1 && chainFromWords ("chain 5", chainNames) == -1,
               "song file: a chain is found by its number or by its own name");
    }

    // ---- working a song out against presets and the default sounds ----
    std::map<juce::String, fx::ChainModel> presets;
    presets["Comp and EQ"]  = chainOf ({ slot ("comp"), slot ("eq") });
    presets["Screamer"]     = chainOf ({ slot ("ts", { { "drive", 4.0f } }), slot ("klon", {}, false) });
    presets["Clean amp"]    = chainOf ({ slot ("gate"), slot ("amp"), slot ("spring", { { "reverb", 2.4f } }), slot ("tremolo", {}, false) });
    presets["Vocal"]        = chainOf ({ slot ("peq"), slot ("harmony", { { "key", 0.0f } }, false), slot ("digitaldelay") });
    presets["Acoustic"]     = chainOf ({ slot ("body"), slot ("peq"), slot ("peq", {}, false) });
    const PresetLoader loadFake = [&presets] (const juce::String& name, fx::ChainModel& m)
    {
        const auto found = presets.find (name);
        if (found == presets.end())
            return false;
        m = found->second;
        return true;
    };
    std::array<fx::ChainModel, EffectsRack::numChains> now;
    now[3] = chainOf ({ slot ("bass9") });

    {
        const auto fallback = SongSounds::fromText ("chain 1 = Comp and EQ\nchain 1 = Clean amp\nacoustic = Acoustic\nvocal = Vocal\n"
                                                    "set = chain 1 / spring / reverb = 1\nset = vocal / digitaldelay / off\n"
                                                    "switch 9 = chain 1 / next amp\n");
        const auto song = SongSounds::fromText ("chain 1 = Comp and EQ\nchain 1 = Clean amp\nchain 1 = Screamer\n"
                                                "set = chain 1 / Spring reverb / Reverb = 3.5\nset = chain 1 / tremolo / on\n"
                                                "set = vocal / harmony / key = 5\nset = vocal / harmony / on\n"
                                                "set = acoustic / peq #2 / on\n"
                                                "switch 7 = chain 1 / klon on off\nswitch 8 = chain 1 / ts on off\nswitch CTL4 = vocal / delay throw\n"
                                                "amps = Electric / Dream 65 - Clean / Dream 65 - Driven\n");
        const auto r = resolveSounds (song, &fallback, chainNames, now, loadFake);
        juce::StringArray order;
        for (const auto& s : r.model[0].slots)
            order.add (s.type);

        check (order.joinIntoString (" ") == "comp eq gate ts klon amp spring tremolo",
               "songs: a song's presets stack in the order written, a drive preset landing in front of the amp (" + order.joinIntoString (" > ") + ")");
        check (r.problems.isEmpty(), "songs: a song that names real presets, effects and settings has no problems (" + r.problems.joinIntoString (" | ") + ")");
        check (r.from[0] == 1 && r.from[1] == 2 && r.from[2] == 2 && r.from[3] == 0 && ! r.has[3] && r.recipe[0].size() == 3,
               "songs: a chain the song names is its own, one it does not name takes the default sounds, one neither names is left alone");
        check (std::abs (r.model[0].slots[6].param ("reverb", 0.0f) - 3.5f) < 0.001f && r.model[0].slots[7].on,
               "songs: the song's own changes sit on top of its presets, by an effect's id or its name on screen");
        check (std::abs (r.model[2].slots[1].param ("key", 0.0f) - 5.0f) < 0.001f && r.model[2].slots[1].on && ! r.model[2].slots[2].on,
               "songs: a song can change one setting of a chain it takes from the default (the harmony's key), and the default's own changes stay");
        check (r.model[1].slots[2].on && ! presets["Acoustic"].slots[2].on, "songs: 'peq #2' means the second Channel EQ in the chain, and the preset itself is not touched");
        check (r.hasSwitches && r.switches.size() == 3 && r.switches[0].sw == 6 && r.switches[0].action == 14 && r.switches[1].action == 13
                 && r.switches[2].sw == 10 && r.switches[2].chain == 2 && r.switches[2].action == 5,
               "songs: a switch names an effect, and lands on the slot that effect ends up in once the presets are stacked");
        check (r.amps[0].size() == 2 && r.amps[0][1] == "Dream 65 - Driven", "songs: the amp switch's list goes to its chain");

        // The default alone, for a song with no file.
        const auto generic = resolveSounds ({}, &fallback, chainNames, now, loadFake);
        check (generic.from[0] == 2 && std::abs (generic.model[0].slots[4].param ("reverb", 0.0f) - 1.0f) < 0.001f
                 && generic.hasSwitches && generic.switches.size() == 1 && generic.switches[0].action == 7,
               "songs: a song with no file takes the default sounds, their own changes and their switches");

        // Edit a preset once and every song that names it follows.
        presets["Screamer"].slots[0].setParam ("drive", 8.0f);
        const auto afterEdit = resolveSounds (song, &fallback, chainNames, now, loadFake);
        check (std::abs (afterEdit.model[0].slots[3].param ("drive", 0.0f) - 8.0f) < 0.001f,
               "songs: a song names its presets rather than copying them, so an edited preset is what the song gets next time");

        // Problems are said, not swallowed.
        const auto bad = SongSounds::fromText ("chain 1 = No such preset\ndrums = Vocal\nset = vocal / flanger / mix = 3\nset = vocal / peq / sparkle = 3\n"
                                               "switch 3 = vocal / juggle\nswitch 4 = next amp\n");
        const auto br = resolveSounds (bad, &fallback, chainNames, now, loadFake);
        check (br.problems.size() == 6 && br.problems[0].contains ("No such preset") && br.problems[0].contains ("was not set by this song") && br.has[0] && br.from[0] == 2 && br.problems.joinIntoString (" ").contains ("no chain called \"drums\"")
                 && br.problems.joinIntoString (" ").contains ("juggle") && br.switches.empty(),
               "songs: a missing preset, an unknown chain, effect, setting or switch job each get a plain sentence (" + juce::String (br.problems.size()) + " problems)");
    }

    // ---- a chain as it stands, put back into words ----
    {
        auto built = presets["Comp and EQ"];
        built.addFrom (presets["Clean amp"], fx::Chain::maxSlots);
        const juce::StringArray recipe { "Comp and EQ", "Clean amp" };
        check (describeChain (built, recipe, "chain 1", loadFake).matches && describeChain (built, recipe, "chain 1", loadFake).changes.empty(),
               "songs: a chain just built from its presets is described as those presets with no changes");

        built.slots[4].setParam ("reverb", 6.25f);
        built.slots[5].on = true;
        built.slots[3].setText ("model", "Dream 65 - Driven");
        const auto d = describeChain (built, recipe, "chain 1", loadFake);
        check (d.matches && d.changes.size() == 3 && d.changes[0].effect == "amp" && d.changes[0].setting == "model"
                 && d.changes[1].effect == "spring" && d.changes[1].value == "6.25" && d.changes[2].setting == "on" && d.changes[2].value == "1",
               "songs: knobs turned since the presets were loaded come out as the song's own changes (" + juce::String (d.changes.size()) + ")");

        built.slots.erase (built.slots.begin());
        check (! describeChain (built, recipe, "chain 1", loadFake).matches && ! describeChain (built, {}, "chain 1", loadFake).matches,
               "songs: a chain rearranged by hand, or never built from presets, cannot be named by presets");
        check (slotWords (presets["Acoustic"], 2) == "peq #2" && slotFromWords (presets["Acoustic"], "Channel EQ #2") == 2 && slotFromWords (presets["Acoustic"], "wah") == -1,
               "songs: the second of two like effects is '#2', by id or by its name on screen");
    }

    // ---- the song book: the Song box, files in the songs folder, program changes, keeping sounds ----
    {
        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GigRig self-test songs");
        root.deleteRecursively();
        const auto songDir = root.getChildFile ("Songs"), presetDir = root.getChildFile ("Effect presets");
        songDir.createDirectory();
        presetDir.createDirectory();

        for (const auto& [name, model] : presets)
            presetDir.getChildFile (name + ".txt").replaceWithText (juce::String (model.toText()));

        AudioEngine engine;
        engine.openSimulated (rate, block, 16, 16);
        auto& rack = engine.effects();

        for (int c = 0; c < 3; ++c)
        {
            auto r = rack.routing (c);
            r.on = true;
            r.name = chainNames[c];
            rack.setRouting (c, r);
        }

        SongBook book (engine, songDir, presetDir);
        int changes = 0;
        book.onChanged = [&changes] { ++changes; };
        const int builtIns = numBuiltInSongs();
        check (book.numEntries() == builtIns && ! book.hasDefault() && book.entryForProgram (11) == book.entryNamed ("Wicked Game") && book.entryForProgram (11) >= 0,
               "song book: with an empty songs folder the list is the built-in looper layouts, found by their RC-600 memory numbers");

        // No files at all: loading a song leaves the effects alone.
        const auto handBuilt = chainOf ({ slot ("fuzz") });
        rack.chain (0).setModel (handBuilt);
        book.load (book.entryNamed ("Dreams"), "test");
        check (book.song().title() == "Dreams" && rack.chain (0).model().slots.size() == 1 && rack.chain (0).model().slots[0].type == "fuzz" && changes > 0
                 && book.lastNote().contains ("stay as they were"),
               "song book: a song with no file and no default sounds sets the looper and leaves the effects as they were");

        // The default sounds and one song's own file, written as Claude would send them.
        songDir.getChildFile ("Default sounds.txt").replaceWithText ("chain 1 = Comp and EQ\nchain 1 = Clean amp\nacoustic = Acoustic\nvocal = Vocal\nswitch 9 = chain 1 / next amp\n");
        songDir.getChildFile ("Wicked Game.txt").replaceWithText ("chain 1 = Comp and EQ\nchain 1 = Clean amp\nchain 1 = Screamer\nset = chain 1 / spring / reverb = 5\n"
                                                                  "switch 7 = chain 1 / ts on off\nswitch CTL3 = vocal / delay throw\n");
        songDir.getChildFile ("Under The Bridge.txt").replaceWithText ("program = 3\nlooper = Dirty Slap\nset = vocal / harmony / key = 4\n");
        check (book.rescanIfChanged() && ! book.rescanIfChanged() && book.hasDefault() && book.numEntries() == builtIns + 1,
               "song book: files put in the songs folder are picked up by themselves, and a file that is not a built-in song becomes a new entry");
        check (rack.chain (0).model().slots.size() == 6 && rack.recipe (0).size() == 2 && rack.pedalAssign (0).sw == 8 && rack.pedalAssign (0).action == 7,
               "song book: when the default sounds change, the loaded song (which uses them) is set again (" + juce::String ((int) rack.chain (0).model().slots.size()) + " effects)");

        book.load (book.entryNamed ("Wicked Game"), "test");
        const auto& wg = rack.chain (0).model();
        check (wg.slots.size() == 8 && wg.slots[3].type == "ts" && std::abs (wg.slots[6].param ("reverb", 0.0f) - 5.0f) < 0.001f
                 && rack.recipe (0).size() == 3 && rack.chain (2).model().slots.size() == 3 && book.lastProblems().isEmpty(),
               "song book: loading a song builds its chains from its presets and its own changes, and the rest from the default (" + book.lastProblems().joinIntoString (" | ") + ")");
        check (rack.pedalAssign (0).sw == 6 && rack.pedalAssign (0).action == 13 && rack.pedalAssign (1).sw == 9 && rack.pedalAssign (1).chain == 2 && rack.pedalAssign (1).action == 5
                 && rack.pedalAssign (2).sw < 0 && rack.takePedal (6, false) == rack.isEnabled(),
               "song book: the song's switches replace the ones before, and the rest are handed back to the looper");
        check (book.statusLine().contains ("its own sounds on chain 1") && book.statusLine().contains ("default sounds on chain 2, 3") && book.statusLine().contains ("program 11"),
               "song book: the screen says where each chain's sound came from (" + book.statusLine() + ")");

        // Program changes, as Songbook Pro sends them.
        book.programChange (3, "test");
        check (book.currentEntry().name == "Under The Bridge" && book.song().title() == "Under The Bridge" && book.song().bank[0][0] == selfTestSong ("Dirty Slap").bank[0][0]
                 && std::abs (rack.chain (2).model().slots[1].param ("key", 0.0f) - 4.0f) < 0.001f && rack.chain (0).model().slots.size() == 6
                 && rack.pedalAssign (0).action == 7,
               "song book: program 3 loads the entry that carries it, with the general looper layout, the default sounds and the song's one change");
        book.programChange (11, "test");
        check (book.currentEntry().name == "Wicked Game" && book.song().rcMemory == 11, "song book: program 11 loads Wicked Game, as the RC-600's memory 11 does");

        // A program no entry carries: the general set-up with the default sounds. Loops are only cleared when the song really changes.
        book.programChange (4, "test");
        const auto general = book.currentEntry().name;
        const int changesBefore = changes;
        rack.chain (0).setSlotOn (0, false);
        book.programChange (8, "test");
        check (general == SongBook::generalLayout && book.currentEntry().name == general && rack.chain (0).model().slots[0].on && changes > changesBefore
                 && book.reportText().contains ("already loaded") && book.reportText().contains ("no entry has that number"),
               "song book: a program number with no entry of its own loads the general set-up, and the same set-up again only sets the sounds afresh");
        book.programChange (0, "test");
        book.programChange (129, "test");
        check (book.currentEntry().name == general, "song book: program numbers outside 1 to 128 are ignored");

        // Keep: turn a knob, press Keep, and the file names the presets plus the one change.
        book.load (book.entryNamed ("Dreams"), "test");
        rack.chain (0).setParam (4, rack.chain (0).effect (4)->paramIndex ("reverb"), 7.0f);
        rack.setPedalAssign (1, { 4, 0, 10 + 4 });
        book.keepForCurrent();
        const auto dreams = songDir.getChildFile ("Dreams.txt").loadFileAsString().replace ("\r\n", "\n");
        check (dreams.contains ("chain 1 = Comp and EQ\nchain 1 = Clean amp\n") && dreams.contains ("set = chain 1 / spring / reverb = 7\n")
                 && dreams.contains ("switch 9 = chain 1 / next amp\n") && dreams.contains ("switch 5 = chain 1 / spring on off\n")
                 && ! dreams.contains ("\nchain 2 =") && ! dreams.contains ("\nchain 3 =") && ! dreams.contains ("\nchain 4 =") && ! dreams.contains ("program ="),
               "song book: Keep writes the presets a chain was built from plus what was changed since, and leaves out chains that match the default");
        book.load (book.entryNamed ("Wicked Game"), "test");
        book.load (book.entryNamed ("Dreams"), "test");
        check (std::abs (rack.chain (0).model().slots[4].param ("reverb", 0.0f) - 7.0f) < 0.001f && rack.pedalAssign (1).sw == 4 && rack.pedalAssign (1).action == 14
                 && book.currentEntry().hasFile && book.lastProblems().isEmpty(),
               "song book: a kept song comes back as it was kept");

        // Edit a preset once: the next load of every song that names it has the change.
        auto louder = presets["Comp and EQ"];
        louder.slots[0].setParam ("output", 3.0f);
        presetDir.getChildFile ("Comp and EQ.txt").replaceWithText (juce::String (louder.toText()));
        book.load (book.entryNamed ("Wicked Game"), "test");
        const float inWicked = rack.chain (0).model().slots[0].param ("output", 0.0f);
        book.load (book.entryNamed ("Dreams"), "test");
        check (std::abs (inWicked - 3.0f) < 0.001f && std::abs (rack.chain (0).model().slots[0].param ("output", 0.0f) - 3.0f) < 0.001f
                 && std::abs (rack.chain (0).model().slots[4].param ("reverb", 0.0f) - 7.0f) < 0.001f,
               "song book: a preset edited once is heard in every song that names it, and each song's own changes stay");

        // A chain rearranged by hand has no presets to name: Keep saves it as a preset of the song's own.
        auto byHand = rack.chain (1).model();
        byHand.slots.push_back (slot ("room"));
        rack.chain (1).setModel (byHand);
        const auto said = book.keepForCurrent();
        const auto ownPreset = presetDir.getChildFile ("Dreams - Acoustic.txt");
        check (ownPreset.existsAsFile() && songDir.getChildFile ("Dreams.txt").loadFileAsString().replace ("\r\n", "\n").contains ("chain 2 = Dreams - Acoustic\n") && said.contains ("new preset")
                 && rack.chain (1).model().slots.size() == 4 && rack.recipe (1).size() == 1,
               "song book: a chain that no longer matches its presets is kept as a new preset named after the song, and the screen says so");

        // Edit once for every song: turn a knob in a stacked chain and save it back into the preset it came from.
        book.load (book.entryNamed ("Wicked Game"), "test");
        rack.chain (0).setParam (3, rack.chain (0).effect (3)->paramIndex ("tone"), 9.0f);       // the Screamer's
        rack.chain (0).setParam (1, 0, rack.chain (0).effect (1)->getParam (0) + 2.0f);            // the EQ's first slider
        const auto owners = slotOwners (rack.recipe (0), book.loader());
        const auto saved = saveBackToPresets (rack, 0, presetDir);
        fx::ChainModel screamer, compEq, cleanAmp;
        book.loader() ("Screamer", screamer);
        book.loader() ("Comp and EQ", compEq);
        book.loader() ("Clean amp", cleanAmp);
        check (owners == std::vector<int> ({ 0, 0, 1, 2, 2, 1, 1, 1 }) && saved.contains ("\"Screamer\"") && saved.contains ("\"Comp and EQ\"") && ! saved.contains ("Clean amp")
                 && screamer.slots.size() == 2 && std::abs (screamer.slots[0].param ("tone", 0.0f) - 9.0f) < 0.001f && compEq.slots.size() == 2
                 && cleanAmp.slots.size() == 4 && std::abs (cleanAmp.slots[2].param ("reverb", 0.0f) - 2.4f) < 0.001f,
               "song book: Update its presets saves only the knobs turned since the song loaded, each into its own preset; the song's own change (reverb 5) stays out of the preset (" + saved + ")");
        check (saveBackToPresets (rack, 0, presetDir).contains ("nothing to save"), "song book: saving back again finds nothing new to save");
        EffectsRack reopened;
        reopened.loadStateText (rack.stateText());
        check (reopened.baseline (0).toText() == rack.baseline (0).toText() && reopened.baseline (0).slots.size() == 8,
               "song book: the chain as it was loaded is remembered across a restart, so Update its presets still knows what was changed");
        book.load (book.entryNamed ("Dreams"), "test");

        // Forget, and a new entry copied from the loaded one.
        book.forgetCurrent();
        check (! songDir.getChildFile ("Dreams.txt").existsAsFile() && ! book.currentEntry().hasFile && rack.chain (1).model().slots.size() == 3
                 && std::abs (rack.chain (0).model().slots[4].param ("reverb", 0.0f) - 2.4f) < 0.001f,
               "song book: Forget removes a song's own sounds and it follows the default again");
        check (book.addEntry ("Wicked Game", 50).isNotEmpty() && book.addEntry ("New One", 11).contains ("Wicked Game") && book.addEntry ("", 0).isNotEmpty()
                 && book.addEntry ("New One", 200).isNotEmpty(),
               "song book: a new entry needs a name of its own and a program number nothing else has");
        rack.chain (2).setParam (1, rack.chain (2).effect (1)->paramIndex ("key"), 9.0f);
        const auto error = book.addEntry ("Hallelujah", 7);
        check (error.isEmpty() && book.currentEntry().name == "Hallelujah" && book.entryForProgram (7) == book.currentIndex()
                 && songDir.getChildFile ("Hallelujah.txt").loadFileAsString().replace ("\r\n", "\n").contains ("looper = Dreams\n")
                 && songDir.getChildFile ("Hallelujah.txt").loadFileAsString().replace ("\r\n", "\n").contains ("set = chain 3 / harmony / key = 9\n")
                 && book.song().bank[0][0] == selfTestSong ("Dreams").bank[0][0],
               "song book: a new entry takes the loaded song's looper layout and the sounds as they stand (" + error + ")");

        // Keep as the default.
        book.load (book.entryNamed ("Yellow"), "test");
        rack.chain (0).setParam (4, rack.chain (0).effect (4)->paramIndex ("decay"), 8.0f);
        book.keepAsDefault();
        book.load (book.entryNamed ("Cocaine"), "test");
        check (std::abs (rack.chain (0).model().slots[4].param ("decay", 0.0f) - 8.0f) < 0.001f
                 && songDir.getChildFile ("Default sounds.txt").loadFileAsString().replace ("\r\n", "\n").contains ("set = chain 1 / spring / decay = 8\n"),
               "song book: Keep as the default changes what every song without its own lines gets");

        // A song file with a mistake says so in the report.
        songDir.getChildFile ("Cocaine.txt").replaceWithText ("chain 1 = Not there\nwhat is this\n");
        book.rescanIfChanged();
        const auto report = book.reportText();
        check (report.contains ("PROBLEM: The preset \"Not there\"") && report.contains ("Line not understood") && report.contains ("Song loaded: Cocaine (program 14)")
                 && report.contains ("Program numbers:") && report.contains ("7 Hallelujah"),
               "song book: the report names the song, its program number, and each problem in its file");

        // The app's start: the looper layout only, the effects as the rack remembers them.
        const auto effectsBefore = rack.chain (0).model().toText();
        songDir.getChildFile ("Cocaine.txt").deleteFile();
        book.rescan();
        book.load (book.entryNamed ("Wicked Game"), "the app started", false);
        check (rack.chain (0).model().toText() == effectsBefore && book.song().title() == "Wicked Game",
               "song book: at the app's start the first song sets the looper only; the effects come back from the app's own memory");

        // The rack remembers which presets a chain is made of.
        EffectsRack other;
        other.loadStateText (rack.stateText());
        check (other.recipe (0) == rack.recipe (0) && ! rack.recipe (0).isEmpty() && other.pedalAssign (0).action == rack.pedalAssign (0).action
                 && other.tailsRingOut() && rack.stateText().contains ("rack.tails=1"),
               "song book: the presets a chain was built from, all six switches and the ring-out setting are saved with the rack");

        root.deleteRecursively();
    }

    // ---- the amp-cycling switch ----
    {
        AudioEngine engine;
        engine.openSimulated (rate, block, 16, 16);
        auto& rack = engine.effects();
        rack.setEnabled (true);
        const auto installed = ampmodels::names (juce::File (juce::String (fx::NamAmp::modelFolder())));
        rack.chain (0).setModel (chainOf ({ slot ("ts"), slot ("amp") }));
        const auto modelNow = [&rack] { return juce::String (rack.chain (0).model().slots[1].text ("model")); };
        juce::StringArray round;
        rack.chain (0).setText (1, "model", installed[0].toStdString());

        for (int i = 0; i < installed.size(); ++i)
        {
            rack.nextAmp (0);
            round.add (modelNow());
        }

        check (installed.size() == 3 && round[0] == installed[1] && round[1] == installed[2] && round[2] == installed[0] && rack.ampNote.contains (installed[0]),
               "amp switch: each press moves to the next amp model and comes round to the first (" + round.joinIntoString (" > ") + ")");

        rack.setAmpCycle (0, { installed[0], installed[2] });
        rack.nextAmp (0);
        const auto first = modelNow();
        rack.nextAmp (0);
        check (first == installed[2] && modelNow() == installed[0], "amp switch: a song's own list of amps is the round it steps through");

        rack.setPedalAssign (0, { 8, 0, 7 });
        rack.doPedalAction (rack.pedalAssign (0));
        check (modelNow() == installed[2] && rack.pedalText().contains ("next amp sound") && rack.takePedal (8, false),
               "amp switch: it works from a pedal switch, and the report says which switch");
        check (rack.nextAmp (1).contains ("no amp"), "amp switch: a chain with no amp says so and changes nothing");

        // Stepping amps while playing does not click: the amp fades from one model to the other.
        auto r = rack.routing (0);
        r.on = true;
        r.source = 9;
        r.out = { 2, true };
        rack.setRouting (0, r);
        std::vector<std::vector<float>> ins (16, std::vector<float> ((size_t) block, 0.0f)), outs (16, std::vector<float> ((size_t) block, 0.0f));
        std::vector<const float*> inPtrs;
        std::vector<float*> outPtrs;
        for (auto& v : ins)  inPtrs.push_back (v.data());
        for (auto& v : outs) outPtrs.push_back (v.data());
        juce::int64 t = 0;
        const int o = returnToOutput (2, 16);
        const auto play = [&] (int blocks)
        {
            std::vector<float> heard;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < block; ++i, ++t)
                    ins[9][(size_t) i] = (float) (0.1 * std::sin (twoPi * 220.0 * (double) t / rate));
                engine.processBlock (inPtrs.data(), 16, outPtrs.data(), 16, block);
                heard.insert (heard.end(), outs[(size_t) o].begin(), outs[(size_t) o].end());
            }
            return heard;
        };

        play (1500);
        const auto steady = play (750);
        rack.nextAmp (0);
        const auto stepping = play (750);
        check (peak (stepping) > 0.001f && biggestStep (stepping) < biggestStep (steady) * 3.0f + 0.01f,
               "amp switch: stepping to the next amp while playing does not click (biggest step " + juce::String (biggestStep (stepping), 4)
               + " against " + juce::String (biggestStep (steady), 4) + ")");
    }

    // ---- a song change through the whole rack: the old chain's delay rings out on the return ----
    {
        AudioEngine engine;
        engine.openSimulated (rate, block, 16, 16);
        auto& rack = engine.effects();
        rack.setEnabled (true);
        auto r = rack.routing (0);
        r.on = true;
        r.source = 9;
        r.out = { 2, true };
        rack.setRouting (0, r);
        rack.chain (0).setModel (delayChain());
        std::vector<std::vector<float>> ins (16, std::vector<float> ((size_t) block, 0.0f)), outs (16, std::vector<float> ((size_t) block, 0.0f));
        std::vector<const float*> inPtrs;
        std::vector<float*> outPtrs;
        for (auto& v : ins)  inPtrs.push_back (v.data());
        for (auto& v : outs) outPtrs.push_back (v.data());
        juce::int64 t = 0;
        const int o = returnToOutput (2, 16);
        const auto play = [&] (double seconds, bool playing)
        {
            std::vector<float> heard;
            for (int b = 0; b < (int) (seconds * rate / block); ++b)
            {
                for (int i = 0; i < block; ++i, ++t)
                    ins[9][(size_t) i] = playing ? (float) (0.2 * std::sin (twoPi * 330.0 * (double) t / rate)) : 0.0f;
                engine.processBlock (inPtrs.data(), 16, outPtrs.data(), 16, block);
                heard.insert (heard.end(), outs[(size_t) o].begin(), outs[(size_t) o].end());
                if (b % 150 == 149)
                    rack.chain (0).collectGarbage();    // not rack.tick(): that also saves the rack to the app's own memory file
            }
            return heard;
        };

        play (1.0, true);
        rack.chain (0).setModelGently (plainChain());
        play (0.05, true);
        const int ringing = rack.tailsRinging();
        const auto tail = play (1.0, false);
        check (ringing == 1 && peak (tail, 0.2, 0.5) > 0.01f && rack.reportText().contains ("delays and reverbs ring out"),
               "tails: through the whole rack, the last song's delay is still heard on the return after the song changes (" + db (peak (tail, 0.2, 0.5)) + ")");
    }

    // ---- the watch that opens the mixer again after it goes away ----
    {
        ReconnectWatch w;
        using Action = ReconnectWatch::Action;
        juce::int64 blocks = 0;
        bool quietSoFar = true;

        for (int i = 0; i < 200; ++i)
            quietSoFar = quietSoFar && w.tick (false, blocks, true, false) == Action::none;
        check (quietSoFar && ! w.isLost(), "reconnect: nothing is watched until a device has been started on purpose");

        w.arm (true);
        for (int i = 0; i < 400; ++i)
            quietSoFar = quietSoFar && w.tick (true, blocks += 75, false, false) == Action::none;
        check (quietSoFar && w.timesLost() == 0 && w.text().contains ("Not lost"), "reconnect: a device that keeps running is left alone");

        // The audio stops arriving (the mixer was switched off).
        int lostAt = -1;
        for (int i = 0; i < 60 && lostAt < 0; ++i)
            if (w.tick (true, blocks, false, false) == Action::lost)
                lostAt = i;
        check (lostAt == ReconnectWatch::stallTicks - 1 && w.isLost() && w.timesLost() == 1,
               "reconnect: no audio for a second and a half counts as the mixer gone (noticed after " + juce::String ((lostAt + 1) / 20.0, 2) + " s)");

        // Then one try a second later, and one every three seconds while it stays away.
        juce::Array<int> tries;
        for (int i = 1; i <= 200; ++i)
            if (w.tick (false, blocks, false, false) == Action::tryOpen)
            {
                tries.add (i);
                w.result (false);
            }
        check (tries.size() == 4 && tries[0] == 20 && tries[1] == 80 && tries[2] == 140 && w.isLost() && w.text().contains ("STILL GONE"),
               "reconnect: it tries again a second after the loss, then every three seconds, for as long as it takes");

        int more = 0;
        while (w.tick (false, blocks, false, false) != Action::tryOpen && more < 100)
            ++more;
        w.result (true);
        check (! w.isLost() && w.timesBack() == 1 && w.text().contains ("Back after") && w.text().contains ("opened again by the app"),
               "reconnect: once the mixer opens it stops trying, and the report says how long it was away (" + w.text() + ")");

        for (int i = 0; i < 100; ++i)
            quietSoFar = quietSoFar && w.tick (true, blocks += 75, false, false) == Action::none;
        check (quietSoFar && w.tick (true, blocks += 75, true, false) == Action::lost && w.timesLost() == 2, "reconnect: an error from the driver counts as gone at once");

        ReconnectWatch busy;
        busy.arm (true);
        bool calm = true;
        for (int i = 0; i < 100; ++i)
            calm = calm && busy.tick (false, 0, false, true) == Action::none;
        check (calm && busy.tick (true, 5, false, false) == Action::none, "reconnect: a latency measurement, which reopens the device itself, is not taken for a loss");

        ReconnectWatch closed;
        closed.arm (true);
        closed.tick (true, 10, false, false);
        closed.arm (false);
        for (int i = 0; i < 100; ++i)
            calm = calm && closed.tick (false, 10, false, false) == Action::none;
        check (calm, "reconnect: a device closed on purpose (the device check) is not taken for a loss");
    }
}

} // namespace gigrig
