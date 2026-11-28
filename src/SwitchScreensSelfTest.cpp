#include "SwitchScreens.h"

#include "effects/Registry.h"

#include <functional>

// Checks for the footswitch screens (28 Nov 2026), run by the automatic builds. The drawing and
// the messages checked here are the very code the board's controller runs (firmware/shared), so
// this is also the only automatic proof that code has: the messages survive the trip and a
// damaged one is refused, every tile fits its screen, the colours follow Tom's rules of
// 28 Nov 2026 (loop states, the stop switch, undo, all start/stop, bank, effects on and off in
// the three off looks, chain colours, tap tempo in time), and that switched off nothing is sent.

namespace gigrig
{

void runSwitchScreensSelfTest (const std::function<void (bool, const juce::String&)>& check)
{
    using namespace tile;
    using Inputs = SwitchScreens::Inputs;
    SwitchScreens::memoryOnDisk = false;

    const auto job = [] (LoopAction action, int track)
    {
        SwitchFunction f;
        f.push = action;
        f.track = track;
        return f;
    };
    const auto firstWords = [] (const Tile& t)
    {
        for (int i = 0; i < t.numItems; ++i)
            if (t.items[i].kind == itemWords)
                return juce::String (t.items[i].text);
        return juce::String();
    };
    const auto dotsOf = [] (const Tile& t, bool flashOn, bool* fits = nullptr)
    {
        std::vector<Colour> dots ((size_t) (side * side));
        const bool ok = draw (t, flashOn, dots.data());
        if (fits != nullptr) *fits = ok;
        return dots;
    };

    // ---- the messages ----
    {
        bool allBack = true, allSmall = true, allSevenBit = true, wrapped = true;
        int longest = 0;

        for (int page = 0; page < numDemoPages(); ++page)
            for (int p = 0; p < demoTilesPerPage; ++p)
            {
                const auto t = demoTile (page, p);
                uint8_t body[maxMessage + 8] = {};
                const int n = encodeTile ((page + p) % maxScreens, t, body);
                longest = juce::jmax (longest, n);
                allSmall = allSmall && n > 0 && n <= maxMessage;
                for (int i = 0; i < n; ++i)
                    allSevenBit = allSevenBit && body[i] < 128;

                Message m;
                allBack = allBack && decode (body, n, m) && m.command == commandTile && m.screen == (page + p) % maxScreens && sameTile (m.tile, t);

                uint8_t whole[maxMessage + 8] = { 0xF0 };
                memcpy (whole + 1, body, (size_t) n);
                whole[n + 1] = 0xF7;
                Message w;
                wrapped = wrapped && decode (whole, n + 2, w) && sameTile (w.tile, t);
            }

        check (allBack, "switch screens: every sample tile comes out of its message exactly as it went in");
        check (allSmall && allSevenBit, "switch screens: every message is short and MIDI-safe (longest " + juce::String (longest) + " bytes, none above 127)");
        check (wrapped, "switch screens: a message is read the same with its MIDI start and end bytes on");

        uint8_t body[maxMessage] = {};
        const auto t = demoTile (0, 2);
        const int n = encodeTile (4, t, body);
        Message m;
        bool refused = true;

        for (int i = 0; i < n; ++i)         // one damaged byte anywhere
        {
            uint8_t bad[maxMessage];
            memcpy (bad, body, (size_t) n);
            bad[i] = (uint8_t) ((bad[i] + 1) & 0x7F);
            refused = refused && ! decode (bad, n, m);
        }

        for (int cut = 0; cut < n; ++cut)   // cut short anywhere
            refused = refused && ! decode (body, cut, m);

        check (refused, "switch screens: a damaged or cut-off message is refused, never drawn");
        check (! decode (nullptr, 0, m) && ! decode (body, n + 1, m), "switch screens: nothing, and a message with a byte too many, are refused");

        // A kind of item this program has never heard of (from a newer app) is left out; the rest is kept.
        uint8_t newer[maxMessage];
        memcpy (newer, body, (size_t) n);
        newer[19] = 99;
        int sum = 0;
        for (int i = 0; i < n - 1; ++i) sum += newer[i];
        newer[n - 1] = (uint8_t) (sum & 0x7F);
        check (decode (newer, n, m) && m.tile.numItems == t.numItems && m.tile.items[0].kind == itemNone && m.tile.items[1].kind == itemLanes
               && m.tile.background == t.background,
               "switch screens: an item of a kind this program does not know is left out and the rest of the tile is kept");

        Status s;
        s.program = 2; s.screens = 17; s.tilesHeard = 12345; s.mode = showDemoPage; s.page = 9;
        const int sn = encodeStatus (s, body);
        check (decode (body, sn, m) && m.command == commandStatus && m.status.program == 2 && m.status.screens == 17 && m.status.tilesHeard == 12345
               && m.status.mode == showDemoPage && m.status.page == 9, "switch screens: the controller's answer (program, screens, tiles taken in) is read back");
        const int hn = encodeHello (body);
        const bool hello = decode (body, hn, m) && m.command == commandHello;
        const int shn = encodeShow (showDemoPage, 7, body);
        check (hello && decode (body, shn, m) && m.command == commandShow && m.mode == showDemoPage && m.page == 7, "switch screens: 'are you there' and 'show this' are read back");
    }

    // ---- the drawing ----
    {
        bool allFit = true, cornersRight = true, allHaveInk = true;
        juce::String misfit;

        for (int page = 0; page < numDemoPages(); ++page)
            for (int p = 0; p < demoTilesPerPage; ++p)
            {
                const auto t = demoTile (page, p);
                bool fits = false;
                const auto dots = dotsOf (t, false, &fits);

                if (! fits)
                {
                    allFit = false;
                    misfit = demoPageName (page);
                }

                const Colour edge = t.borderWidth > 0 ? t.border : t.background;
                cornersRight = cornersRight && dots[0] == edge && dots[(size_t) (side * side - 1)] == edge
                            && dots[(size_t) ((t.borderWidth + 1) * side + t.borderWidth + 1)] == t.background;

                int other = 0;
                for (const auto d : dots)
                    other += d != t.background && d != edge ? 1 : 0;
                allHaveInk = allHaveInk && other > 400;
            }

        check (allFit, "switch screens: every sample tile fits its 240 by 240 screen" + (misfit.isEmpty() ? juce::String() : " (not: " + misfit + ")"));
        check (cornersRight, "switch screens: each tile's edge is its border colour and inside it is the background colour");
        check (allHaveInk, "switch screens: every sample tile has something drawn on it");

        const auto recording = demoTile (0, 1);
        check (recording.flashCentis == 50 && dotsOf (recording, false)[0] == colours::red && dotsOf (recording, true)[0] == colours::redDark,
               "switch screens: a recording track flashes between red and dark red, half a second each");

        Tile tooLong;
        tooLong.items[tooLong.numItems++] = demoTile (14, 2).items[0];
        snprintf (tooLong.items[0].text, sizeof tooLong.items[0].text, "%s", "WWWWWWWWWWWWWWW");
        bool fits = true;
        dotsOf (tooLong, false, &fits);
        check (! fits, "switch screens: words too wide for the screen even at the smallest size are reported, not hidden");

        // Words that do not fit on one line go onto two, then down a size: the same words are narrower one size down.
        check (textWidth ("Delay throw", textWords) > textWidth ("Delay throw", textSmall) && textWidth ("Delay throw", textSmall) > textWidth ("Delay throw", textTiny)
               && textWidth ("8", textBig) > textWidth ("8", textWords), "switch screens: the four sizes of letters are four different sizes");

        // Every effect the rig has, as a switch tile in each of the three off looks, on and off.
        bool effectsFit = true, namesShort = true;
        juce::String worst;

        for (const auto& type : fx::effectTypes())
        {
            const auto name = SwitchScreens::shortName (type.name);
            namesShort = namesShort && name.isNotEmpty() && name.length() <= maxText;

            for (int look = 0; look < numOffLooks; ++look)
                for (int kind = 0; kind < numPedalKinds; ++kind)
                    for (int on = 0; on < 2; ++on)
                    {
                        bool ok = false;
                        dotsOf (effectTile (name.toRawUTF8(), nullptr, on == 1, look, colours::electric, kind, colours::green, colours::white), false, &ok);
                        if (! ok) { effectsFit = false; worst = type.name; }
                    }
        }

        check (namesShort, "switch screens: every effect's name cuts down to 15 letters or fewer for its tile");
        check (effectsFit, "switch screens: every effect's tile fits, in every look and with every kind of pedal picture" + (worst.isEmpty() ? juce::String() : " (not: " + juce::String (worst) + ")"));
        check (SwitchScreens::shortName ("1176 style compressor") == "1176 comp" && SwitchScreens::shortName ("Whammy style pedal") == "Whammy",
               "switch screens: an effect's name is cut down the way the phone screen cuts it (\"1176 style compressor\" is \"1176 comp\")");
    }

    // ---- Tom's rules: what each switch's screen shows ----
    {
        SwitchScreens screens;
        screens.setUseRealDevices (false);
        Inputs in;
        in.looperJob[0] = job (LoopAction::recPlay, 0);
        in.looperJob[1] = job (LoopAction::stop, 0);
        in.looperJob[2] = job (LoopAction::playStop, 0);
        in.looperJob[3] = job (LoopAction::undoRedo, -1);
        in.looperJob[4] = job (LoopAction::allStartStop, -2);
        in.looperJob[5] = job (LoopAction::bankToggle, -2);
        in.looperJob[6] = job (LoopAction::recPlay, -1);
        in.looperJob[7] = job (LoopAction::clear, 0);
        in.track[0].rcInputs = rcInst1L | rcInst1R;         // acoustic and stomp
        in.track[2].rcInputs = 63;                          // every socket
        in.currentTrack = 2;

        screens.update (in);
        const auto& track = screens.face (0).tile;
        check (track.background == colours::blue && track.flashCentis == 0 && firstWords (track) == "1" && track.items[0].size == textBig
               && track.items[1].kind == itemLanes && track.items[1].numPictures == 2 && track.items[1].pictures[0] == picAcoustic && track.items[1].pictures[1] == picKick,
               "switch screens: an empty loop track is blue, with its number and its lanes as pictures (acoustic guitar, kick drum)");
        check (screens.face (1).tile.background == colours::blue && firstWords (screens.face (1).tile) == "Stop 1",
               "switch screens: a stop switch is blue while its track is empty");
        check (screens.face (4).tile.background == colours::dim && firstWords (screens.face (4).tile) == "All start" && screens.face (4).tile.items[0].backing == 0,
               "switch screens: All start/stop is dim with nothing recorded (dark grey, paler words, no patch)");
        check (screens.face (5).tile.background == colours::white && firstWords (screens.face (5).tile) == "Bank 1", "switch screens: bank 1 is white");
        check (firstWords (screens.face (6).tile) == "3" && screens.face (6).tile.items[1].numPictures == 3 && screens.face (6).tile.items[1].more == 2
               && screens.face (6).tile.items[1].pictures[0] == picMic,
               "switch screens: a switch for 'the current track' shows that track's number, and five lanes show three pictures and +2");
        check (screens.face (7).tile.background == colours::dim && screens.face (8).tile.background == colours::dim && firstWords (screens.face (8).tile) == "Switch 9",
               "switch screens: a switch with nothing to do just now is dim");

        in.track[0].state = TrackState::recording;
        screens.update (in);
        check (screens.face (0).tile.background == colours::red && screens.face (0).tile.flashTo == colours::redDark && screens.face (0).tile.flashCentis == 50,
               "switch screens: a recording track flashes red");
        check (screens.face (1).tile.background == colours::red && screens.face (2).tile.background == colours::red && firstWords (screens.face (2).tile) == "Stop 1",
               "switch screens: a stop switch is red while its track records");
        check (screens.face (4).tile.background == colours::green && firstWords (screens.face (4).tile) == "All stop", "switch screens: All start/stop is green and says All stop while anything records or plays");

        in.track[0].state = TrackState::playing;
        in.track[0].hasAudio = true;
        screens.update (in);
        check (screens.face (0).tile.background == colours::green && screens.face (0).tile.flashCentis == 0, "switch screens: a playing track is green");
        check (screens.face (1).tile.background == colours::red, "switch screens: a stop switch is red while its track plays");
        check (screens.face (7).tile.background == colours::white && firstWords (screens.face (7).tile) == "Clear 1", "switch screens: a clear switch is white once its track holds a loop");

        in.track[0].state = TrackState::overdubbing;
        screens.update (in);
        check (screens.face (0).tile.background == colours::yellow, "switch screens: an overdubbing track is yellow");

        in.track[0].state = TrackState::stopped;
        screens.update (in);
        check (screens.face (0).tile.background == colours::red && screens.face (0).tile.flashCentis == 0, "switch screens: a stopped track is steady red");
        check (screens.face (1).tile.background == colours::dim && firstWords (screens.face (1).tile) == "Stop 1" && firstWords (screens.face (2).tile) == "Play 1",
               "switch screens: a stop switch is dim when its track is recorded but stopped (and a play/stop switch then says Play)");
        check (screens.face (4).tile.background == colours::red && firstWords (screens.face (4).tile) == "All start", "switch screens: All start/stop is red and says All start when everything is stopped");

        in.bank = 1;
        screens.update (in);
        check (screens.face (5).tile.background == colours::purple && firstWords (screens.face (5).tile) == "Bank 2", "switch screens: bank 2 is purple");

        // Undo: white unused, red "Redo" after an undo, green "Undo" after a redo, white again once something new is recorded.
        in.track[0].state = TrackState::playing;
        screens.update (in);
        const bool unused = screens.face (3).tile.background == colours::white && firstWords (screens.face (3).tile) == "Undo";
        in.track[0].undone = true;
        screens.update (in);
        const bool undone = screens.face (3).tile.background == colours::red && firstWords (screens.face (3).tile) == "Redo";
        in.track[0].undone = false;
        screens.update (in);
        const bool redone = screens.face (3).tile.background == colours::green && firstWords (screens.face (3).tile) == "Undo";
        screens.update (in);
        const bool stays = screens.undoLamp() == SwitchScreens::UndoLamp::redone;
        in.track[1].state = TrackState::recording;
        screens.update (in);
        const bool whiteAgain = screens.face (3).tile.background == colours::white && firstWords (screens.face (3).tile) == "Undo";
        check (unused && undone && redone && stays && whiteAgain,
               "switch screens: undo is white unused, red 'Redo' after an undo, green 'Undo' after a redo, and white again when a new loop is recorded");

        in.track[1].state = TrackState::playing;
        in.track[1].hasAudio = true;
        in.track[1].undone = true;
        screens.update (in);
        const bool red = screens.undoLamp() == SwitchScreens::UndoLamp::undone;
        in.track[1] = {};
        screens.update (in);
        check (red && screens.undoLamp() == SwitchScreens::UndoLamp::unused, "switch screens: an undone loop that is then cleared leaves undo white, not green");

        // The white-outline look turns every white tile into a white outline on black.
        auto st = screens.settings();
        st.offLook = lookWhiteOutline;
        screens.setSettings (st);
        in.bank = 0;
        screens.update (in);
        check (screens.face (5).tile.background == colours::black && screens.face (5).tile.border == colours::white && screens.face (5).tile.borderWidth > 0,
               "switch screens: with the white outline look, a white tile is a white outline on black");
    }

    // ---- effect switches ----
    {
        SwitchScreens screens;
        screens.setUseRealDevices (false);
        Inputs in;
        auto& drive = in.effect[0];
        drive.used = true; drive.action = 12; drive.chain = 2; drive.chainName = "Electric"; drive.effectId = "ts"; drive.effectName = "Tube screamer style drive"; drive.toggle = 0;
        auto& harmony = in.effect[1];
        harmony.used = true; harmony.action = 8; harmony.chainName = "Vocal"; harmony.second = "Key of G";
        auto& tap = in.effect[2];
        tap.used = true; tap.action = 2; tap.second = "120";
        auto& wah = in.effect[3];
        wah.used = true; wah.action = 6; wah.chainName = "Tom acoustic";
        in.effect[4].used = true; in.effect[4].action = 10; in.effect[4].chainName = "Bass"; in.effect[4].effectId = "bass9"; in.effect[4].effectName = "Bass"; in.effect[4].toggle = 1;
        in.looperJob[0] = job (LoopAction::recPlay, 0);     // the effects have taken switch 1 from the looper
        in.bpm = 120.0;

        bool looksRight = true;

        for (int look = 0; look < numOffLooks; ++look)
        {
            auto st = screens.settings();
            st.offLook = look;
            screens.setSettings (st);
            in.effect[0].toggle = 0;
            screens.update (in);
            const auto off = screens.face (0).tile;
            in.effect[0].toggle = 1;
            screens.update (in);
            const auto on = screens.face (0).tile;

            looksRight = looksRight && on.background == colours::purple && off.items[0].kind == itemPedal && off.items[0].lit == 0 && on.items[0].lit == 1
                      && firstWords (off).isNotEmpty() && firstWords (off).length() <= maxText;

            if (look == lookChainBorder)
                looksRight = looksRight && off.background == colours::black && off.border == colours::electric && on.border == colours::electric && off.borderWidth > 0 && on.borderWidth > 0;
            else if (look == lookWhiteOutline)
                looksRight = looksRight && off.background == colours::black && off.border == colours::white && off.borderWidth > 0 && on.borderWidth == 0;
            else
                looksRight = looksRight && off.background == colours::white && off.borderWidth == 0 && on.borderWidth == 0;
        }

        check (looksRight, "switch screens: an effect switch is purple when on, and off it follows the look chosen (chain colour border, white outline, or white all over), with its pedal picture");

        auto st = screens.settings();
        st.offLook = lookChainBorder;
        screens.setSettings (st);
        screens.update (in);
        check (SwitchScreens::chainColour ("Vocal") == colours::vocal && SwitchScreens::chainColour ("Tom acoustic") == colours::acoustic
               && SwitchScreens::chainColour ("Electric") == colours::electric && SwitchScreens::chainColour ("Bass on electric") == colours::electric
               && SwitchScreens::chainColour ("Bass") == colours::bass && SwitchScreens::chainColour ("Stomp") == colours::stomp
               && SwitchScreens::chainColour ("Duo vocal") == colours::duo
               && screens.face (4).tile.border == colours::bass && screens.face (3).tile.border == colours::acoustic,
               "switch screens: the chain colours are red vocal, yellow acoustic, blue electric, orange bass, purple stomp and green duo, taken from the chain's name");

        const auto& h = screens.face (1).tile;
        check (h.background == colours::black && h.border == colours::vocal && firstWords (h) == "Harmony" && h.numItems == 2 && juce::String (h.items[1].text) == "Key of G",
               "switch screens: a hold effect shows words only, with the key for the harmony");
        in.held[1] = true;
        screens.update (in);
        check (screens.face (1).tile.background == colours::purple, "switch screens: a hold effect is purple while its switch is held");

        const auto& t = screens.face (2).tile;
        check (t.background == colours::purple && t.flashTo == colours::black && t.flashCentis == 25 && juce::String (t.items[1].text) == "120",
               "switch screens: tap tempo blinks in time (120 beats a minute is a quarter of a second on, a quarter off)");
        in.bpm = 0.0;
        in.effect[2].second = {};
        screens.update (in);
        check (screens.face (2).tile.flashCentis == 0 && screens.face (2).tile.background == colours::black, "switch screens: with no tempo yet, tap tempo is steady and off");
        check (screens.reportText().contains ("Switch 1: Tube screamer") && screens.reportText().contains ("Sending") == false && screens.reportText().contains ("Off. Nothing is opened or sent."),
               "switch screens: the report says what each switch's screen shows, and that nothing is being sent");
    }

    // ---- sending ----
    {
        SwitchScreens screens;
        screens.setUseRealDevices (false);
        std::vector<Message> got;
        int unreadable = 0;
        screens.sendMidi = [&] (const juce::MidiMessage& message)
        {
            Message m;
            if (message.isSysEx() && decode (message.getSysExData(), message.getSysExDataSize(), m))
                got.push_back (m);
            else
                ++unreadable;
        };
        const auto tilesFor = [&] (int screen)
        {
            int n = 0;
            for (const auto& m : got)
                n += m.command == commandTile && m.screen == screen ? 1 : 0;
            return n;
        };

        Inputs in;
        for (int sw = 0; sw < 6; ++sw)
            in.looperJob[(size_t) sw] = job (LoopAction::recPlay, sw);

        for (int i = 0; i < 12; ++i)
            screens.update (in);

        check (got.empty() && screens.messagesSent() == 0 && ! screens.outputIsOpen(), "switch screens: switched off (as it starts), no MIDI output is opened and nothing is sent");

        auto st = screens.settings();
        st.send = true;
        screens.setSettings (st);
        screens.update (in);
        bool first = unreadable == 0 && got.size() == 5 && got[0].command == commandShow && got[0].mode == showRig && got[1].command == commandHello;
        for (int s = 0; s < 3 && first; ++s)
            first = got[(size_t) (2 + s)].command == commandTile && got[(size_t) (2 + s)].screen == s && sameTile (got[(size_t) (2 + s)].tile, screens.face (s).tile);
        check (first, "switch screens: switched on, the controller is told what to show, asked if it is there, and sent the three bench screens' tiles");

        got.clear();
        screens.update (in);
        screens.update (in);
        check (tilesFor (0) + tilesFor (1) + tilesFor (2) == 0, "switch screens: a tile that has not changed is not sent again straight away");

        in.track[1].state = TrackState::recording;
        screens.update (in);
        check (tilesFor (1) == 1 && tilesFor (0) == 0 && tilesFor (2) == 0 && got.back().tile.flashCentis == 50,
               "switch screens: when one switch's state changes, that screen's tile goes out at once and the others do not");

        got.clear();
        for (int i = 0; i < 40; ++i)
            screens.update (in);
        check (tilesFor (0) >= 2 && tilesFor (1) >= 2 && tilesFor (2) >= 2 && tilesFor (0) <= 4,
               "switch screens: every screen's tile is sent again a little later, so a controller that missed one is put right");

        st.bench = { 5, 0, 1 };
        screens.setSettings (st);
        got.clear();
        screens.update (in);
        Message* toFirst = nullptr;
        for (auto& m : got)
            if (m.command == commandTile && m.screen == 0)
                toFirst = &m;
        check (toFirst != nullptr && sameTile (toFirst->tile, screens.face (5).tile) && screens.switchForScreen (0) == 5 && screens.switchForScreen (2) == 1 && screens.switchForScreen (7) == 7
               && screens.switchForScreen (12) == -1,
               "switch screens: each bench screen shows the switch chosen for it");

        // The controller answers: the app believes it, and sends as many screens as it says it has.
        check (! screens.controllerAnswering(), "switch screens: before the controller answers, the tab says it has not");
        Status status;
        status.program = 2; status.screens = 17; status.tilesHeard = 40;
        uint8_t body[maxMessage];
        const int n = encodeStatus (status, body);
        screens.handleMidi ("Teensy MIDI", juce::MidiMessage::createSysExMessage (body, n));
        screens.handleMidi ("Teensy MIDI", juce::MidiMessage::noteOn (10, 60, (juce::uint8) 100));
        got.clear();
        screens.update (in);
        int newScreens = 0;      // the three bench screens have theirs already
        for (int s = 3; s < maxScreens; ++s)
            newScreens += tilesFor (s) > 0 ? 1 : 0;
        check (screens.controllerAnswering() && screens.controllerStatus().screens == 17 && newScreens == numPedalSwitches - 3 && screens.statusLine().contains ("17 screens"),
               "switch screens: when the controller answers that it has 17 screens, every switch the app knows gets a screen");

        st.show = showDemoPage;
        st.page = 4;
        screens.setSettings (st);
        got.clear();
        screens.update (in);
        check (! got.empty() && got[0].command == commandShow && got[0].mode == showDemoPage && got[0].page == 4, "switch screens: choosing a sample page tells the controller at once");

        st.send = false;
        screens.setSettings (st);
        got.clear();
        for (int i = 0; i < 10; ++i)
            screens.update (in);
        check (got.empty(), "switch screens: switched off again, nothing more is sent");

        SwitchScreens other;
        other.setUseRealDevices (false);
        st.send = true;
        st.offLook = lookWhiteOutline;
        screens.setSettings (st);
        other.loadStateText (screens.stateText());
        check (other.settings() == screens.settings() && screens.stateText().contains ("off look = white outline") && screens.stateText().contains ("bench screen 1 = 6"),
               "switch screens: the choices are remembered in a small text file and read back the same");
    }
}

} // namespace gigrig
