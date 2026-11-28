#include "RemoteServer.h"
#include "LooperLayout.h"
#include "BinaryData.h"
#include "effects/Registry.h"

namespace gigrig
{

/** The checks for the remote screen (docs/remote.md): a song's looper layout as words and as
    numbers, what the page can ask the rig to do, and the little server that hands the page out.
    No audio device and no real network card are needed: the engine is the pretend one, and the
    one socket check talks to this computer itself. */
void runRemoteSelfTest (const std::function<void (bool, const juce::String&)>& check)
{
    // ---- a looper layout as lines in a song file ----
    {
        int wordsOk = 0, numbersOk = 0, saidInWords = 0, total = 0;
        juce::String firstBad;

        for (int i = 0; i < numBuiltInSongs(); ++i)
        {
            const auto& song = builtInSong (i);
            const auto lines = layout::toLines (song);

            SongSetup back;
            juce::StringArray problems;
            back.assign[0] = decodeRcAssign (13, 1);        // something to be cleared, so "looper links = none" is proven too
            layout::fromLines (lines, back, problems);
            bool same = problems.isEmpty() && layout::toLines (back) == lines && back.beatsPerBar == song.beatsPerBar
                        && back.recThenPlay == song.recThenPlay && back.quantise == song.quantise;

            for (int t = 0; t < numLoopTracks && same; ++t)
                same = back.track[(size_t) t].rcInputs == song.track[(size_t) t].rcInputs && back.track[(size_t) t].bars == song.track[(size_t) t].bars
                       && back.track[(size_t) t].stopMode == song.track[(size_t) t].stopMode && back.track[(size_t) t].returnMode == song.track[(size_t) t].returnMode
                       && back.track[(size_t) t].level == song.track[(size_t) t].level;

            for (int b = 0; b < numLoopBanks && same; ++b)
                for (int sw = 0; sw < 9 && same; ++sw)
                {
                    same = layout::functionWords (back.bank[b][sw]) == layout::functionWords (song.bank[b][sw]);
                    ++total;
                    saidInWords += layout::functionWords (song.bank[b][sw]).startsWith ("rc ") ? 0 : 1;
                }

            wordsOk += same ? 1 : 0;
            if (! same && firstBad.isEmpty())
                firstBad = song.title() + (problems.isEmpty() ? juce::String() : ": " + problems[0]);

            SongSetup viaJson;
            numbersOk += layout::fromJson (layout::toJson (song), viaJson).isEmpty() && layout::toLines (viaJson) == lines ? 1 : 0;
        }

        check (wordsOk == numBuiltInSongs() && numBuiltInSongs() > 0,
               "looper layout: every built-in song written as song-file lines reads back the same (" + juce::String (wordsOk) + " of " + juce::String (numBuiltInSongs())
               + (firstBad.isNotEmpty() ? ", first wrong: " + firstBad : juce::String()) + ")");
        check (numbersOk == numBuiltInSongs(),
               "looper layout: every built-in song sent to the remote screen as numbers comes back the same (" + juce::String (numbersOk) + " of " + juce::String (numBuiltInSongs()) + ")");
        check (saidInWords * 10 >= total * 9,
               "looper layout: the words cover the switch functions Tom's songs use (" + juce::String (saidInWords) + " of " + juce::String (total) + " switches, the rest kept as RC-600 numbers)");

        // The words themselves are saved on Tom's disk: these exact lines must always mean this.
        const juce::StringArray lines { "looper beats = 3",
                                        "looper order = record overdub play",
                                        "looper new track = starts at once",
                                        "looper track 1 = acoustic + stomp / free / in place / stops at once / level 100",
                                        "looper track 2 = bass / 4 bars / from its first bar / stops at the end of its loop / level 96",
                                        "looper track 3 = vocal / 2 bars / from its first note / fades out / level 120",
                                        "looper switch 1.1 = track 1 / record play, hold undo",
                                        "looper switch 1.2 = track 2 / play stop",
                                        "looper switch 1.3 = current track / undo redo",
                                        "looper switch 1.9 = all start stop",
                                        "looper switch 2.7 = bank",
                                        "looper switch 2.8 = rc 201",
                                        "looper link = switch 1.3 / track 1 / play stop",
                                        "looper link = switch CTL3 / all start stop",
                                        "looper link = track 1 plays / track 3 / record play",
                                        "looper switch 3.1 = track 1 / stop",
                                        "looper track 2 = bass / as long as a piece of string",
                                        "looper switch 1.4 = track 9 / stop" };
        SongSetup s;
        juce::StringArray problems;
        const bool used = layout::fromLines (lines, s, problems);
        const auto& t = s.track;
        check (used && s.beatsPerBar == 3 && ! s.recThenPlay && ! s.quantise
                 && t[0].rcInputs == (rcInst1L | rcInst1R) && t[0].bars == 0 && t[0].returnMode == ReturnMode::inPlace && t[0].stopMode == StopMode::immediate && t[0].level == 100
                 && t[1].rcInputs == rcInst2L && t[1].bars == 4 && t[1].returnMode == ReturnMode::fromBarLine && t[1].stopMode == StopMode::loopEnd && t[1].level == 96
                 && t[2].rcInputs == rcMic1 && t[2].bars == 2 && t[2].returnMode == ReturnMode::atOnce && t[2].stopMode == StopMode::fade && t[2].level == 120,
               "looper layout: the saved words for beats, order, sources, length, how a track comes back and stops, and level mean what they always did");
        check (s.bank[0][0] == 2 && s.bank[0][1] == 218 && s.bank[0][2] == 157 + 14 && s.bank[0][8] == 186 && s.bank[1][6] == 193 && s.bank[1][7] == 201,
               "looper layout: the saved words for switches give the RC-600 function numbers the looper already knows (and 'rc 201' keeps a number the words cannot say)");
        check (s.assign[0].rcSource == 15 && s.assign[0].rcTarget == 1 && s.assign[0].source == LoopAssign::Source::pedal && s.assign[0].action == LoopAction::playStop
                 && s.assign[1].rcSource == 42 && s.assign[1].action == LoopAction::allStartStop
                 && s.assign[2].source == LoopAssign::Source::trackPlayStop && s.assign[2].sourceTrack == 0 && s.assign[2].targetTrack == 2 && s.assign[2].action == LoopAction::recPlay
                 && s.assign[3].source == LoopAssign::Source::off,
               "looper layout: the saved words for links give a switch link, a CTL link and a track link");
        check (problems.size() == 3 && t[1].bars == 4 && s.bank[0][3] == 0,
               "looper layout: lines it cannot use are reported and change nothing (" + juce::String (problems.size()) + " reported: a bank 3, a nonsense length, a track 9)");

        int code = -1;
        check (layout::functionFromWords ("Track 2 / Record Play,  hold undo", code) && code == 28 && ! layout::functionFromWords ("track 2 / moonwalk", code)
                 && layout::functionWords (0) == "nothing" && layout::sourceWords (rcMic1 | rcInst2R) == "vocal + electric",
               "looper layout: capital letters and spacing do not matter, and words it does not know are refused");
    }

    // ---- what the page can see and do, against the pretend engine and folders of its own ----
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GigRig self-test remote");
    root.deleteRecursively();
    const auto songDir = root.getChildFile ("Songs"), presetDir = root.getChildFile ("Effect presets");
    songDir.createDirectory();
    presetDir.createDirectory();

    {
        AudioEngine engine;
        engine.openSimulated (96000.0, 64, 16, 16);
        engine.mixer().setPresetRoot (root.getChildFile ("Mixer"));
        PedalInput pedal;
        SongBook book (engine, songDir, presetDir);
        RemoteApi api (engine, pedal, book);
        int told = 0;
        api.onRigChanged = [&told] { ++told; };

        const auto cmd = [&api] (const juce::String& json) { return api.command (juce::JSON::parse (json)); };
        const auto fine = [] (const juce::var& r) { return (bool) r.getProperty ("ok", false); };

        const auto state = api.state();
        const auto live = api.live();
        const auto has = [] (const juce::var& v, std::initializer_list<const char*> keys)
        {
            for (const auto* k : keys)
                if (! v.hasProperty (k))
                    return false;
            return true;
        };

        check (has (state, { "build", "rig", "songs", "current", "layout", "functions", "sources", "switches", "effects", "mixer", "types", "maxLanes", "maxLinks" })
                 && state["songs"].size() == book.numEntries() && state["layout"]["tracks"].size() == numLoopTracks && state["layout"]["banks"][1].size() == 9
                 && state["mixer"]["channels"].size() == numMixChannels && state["effects"]["chains"].size() > 0 && state["functions"].size() == (int) layout::functionChoices().size(),
               "remote: the state the page is given has the songs, the looper layout, the effects chains and the mixer");
        check (has (live, { "looper", "load", "late", "open" }) && live["looper"]["tracks"].size() == numLoopTracks && ! live.hasProperty ("mix"),
               "remote: the live part has the six tracks, and no mixer meters while the mixer is off");

        const int wicked = book.entryNamed ("Wicked Game");
        auto r = cmd ("{\"op\":\"song.load\",\"index\":" + juce::String (wicked) + "}");
        check (fine (r) && book.currentIndex() == wicked && told == 1 && api.commandsDone() == 1 && ! fine (cmd ("{\"op\":\"song.load\",\"index\":999}")),
               "remote: the page loads a song by its place in the list, and a place that is not there is refused");

        // Build: take the loaded layout, change it, try it, save it, find it in the file, read it back.
        r = cmd ("{\"op\":\"layout.of\",\"index\":" + juce::String (wicked) + "}");
        auto wanted = r["layout"].clone();
        const auto before = layout::toLines (book.song());
        wanted["tracks"][1].getDynamicObject()->setProperty ("bars", 4);
        wanted["tracks"][1].getDynamicObject()->setProperty ("inputs", rcInst2L);
        wanted["banks"][0].getArray()->set (4, 189);
        wanted.getDynamicObject()->setProperty ("beats", 3);

        auto ask = new juce::DynamicObject();
        ask->setProperty ("op", "layout.try");
        ask->setProperty ("layout", wanted);
        juce::var askVar (ask);
        r = api.command (askVar);
        check (fine (r) && book.layoutIsTried() && book.song().track[1].bars == 4 && book.song().bank[0][4] == 189 && book.song().beatsPerBar == 3
                 && book.currentEntry().sounds.layoutLines.isEmpty() && songDir.getNumberOfChildFiles (juce::File::findFiles) == 0,
               "remote: trying a layout from the builder puts it on the looper and saves nothing");

        r = cmd ("{\"op\":\"layout.forget\"}");
        check (fine (r) && ! book.layoutIsTried() && layout::toLines (book.song()) == before,
               "remote: stopping a try puts the song's saved layout back");

        ask->setProperty ("op", "layout.save");
        r = api.command (askVar);
        const auto file = songDir.getChildFile ("Wicked Game.txt");
        const auto text = file.loadFileAsString();
        check (fine (r) && text.contains ("looper track 2 = bass / 4 bars") && text.contains ("looper switch 1.5 = clear all") && text.contains ("looper beats = 3")
                 && ! book.currentEntry().sounds.layoutLines.isEmpty() && book.currentEntry().sounds.notUnderstood.isEmpty(),
               "remote: saving a layout writes it into the song's file as 'looper' lines");

        {
            AudioEngine engine2;
            engine2.openSimulated (96000.0, 64, 16, 16);
            SongBook again (engine2, songDir, presetDir);
            const int i = again.entryNamed ("Wicked Game");
            again.load (i, "the self-test");
            check (i >= 0 && again.song().track[1].bars == 4 && again.song().bank[0][4] == 189 && again.song().beatsPerBar == 3
                     && again.entryForProgram (11) == i && again.lastProblems().isEmpty(),
                   "remote: a fresh start reads the saved layout back from the song's file, and the song keeps its program number");
        }

        ask->setProperty ("op", "layout.saveAs");
        ask->setProperty ("name", "Remote test song");
        ask->setProperty ("program", 77);
        r = api.command (askVar);
        check (fine (r) && book.entryNamed ("Remote test song") == book.currentIndex() && book.entryForProgram (77) == book.currentIndex()
                 && book.song().bank[0][4] == 189 && songDir.getChildFile ("Remote test song.txt").existsAsFile(),
               "remote: saving a layout as a new song adds the song with its program number and loads it");

        const int back = book.entryNamed ("Wicked Game");
        cmd ("{\"op\":\"song.load\",\"index\":" + juce::String (back) + "}");
        r = cmd ("{\"op\":\"layout.forget\"}");
        check (fine (r) && layout::toLines (book.song()) == before && ! file.loadFileAsString().contains ("looper "),
               "remote: taking a song's own layout out of its file puts the built-in one back");

        // The mixer and the effects, by the same numbers the page sends.
        auto& mixer = engine.mixer();
        const bool wasOff = ! mixer.isOn();
        r = cmd ("{\"op\":\"mix.ch\",\"c\":1,\"key\":\"fader\",\"value\":-6.5}");
        const bool faderSet = fine (r) && std::abs (mixer.setup().channel[1].faderDb + 6.5f) < 0.01f;
        cmd ("{\"op\":\"mix.ch\",\"c\":1,\"key\":\"fader\",\"value\":99}");
        const bool capped = std::abs (mixer.setup().channel[1].faderDb - 10.0f) < 0.01f;
        cmd ("{\"op\":\"mix.ch\",\"c\":0,\"key\":\"fx1\",\"value\":-12}");
        cmd ("{\"op\":\"mix.out\",\"o\":0,\"key\":\"level\",\"value\":-3}");
        cmd ("{\"op\":\"mix.fx\",\"f\":2,\"key\":\"ret\",\"value\":-9}");
        check (wasOff && ! mixer.isOn() && faderSet && capped && std::abs (mixer.setup().channel[0].fxDb[1] + 12.0f) < 0.01f
                 && std::abs (mixer.setup().output[0].levelDb + 3.0f) < 0.01f && std::abs (mixer.setup().fx[2].returnDb + 9.0f) < 0.01f
                 && ! fine (cmd ("{\"op\":\"mix.ch\",\"c\":40,\"key\":\"fader\",\"value\":0}")) && ! fine (cmd ("{\"op\":\"mix.ch\",\"c\":0,\"key\":\"volume\",\"value\":0}")),
               "remote: faders, sends, effect returns and the output level are set from the page, kept inside their limits, and moving them does not switch the mixer on");

        cmd ("{\"op\":\"mix.on\",\"on\":true}");
        const bool nowOn = mixer.isOn() && api.live().hasProperty ("mix");
        cmd ("{\"op\":\"mix.on\",\"on\":false}");
        check (nowOn && ! mixer.isOn(), "remote: the page switches the mixer on and off, and the meters come only while it is on");

        check (! fine (cmd ("{\"op\":\"fx.param\",\"chain\":50,\"slot\":0,\"id\":\"drive\",\"value\":3}")) && ! fine (cmd ("{\"op\":\"self.destruct\"}")) && ! fine (cmd ("[1,2,3]"))
                 && cmd ("{\"op\":\"self.destruct\"}")["error"].toString().contains ("self.destruct"),
               "remote: a chain that is not there, an order this build does not know, and nonsense are all refused with a reason");


        // ---- phase 2 (27 Nov 2026): the row of effects, presets, mixer set-ups, break music, the knob, the tuner ----
        {
            auto& rack = engine.effects();
            const auto slotsOf = [&rack] { return rack.chain (0).model().slots; };
            const auto before0 = slotsOf();
            const auto st = api.state();
            const auto firstType = st["fxCatalogue"][0]["id"].toString();
            check (st["fxCatalogue"].size() == (int) fx::effectTypes().size() && (int) st["fxMaxSlots"] == fx::Chain::maxSlots && firstType.isNotEmpty()
                     && st["fxCatalogue"][0].hasProperty ("name") && st["fxCatalogue"][0].hasProperty ("group"),
                   "remote: the state lists every effect that can be added and how many a chain holds");

            auto added = cmd ("{\"op\":\"fx.add\",\"chain\":0,\"type\":\"boost\",\"at\":0}");
            auto afterAdd = slotsOf();
            const bool addedFirst = fine (added) && afterAdd.size() == before0.size() + 1 && afterAdd[0].type == "boost" && afterAdd[0].on;
            cmd ("{\"op\":\"fx.add\",\"chain\":0,\"type\":\"tremolo\"}");
            afterAdd = slotsOf();
            check (addedFirst && afterAdd.size() == before0.size() + 2 && afterAdd.back().type == "tremolo" && ! fine (cmd ("{\"op\":\"fx.add\",\"chain\":0,\"type\":\"flux capacitor\"}")),
                   "remote: an effect is added where the page says (or at the end), switched on, and a type that does not exist is refused");

            // Its settings travel with it when it moves.
            struct First { juce::String id; float min = 0.0f, max = 1.0f; };
            const auto firstParam = [&api] (const std::string& type)
            {
                const auto p = api.state()["types"][juce::Identifier (juce::String (type))]["params"][0];
                return First { p["id"].toString(), (float) (double) p["min"], (float) (double) p["max"] };
            };
            const auto boostParam = firstParam ("boost");
            cmd ("{\"op\":\"fx.param\",\"chain\":0,\"slot\":0,\"id\":\"" + boostParam.id + "\",\"value\":" + juce::String (boostParam.max) + "}");
            const int last = (int) slotsOf().size() - 1;
            auto moved = cmd ("{\"op\":\"fx.move\",\"chain\":0,\"slot\":0,\"to\":" + juce::String (last) + "}");
            auto afterMove = slotsOf();
            const bool movedOk = fine (moved) && afterMove.back().type == "boost" && afterMove[(size_t) last - 1].type == "tremolo"
                                 && std::abs (afterMove.back().param (boostParam.id.toStdString(), -999.0f) - boostParam.max) < 1.0e-4f;
            check (movedOk && ! fine (cmd ("{\"op\":\"fx.move\",\"chain\":0,\"slot\":0,\"to\":99}")) && ! fine (cmd ("{\"op\":\"fx.move\",\"chain\":0,\"slot\":99,\"to\":0}")),
                   "remote: an effect moves along the chain with its settings, and a move off the end is refused");

            auto removed = cmd ("{\"op\":\"fx.remove\",\"chain\":0,\"slot\":" + juce::String (last) + "}");
            check (fine (removed) && slotsOf().size() == before0.size() + 1 && slotsOf().back().type == "tremolo" && ! fine (cmd ("{\"op\":\"fx.remove\",\"chain\":0,\"slot\":99}")),
                   "remote: an effect is removed, and one that is not there is refused");

            for (int i = 0; i < fx::Chain::maxSlots + 2; ++i)
                cmd ("{\"op\":\"fx.add\",\"chain\":0,\"type\":\"boost\"}");
            check ((int) slotsOf().size() == fx::Chain::maxSlots && cmd ("{\"op\":\"fx.add\",\"chain\":0,\"type\":\"boost\"}")["error"].toString().contains ("full"),
                   "remote: a full chain refuses another effect and says why");
            while ((int) slotsOf().size() > 2)
                cmd ("{\"op\":\"fx.remove\",\"chain\":0,\"slot\":0}");

            auto saved = cmd ("{\"op\":\"fx.save\",\"chain\":0,\"name\":\"Remote test sound\"}");
            const auto presetFile = presetDir.getChildFile ("Remote test sound.txt");
            const bool savedOk = fine (saved) && presetFile.existsAsFile() && presetFile.loadFileAsString().contains ("boost") && ! fine (cmd ("{\"op\":\"fx.save\",\"chain\":0,\"name\":\"  \"}"));
            cmd ("{\"op\":\"fx.remove\",\"chain\":0,\"slot\":0}");
            const bool gone = slotsOf().size() == 1;
            auto loaded = cmd ("{\"op\":\"fx.preset\",\"chain\":0,\"name\":\"Remote test sound\"}");
            check (savedOk && gone && fine (loaded) && slotsOf().size() == 2 && juce::JSON::toString (api.state()["effects"]["presets"]).contains ("Remote test sound"),
                   "remote: a chain is saved as a preset under a typed name, a blank name is refused, and the preset loads back ("
                   + juce::String (savedOk ? "" : "not saved: " + saved["error"].toString() + " ") + (gone ? "" : "not removed ") + (fine (loaded) ? "" : "not loaded ") + juce::String ((int) slotsOf().size()) + " effects)");

            // Update its presets: a change made on the page goes back into the preset the chain was made from.
            const auto p0 = firstParam (slotsOf()[0].type);
            cmd ("{\"op\":\"fx.param\",\"chain\":0,\"slot\":0,\"id\":\"" + p0.id + "\",\"value\":" + juce::String (p0.min) + "}");
            cmd ("{\"op\":\"fx.param\",\"chain\":0,\"slot\":1,\"id\":\"" + firstParam (slotsOf()[1].type).id + "\",\"value\":" + juce::String (firstParam (slotsOf()[1].type).max) + "}");
            const auto textBefore = presetFile.loadFileAsString();
            auto updated = cmd ("{\"op\":\"fx.update\",\"chain\":0}");
            check (fine (updated) && presetFile.loadFileAsString() != textBefore, "remote: 'update its presets' writes the chain's changes back into the preset it was made from (" + updated["note"].toString() + ")");
        }

        {
            cmd ("{\"op\":\"mix.ch\",\"c\":3,\"key\":\"fader\",\"value\":-7.5}");
            auto a = cmd ("{\"op\":\"mix.save\",\"kind\":\"act\",\"name\":\"Remote act\"}");
            auto v = cmd ("{\"op\":\"mix.save\",\"kind\":\"venue\",\"name\":\"Remote venue\"}");
            auto m = cmd ("{\"op\":\"mix.save\",\"kind\":\"monitors\",\"name\":\"Remote monitors\"}");
            cmd ("{\"op\":\"mix.ch\",\"c\":3,\"key\":\"fader\",\"value\":0}");
            auto back = cmd ("{\"op\":\"mix.preset\",\"kind\":\"act\",\"name\":\"Remote act\"}");
            const auto st = api.state();
            check (fine (a) && fine (v) && fine (m) && fine (back) && std::abs (mixer.setup().channel[3].faderDb + 7.5f) < 0.01f
                     && juce::JSON::toString (st["mixer"]["acts"]).contains ("Remote act")
                     && juce::JSON::toString (st["mixer"]["venues"]).contains ("Remote venue") && juce::JSON::toString (st["mixer"]["monitorSets"]).contains ("Remote monitors")
                     && ! fine (cmd ("{\"op\":\"mix.save\",\"kind\":\"act\",\"name\":\"\"}")) && ! fine (cmd ("{\"op\":\"mix.save\",\"kind\":\"lighting\",\"name\":\"x\"}")),
                   "remote: an act, a venue and a monitor set-up are saved from the page under typed names, show in the lists and load back");
        }

        {
            auto& music = engine.breakMusic();
            const auto folder = root.getChildFile ("Break music");
            folder.createDirectory();
            music.setFolder (folder);
            {
                juce::AudioBuffer<float> buf (2, 9600);
                for (int i = 0; i < 9600; ++i) { buf.setSample (0, i, 0.2f * std::sin ((float) i * 0.05f)); buf.setSample (1, i, 0.2f * std::sin ((float) i * 0.07f)); }
                juce::WavAudioFormat wav;
                auto out = std::unique_ptr<juce::OutputStream> (folder.getChildFile ("Quiet one.wav").createOutputStream());
                if (auto w = wav.createWriterFor (out, juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (16)))
                    w->writeFromAudioSampleBuffer (buf, 0, 9600);
            }

            const auto st = api.state();
            const bool listed = juce::JSON::toString (st["breakMusic"]["mixes"]).contains ("Quiet one") && has (st["breakMusic"], { "mix", "level", "minLevel", "maxLevel", "fadeDown", "fadeUp", "onSwitch", "onSong", "onSound", "soundDb" })
                                && ! juce::JSON::toString (st["breakMusic"]).contains (folder.getParentDirectory().getFullPathName());
            const bool silentStart = ! music.isPlaying() && ! (bool) api.live()["breakMusic"]["playing"];
            auto played = cmd ("{\"op\":\"break.play\",\"name\":\"Quiet one\"}");
            const bool playing = fine (played) && music.isPlaying() && music.isUp() && (bool) api.live()["breakMusic"]["playing"] && api.live()["breakMusic"]["mix"].toString() == "Quiet one";
            cmd ("{\"op\":\"break.level\",\"value\":-20}");
            cmd ("{\"op\":\"break.level\",\"value\":40}");
            const bool levelHeld = std::abs (music.levelDb() - BreakMusic::maxLevelDb) < 0.01f;
            cmd ("{\"op\":\"break.level\",\"value\":-18}");
            cmd ("{\"op\":\"break.down\"}");
            const bool down = ! music.isUp() && music.isPlaying();
            cmd ("{\"op\":\"break.up\"}");
            const bool upAgain = music.isUp();
            cmd ("{\"op\":\"break.set\",\"key\":\"fadeUp\",\"value\":6}");
            cmd ("{\"op\":\"break.set\",\"key\":\"onSong\",\"value\":true}");
            const auto set = music.settings();
            check (listed && silentStart && playing && levelHeld && std::abs (music.levelDb() + 18.0f) < 0.01f && down && upAgain && std::abs (set.fadeUpSeconds - 6.0) < 0.01 && set.onSong
                     && ! fine (cmd ("{\"op\":\"break.set\",\"key\":\"disco\",\"value\":1}")) && ! fine (cmd ("{\"op\":\"break.play\",\"name\":\"No such mix\"}")),
                   "remote: the page lists the mixes (and no folder path), plays one, sets its level inside the limits, fades it down and up and changes its settings");

            // The knob: a hold hands it to the break music, a turn is one dB, another hold hands it back.
            auto held = cmd ("{\"op\":\"knob\",\"do\":\"hold\"}");
            const bool onBreak = (bool) api.live()["breakMusic"]["knobOnBreak"];
            cmd ("{\"op\":\"knob\",\"do\":\"left\"}");
            const bool turned = std::abs (music.levelDb() + 19.0f) < 0.01f;
            cmd ("{\"op\":\"knob\",\"do\":\"hold\"}");
            check (fine (held) && onBreak && turned && ! (bool) api.live()["breakMusic"]["knobOnBreak"] && ! fine (cmd ("{\"op\":\"knob\",\"do\":\"spin\"}")),
                   "remote: the knob's buttons work as the knob will (hold for the break music, a click is one dB, hold to hand it back)");

            cmd ("{\"op\":\"break.stop\"}");
            check (! music.isPlaying() && ! (bool) api.live()["breakMusic"]["playing"], "remote: the page stops the break music");
        }

        {
            auto& tuner = engine.tuner();
            const auto st = api.state();
            const bool offFirst = ! tuner.isOn() && ! (bool) api.live()["tuner"]["on"] && has (st["tuner"], { "socket", "reference", "mutes" });
            cmd ("{\"op\":\"tuner.on\",\"on\":true}");
            const bool nowOn = tuner.isOn() && (bool) api.live()["tuner"]["on"] && has (api.live()["tuner"], { "heard", "name", "cents", "hz", "socket" });
            cmd ("{\"op\":\"tuner.set\",\"key\":\"socket\",\"value\":2}");
            cmd ("{\"op\":\"tuner.set\",\"key\":\"reference\",\"value\":442}");
            cmd ("{\"op\":\"tuner.set\",\"key\":\"mutes\",\"value\":false}");
            const bool setOk = tuner.socket() == 2 && std::abs (tuner.reference() - 442.0) < 0.01 && ! tuner.mutes();

            // 28 Nov 2026: what goes silent (the guitars, the whole main mix, nothing), which channels are guitars, and the full screen.
            cmd ("{\"op\":\"tuner.set\",\"key\":\"silence\",\"value\":\"main\"}");
            const bool mainOk = tuner.silence() == Tuner::Silence::mainMix && api.state()["tuner"]["silence"].toString() == "main";
            cmd ("{\"op\":\"tuner.set\",\"key\":\"silence\",\"value\":\"guitars\"}");
            cmd ("{\"op\":\"tuner.set\",\"key\":\"fullScreen\",\"value\":false}");
            cmd ("{\"op\":\"tuner.set\",\"key\":\"monitors\",\"value\":false}");
            const auto st2 = api.state();
            const auto* chans = st2["tuner"]["channels"].getArray();
            const bool wordsOk = tuner.silence() == Tuner::Silence::guitars && ! tuner.fillsScreen() && ! (bool) st2["tuner"]["fullScreen"] && chans != nullptr
                                     && ! tuner.monitorsKeepGuitars() && ! (bool) st2["tuner"]["monitors"]
                                     && ! fine (cmd ("{\"op\":\"tuner.set\",\"key\":\"silence\",\"value\":\"everything\"}"));
            bool chanOk = chans != nullptr && ! chans->isEmpty();

            if (chanOk)
            {
                const auto name = (*chans)[0]["name"].toString();
                const bool was = (bool) (*chans)[0]["silent"];
                cmd ("{\"op\":\"tuner.set\",\"key\":\"channel\",\"name\":" + juce::JSON::toString (juce::var (name)) + ",\"value\":" + (was ? "false" : "true") + "}");
                const auto after = api.state();
                chanOk = tuner.goesSilent (name) != was && (bool) after["tuner"]["channels"][0]["silent"] != was && has (api.live()["tuner"], { "strobing", "strength" })
                             && ! fine (cmd ("{\"op\":\"tuner.set\",\"key\":\"channel\",\"value\":true}"));
                tuner.setGoesSilent (name, was);
            }

            cmd ("{\"op\":\"tuner.on\",\"on\":false}");
            tuner.setSocket (-1);
            tuner.setReference (440.0);
            tuner.setSilence (Tuner::Silence::guitars);
            tuner.setFillsScreen (true);
            tuner.setMonitorsKeepGuitars (true);
            check (offFirst && nowOn && setOk && ! tuner.isOn() && ! fine (cmd ("{\"op\":\"tuner.set\",\"key\":\"colour\",\"value\":1}")),
                   "remote: the page switches the tuner on and off and sets its input, its A and whether anything goes silent");
            check (mainOk && wordsOk && chanOk, "remote: the page chooses what the tuner silences (the guitars, the whole main mix or nothing), which channels are guitars, whether the monitors keep them, and whether it fills the screen");
        }

        // ---- the server: the page's files, the two JSON texts, an order, and one real trip through a socket ----
        RemoteServer server (api);
        server.refreshNow();
        const auto text8 = [] (const RemoteServer::Response& a) { return juce::String::fromUTF8 ((const char*) a.body.getData(), (int) a.body.getSize()); };

        const auto page = server.handle ("GET", "/", {});
        const auto html = text8 (page);
        int filesNamed = 0, filesServed = 0;

        for (int at = html.indexOf ("src=\""); at >= 0; at = html.indexOf (at + 1, "src=\""))
        {
            const auto name = html.substring (at + 5).upToFirstOccurrenceOf ("\"", false, false);
            ++filesNamed;
            filesServed += server.handle ("GET", "/" + name, {}).status == 200 ? 1 : 0;
        }

        check (page.status == 200 && page.type.contains ("html") && html.contains ("Rig Control") && filesNamed >= 4 && filesServed == filesNamed
                 && server.handle ("GET", "/app.css", {}).status == 200,
               "remote server: the page and every file it names are inside the app (" + juce::String (filesServed) + " of " + juce::String (filesNamed) + " scripts)");

        const auto stateAnswer = juce::JSON::parse (text8 (server.handle ("GET", "/api/state", {})));
        const auto liveAnswer = juce::JSON::parse (text8 (server.handle ("GET", "/api/live", {})));
        check (stateAnswer.hasProperty ("v") && stateAnswer["s"]["songs"].size() == book.numEntries() && (int) liveAnswer["v"] == (int) stateAnswer["v"] && liveAnswer["l"].hasProperty ("looper"),
               "remote server: the state and the live part come as JSON with the same version number");

        const int v1 = (int) stateAnswer["v"];
        const auto done = juce::JSON::parse (text8 (server.handle ("POST", "/api/cmd", "{\"op\":\"mix.ch\",\"c\":2,\"key\":\"mute\",\"value\":true}")));
        const int v2 = (int) juce::JSON::parse (text8 (server.handle ("GET", "/api/live", {})))["v"];
        check (fine (done) && mixer.setup().channel[2].mute && v2 != v1 && ! fine (juce::JSON::parse (text8 (server.handle ("POST", "/api/cmd", "this is not JSON")))),
               "remote server: an order from the page is carried out and moves the version number on, so every open page fetches the new state");
        check (server.handle ("GET", "/nothing-here.js", {}).status == 404 && server.handle ("GET", "/../CMakeLists.txt", {}).status == 404
                 && server.handle ("GET", "/api/report", {}).status == 200 && server.handle ("DELETE", "/api/state", {}).status >= 400,
               "remote server: anything that is not one of the page's files or the four addresses is refused");

        const auto error = server.start (0);
        juce::String firstLine;

        if (error.isEmpty())
        {
            juce::StreamingSocket socket;

            if (socket.connect ("127.0.0.1", server.port(), 2000))
            {
                const juce::String request ("GET /api/live HTTP/1.1\r\nHost: self-test\r\nConnection: close\r\n\r\n");
                socket.write (request.toRawUTF8(), (int) request.getNumBytesAsUTF8());
                char buffer[256] = {};

                if (socket.waitUntilReady (true, 3000) == 1)
                {
                    const int got = socket.read (buffer, 255, false);
                    firstLine = juce::String::fromUTF8 (buffer, juce::jmax (0, got)).upToFirstOccurrenceOf ("\r\n", false, false);
                }
            }
        }

        server.stop();
        check (error.isEmpty() && firstLine.startsWith ("HTTP/1.1 200") && ! server.isRunning() && server.reportText().isNotEmpty(),
               "remote server: a real request through a socket on this computer is answered (" + (error.isNotEmpty() ? error : firstLine) + ")");

        // ---- the demo's copy of the state must have everything the real state has, or the demo page would break where the real one works ----
        int size = 0;
        const char* data = BinaryData::getNamedResource ("demodata_js", size);
        const auto js = data != nullptr ? juce::String::fromUTF8 (data, size) : juce::String();
        const auto demo = juce::JSON::parse (js.fromFirstOccurrenceOf ("window.GIGRIG_DEMO = ", false, false).upToLastOccurrenceOf (";\nwindow.GIGRIG_DEMO.songs", false, false));
        juce::StringArray missing;

        const std::function<void (const juce::var&, const juce::var&, const juce::String&)> sameShape = [&] (const juce::var& realOne, const juce::var& demoOne, const juce::String& where)
        {
            if (auto* o = realOne.getDynamicObject())
            {
                if (where == "types" || where.endsWith (".v") || where.endsWith (".t"))
                    return;                                             // these are keyed by effect and setting ids, which differ from rig to rig

                for (const auto& p : o->getProperties())
                {
                    if (! demoOne.hasProperty (p.name))
                        missing.add (where + "." + p.name.toString());
                    else
                        sameShape (p.value, demoOne[p.name], where.isEmpty() ? p.name.toString() : where + "." + p.name.toString());
                }
            }
            else if (realOne.isArray() && realOne.size() > 0 && demoOne.isArray() && demoOne.size() > 0)
                sameShape (realOne[0], demoOne[0], where + "[0]");
        };

        sameShape (api.state(), demo["state"], {});
        check (demo["songs"].size() > 0 && demo["songs"].size() == demo["state"]["songs"].size() && missing.isEmpty(),
               "remote demo: the demo's copy of the rig has every part the real state has" + (missing.isEmpty() ? juce::String() : " (missing: " + missing.joinIntoString (", ") + ")"));
    }

    root.deleteRecursively();
}

} // namespace gigrig
