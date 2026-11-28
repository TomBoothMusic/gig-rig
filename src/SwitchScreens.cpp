#include "SwitchScreens.h"

#include "effects/Registry.h"
#include "effects/harmony/Harmony.h"

namespace gigrig
{

namespace
{
    using namespace tile;

    bool busy (TrackState s)
    {
        return s == TrackState::armed || s == TrackState::recording || s == TrackState::closing
            || s == TrackState::playing || s == TrackState::overdubbing || s == TrackState::stopping;
    }

    bool takingSound (TrackState s)
    {
        return s == TrackState::armed || s == TrackState::recording || s == TrackState::closing || s == TrackState::overdubbing;
    }

    /** The stand-in pedal drawings: the kind of gear and its colour, by effect type. The same
        table as pedalLook in remote/screens.js; keep the two in step. (The looks of Tom's real
        pedals are in his private file, not here.) */
    struct PedalLook { const char* id; int kind; juce::uint32 colour; };

    const PedalLook pedalLooks[] =
    {
        { "comp", pedalRack, 0x17181b }, { "la2a", pedalRack, 0xb9bcc0 }, { "peq", pedalRack, 0x2b3440 }, { "plate", pedalRack, 0x8f979f },
        { "room", pedalRack, 0x3a4652 }, { "harmony", pedalRack, 0x4a3a6e }, { "ir", pedalRack, 0x4f4436 }, { "body", pedalStomp, 0x8a5a2b },
        { "eq", pedalSliders, 0xc9d2d8 }, { "gate", pedalStomp, 0x6b7480 },
        { "bd", pedalStomp, 0x2f6fd0 }, { "klon", pedalStomp, 0xb89b4a }, { "ts", pedalStomp, 0x3fa35a }, { "marshall", pedalStomp, 0x26262a },
        { "fuzz", pedalStomp, 0xb23a2e }, { "boost", pedalStomp, 0xe6e6e2 }, { "bass9", pedalStomp, 0x1c1c1f },
        { "amp", pedalAmp, 0x20201f },
        { "wah", pedalRocker, 0x1b1b1d }, { "whammy", pedalRocker, 0xb3202a }, { "volume", pedalRocker, 0x5a6068 },
        { "flanger", pedalStomp, 0x7d4bc2 }, { "chorus", pedalStomp, 0x5db6e8 }, { "vibrato", pedalStomp, 0x2aa39a }, { "univibe", pedalStomp, 0x3b3f46 },
        { "tremolo", pedalStomp, 0xd98a2b }, { "analogdelay", pedalStomp, 0x2d6b4f }, { "tapedelay", pedalStomp, 0xd8cfb8 }, { "digitaldelay", pedalStomp, 0xdfe6ec },
        { "spring", pedalStomp, 0xa8875a },
    };

    PedalLook lookFor (const juce::String& effectId)
    {
        for (const auto& l : pedalLooks)
            if (effectId == l.id)
                return l;

        return { "", pedalStomp, 0x56616d };
    }

    Colour colour565 (juce::uint32 c)       { return rgb ((int) ((c >> 16) & 255), (int) ((c >> 8) & 255), (int) (c & 255)); }

    bool isLight (juce::uint32 c)
    {
        return (((c >> 16) & 255) * 299 + ((c >> 8) & 255) * 587 + (c & 255) * 114) / 1000 > 150;
    }

    juce::String colourName (Colour c)
    {
        return c == colours::blue ? "blue" : c == colours::red ? "red" : c == colours::green ? "green" : c == colours::yellow ? "yellow"
             : c == colours::white ? "white" : c == colours::purple ? "purple" : c == colours::dim ? "dim grey" : c == colours::black ? "black"
             : c == colours::electric ? "blue" : c == colours::bass ? "orange" : c == colours::stomp ? "green" : c == colours::duo ? "light purple" : "grey";
    }

    /** "purple", "black with a yellow border", "red, flashing". */
    juce::String lookInWords (const Tile& t)
    {
        juce::String s = colourName (t.background);
        if (t.flashCentis > 0)      s << ", flashing";
        if (t.borderWidth > 0)      s << " with a " << colourName (t.border) << " border";
        return s;
    }

    /** The lane pictures for a track's sockets: vocal, acoustic, electric, bass, stomp, in that order. */
    int lanePictures (int rcInputs, uint8_t* pictures, int& more, juce::String& words)
    {
        juce::Array<int> all;
        juce::StringArray names;
        if ((rcInputs & (rcMic1 | rcMic2)) != 0)    { all.add (picMic); names.add ("vocal"); }
        if ((rcInputs & rcInst1L) != 0)             { all.add (picAcoustic); names.add ("acoustic"); }
        if ((rcInputs & rcInst2R) != 0)             { all.add (picElectric); names.add ("electric"); }
        if ((rcInputs & rcInst2L) != 0)             { all.add (picBass); names.add ("bass"); }
        if ((rcInputs & rcInst1R) != 0)             { all.add (picKick); names.add ("stomp"); }

        const int shown = juce::jmin (maxLanePictures, all.size());
        for (int i = 0; i < shown; ++i)
            pictures[i] = (uint8_t) all[i];

        more = all.size() - shown;
        words = names.isEmpty() ? juce::String ("no lanes") : "lanes: " + names.joinIntoString (", ");
        return shown;
    }
}

//==============================================================================
SwitchScreens::SwitchScreens() = default;

SwitchScreens::~SwitchScreens()
{
    stopTimer();
    output.reset();
}

juce::String SwitchScreens::offLookName (int look)
{
    return look == lookWhiteOutline ? "white outline on black" : look == lookFullWhite ? "full white" : "chain colour border";
}

juce::String SwitchScreens::shortName (const juce::String& effectName)
{
    auto s = effectName;

    while (s.contains ("(") && s.fromFirstOccurrenceOf ("(", false, false).contains (")"))
        s = (s.upToFirstOccurrenceOf ("(", false, false).trimEnd() + " " + s.fromFirstOccurrenceOf (")", false, false).trimStart()).trim();

    s = s.replace (" style", "", true).replace ("compressor", "comp", true);
    if (s.startsWith ("Mixer "))            s = s.substring (6);
    if (s.endsWithIgnoreCase (" pedal"))    s = s.dropLastCharacters (6);
    if (s.endsWithIgnoreCase (" copy"))     s = s.dropLastCharacters (5);
    s = s.trim();

    // Fifteen letters is all a tile's line holds: whole words are dropped from the end until it fits.
    while (s.length() > maxText && s.containsChar (' '))
        s = s.upToLastOccurrenceOf (" ", false, false).trim();

    s = s.substring (0, maxText).trim();
    return s.isEmpty() ? effectName.substring (0, maxText) : s.substring (0, 1).toUpperCase() + s.substring (1);
}

Colour SwitchScreens::chainColour (const juce::String& chainName)
{
    const auto n = chainName.toLowerCase();
    return n.startsWith ("duo") ? colours::duo
         : n.contains ("vocal") || n.contains ("vox") ? colours::vocal
         : n.contains ("acoustic") ? colours::acoustic
         : n.contains ("electric") ? colours::electric
         : n.contains ("bass") ? colours::bass
         : n.contains ("stomp") || n.contains ("kick") ? colours::stomp
         : colours::dimInk;
}

juce::Image SwitchScreens::picture (const Tile& t, bool flashOn)
{
    std::vector<Colour> dots ((size_t) (side * side));
    draw (t, flashOn, dots.data());
    juce::Image image (juce::Image::RGB, side, side, false);
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
        {
            const Colour c = dots[(size_t) (y * side + x)];
            data.setPixelColour (x, y, juce::Colour ((juce::uint8) (((c >> 11) & 31) * 255 / 31), (juce::uint8) (((c >> 5) & 63) * 255 / 63), (juce::uint8) ((c & 31) * 255 / 31)));
        }

    return image;
}

//==============================================================================
SwitchScreens::Inputs SwitchScreens::readRig (AudioEngine& engine, PedalInput& pedal)
{
    Inputs in;
    const auto snap = engine.looper().snapshot();
    const auto song = engine.looper().song();
    in.bank = juce::jlimit (0, numLoopBanks - 1, snap.bank);
    in.currentTrack = snap.currentTrack;

    for (int t = 0; t < numLoopTracks; ++t)
    {
        const auto& v = snap.track[(size_t) t];
        in.track[(size_t) t] = { v.state, v.hasAudio, v.undone || v.overdubUndone, song.track[(size_t) t].rcInputs };
    }

    for (int sw = 0; sw < 9; ++sw)
        in.looperJob[(size_t) sw] = decodeRcFunction (song.bank[in.bank][sw]);

    // CTL3 and CTL4 have no bank function: what they do comes from the song's assigns.
    for (const auto& a : song.assign)
        if (a.source == LoopAssign::Source::pedal && a.sw >= 9 && a.sw < numPedalSwitches && a.action != LoopAction::none
            && in.looperJob[(size_t) a.sw].push == LoopAction::none)
        {
            in.looperJob[(size_t) a.sw].push = a.action;
            in.looperJob[(size_t) a.sw].track = a.targetTrack;
        }

    const auto seen = pedal.seen();
    for (int sw = 0; sw < numPedalSwitches; ++sw)
        in.held[(size_t) sw] = seen[(size_t) sw].down;

    auto& rack = engine.effects();
    in.bpm = rack.effectsTempo() > 0.0 ? rack.effectsTempo() : snap.bpm;

    if (rack.isEnabled())
        for (int i = 0; i < EffectsRack::numPedalAssigns; ++i)
        {
            const auto a = rack.pedalAssign (i);

            if (a.sw < 0 || a.sw >= numPedalSwitches || in.effect[(size_t) a.sw].used)
                continue;       // the first row that names a switch is the one its screen shows

            auto& e = in.effect[(size_t) a.sw];
            const int c = juce::jlimit (0, EffectsRack::numChains - 1, a.chain);
            e.used = true;
            e.action = a.action;
            e.chain = c;
            e.chainName = rack.routing (c).name;
            e.toggle = rack.pedalToggleState (a);
            const auto& model = rack.chain (c).model();

            if (a.action >= 10 && a.action != EffectsRack::actionPedalToe && a.action - 10 < (int) model.slots.size())
            {
                e.effectId = model.slots[(size_t) (a.action - 10)].type;
                if (const auto* type = fx::findEffectType (model.slots[(size_t) (a.action - 10)].type))
                    e.effectName = type->name;
            }
            else if (a.action == 8)
            {
                for (const auto& slot : model.slots)
                    if (slot.type == "harmony")
                    {
                        const auto& keys = fx::Harmony::keyNames();
                        const int k = juce::jlimit (0, (int) keys.size() - 1, (int) std::lround (slot.param ("key", 0.0f)));
                        e.second = "Key of " + juce::String (keys[(size_t) k]).upToFirstOccurrenceOf (" /", false, false);
                        break;
                    }
            }
            else if (a.action == 2 && in.bpm > 0.0)
            {
                e.second = juce::String (juce::roundToInt (in.bpm));
            }
        }

    return in;
}

//==============================================================================
SwitchScreens::Face SwitchScreens::effectFace (int sw, const Inputs& in) const
{
    const auto& e = in.effect[(size_t) sw];
    const bool held = in.held[(size_t) sw];
    const auto chain = e.action == 2 || e.action == 4 ? colours::dimInk : chainColour (e.chainName);      // tap tempo and the test take belong to no chain
    const auto chainWords = e.chainName.isNotEmpty() ? e.chainName : "chain " + juce::String (e.chain + 1);
    const auto words = [&] (const juce::String& name, const juce::String& second, bool on, const juce::String& what)
    {
        Face f;
        f.tile = effectTile (name.toRawUTF8(), second.isEmpty() ? nullptr : second.toRawUTF8(), on, now.offLook, chain, -1, 0, 0);
        f.words = what + ": " + lookInWords (f.tile);
        return f;
    };

    if (e.action >= 10 && e.action != EffectsRack::actionPedalToe)
    {
        const auto look = lookFor (e.effectId);
        const auto name = shortName (e.effectName.isNotEmpty() ? e.effectName : "Slot " + juce::String (e.action - 9));
        Face f;
        f.tile = effectTile (name.toRawUTF8(), nullptr, e.toggle == 1, now.offLook, chain, look.kind, colour565 (look.colour),
                             isLight (look.colour) ? rgb (0x11, 0x11, 0x11) : rgb (0xf1, 0xea, 0xd2));
        f.words = name + " (" + chainWords + ") " + (e.toggle == 1 ? "ON: " : "off: ") + lookInWords (f.tile) + ", with a pedal picture";
        return f;
    }

    switch (e.action)
    {
        case 0:  return words ("A/B", e.toggle == 1 ? "Effects" : "Dry", e.toggle == 1, "A/B on " + chainWords);
        case 1:  return words (shortName (chainWords), e.toggle == 1 ? "On" : "Off", e.toggle == 1, chainWords + " on/off");
        case 2:
        {
            auto f = words ("Tap", e.second, in.bpm > 0.0, "tap tempo");

            if (in.bpm > 0.0)       // blinks in time: purple on the beat, the off look between
            {
                f.tile.flashTo = now.offLook == lookFullWhite ? colours::white : colours::black;
                f.tile.flashCentis = (uint8_t) juce::jlimit (5, 127, juce::roundToInt (3000.0 / in.bpm));
                f.words = "tap tempo: purple, blinking at " + juce::String (juce::roundToInt (in.bpm)) + " beats a minute";
            }

            return f;
        }
        case 3:  return words ("Whammy", {}, held, "whammy bend while held (" + chainWords + ")");
        case 4:  return words ("Test take", {}, held, "test take");
        case 5:  return words ("Delay throw", {}, held, "delay throw while held (" + chainWords + ")");
        case 6:  return words ("Wah", {}, held, "wah sweep while held (" + chainWords + ")");
        case 7:  return words ("Next amp", {}, held, "next amp sound (" + chainWords + ")");
        case 8:  return words ("Harmony", e.second, held, "harmony while held (" + chainWords + ")");
        case 9:  return words ("Toe switch", e.toggle == 1 ? "On" : "Off", e.toggle == 1, "toe switch");
        default: break;
    }

    return words ("Pedal toe", {}, held, "expression pedal toe down while held");
}

SwitchScreens::Face SwitchScreens::faceFor (int sw, const Inputs& in) const
{
    if (in.effect[(size_t) sw].used)
        return effectFace (sw, in);

    const auto& job = in.looperJob[(size_t) sw];
    const int t = job.track == -1 ? in.currentTrack : job.track;
    const bool hasTrack = t >= 0 && t < numLoopTracks;
    const auto number = juce::String (t + 1);
    const auto made = [] (const Tile& tileMade, const juce::String& what)
    {
        Face f;
        f.tile = tileMade;
        f.words = what + ": " + lookInWords (tileMade);
        return f;
    };
    const auto plain = [this, &made] (Colour background, const juce::String& text, const juce::String& what)
    {
        return made (wordsTile (background, text.toRawUTF8(), nullptr, now.offLook), "\"" + text + "\", " + what);
    };
    const auto dimmed = [&made] (const juce::String& text, const juce::String& what)
    {
        return made (dimTile (text.toRawUTF8()), "\"" + text + "\", " + what);
    };

    bool anyBusy = false, anyAudio = false;
    for (const auto& tr : in.track)
    {
        anyBusy = anyBusy || busy (tr.state);
        anyAudio = anyAudio || tr.hasAudio;
    }

    switch (job.push)
    {
        case LoopAction::recPlay:
        {
            if (! hasTrack)
                break;

            const auto& tr = in.track[(size_t) t];
            const int look = takingSound (tr.state) && tr.state != TrackState::overdubbing ? trackRecording
                           : tr.state == TrackState::overdubbing ? trackOverdubbing
                           : tr.state == TrackState::playing || tr.state == TrackState::stopping ? trackPlaying
                           : tr.state == TrackState::stopped && tr.hasAudio ? trackStopped : trackEmpty;
            uint8_t pictures[maxLanePictures] = {};
            int more = 0;
            juce::String lanes;
            const int shown = lanePictures (tr.rcInputs, pictures, more, lanes);
            const char* const stateWords[] = { "empty", "recording", "playing", "overdubbing", "stopped" };
            auto f = made (trackTile (t + 1, look, pictures, shown, more), "track " + number + ", " + stateWords[look]);
            f.words << "; " << lanes;
            return f;
        }

        case LoopAction::playStop:
        case LoopAction::stop:
        {
            if (! hasTrack)
                break;

            // Tom, 28 Nov 2026: red while its track plays or records, blue when empty, dim when recorded but stopped.
            const auto& tr = in.track[(size_t) t];
            const bool starts = job.push == LoopAction::playStop && ! busy (tr.state);
            const auto text = (starts ? "Play " : "Stop ") + number;

            if (busy (tr.state))    return plain (colours::red, text, "its track is playing or recording");
            if (! tr.hasAudio)      return plain (colours::blue, text, "its track is empty");
            return dimmed (text, "its track is recorded but stopped");
        }

        case LoopAction::clear:
            if (! hasTrack)
                break;
            return in.track[(size_t) t].hasAudio || busy (in.track[(size_t) t].state) ? plain (colours::white, "Clear " + number, "its track holds a loop")
                                                                                         : dimmed ("Clear " + number, "its track is empty");

        case LoopAction::undoRedo:
        case LoopAction::undoOrClear:
            // The screen names what the next press does.
            if (lamp == UndoLamp::undone)   return plain (colours::red, "Redo", "after an undo");
            if (lamp == UndoLamp::redone)   return plain (colours::green, "Undo", "after a redo");
            return plain (colours::white, "Undo", "not used yet");

        case LoopAction::allStartStop:
            if (anyBusy)    return plain (colours::green, "All stop", "something is playing or recording");
            if (anyAudio)   return plain (colours::red, "All start", "everything is stopped");
            return dimmed ("All start", "nothing is recorded");

        case LoopAction::allClear:
            return anyAudio || anyBusy ? plain (colours::white, "Clear all", "there are loops") : dimmed ("Clear all", "nothing is recorded");

        case LoopAction::bankToggle:
            return in.bank == 0 ? plain (colours::white, "Bank 1", "bank 1") : plain (colours::purple, "Bank 2", "bank 2");

        case LoopAction::none:
        case LoopAction::unsupported:
        default:
            break;
    }

    return dimmed (sw < 9 ? "Switch " + juce::String (sw + 1) : sw == 9 ? juce::String ("CTL3") : juce::String ("CTL4"), "does nothing in this song");
}

void SwitchScreens::followUndo (const Inputs& in)
{
    bool recording = false, anyUndone = false, cameBack = false;

    for (int t = 0; t < numLoopTracks; ++t)
    {
        const auto& tr = in.track[(size_t) t];
        recording = recording || takingSound (tr.state);
        anyUndone = anyUndone || tr.undone;
        // An undone loop that is back, still holding its sound, was redone (a cleared one is just gone).
        cameBack = cameBack || (wasUndone[(size_t) t] && ! tr.undone && tr.hasAudio);
        wasUndone[(size_t) t] = tr.undone;
    }

    // Tom, 28 Nov 2026: back to white whenever a new loop or overdub is recorded after an undo or redo.
    if (recording)          lamp = UndoLamp::unused;
    else if (anyUndone)     lamp = UndoLamp::undone;
    else if (cameBack)      lamp = UndoLamp::redone;
    else if (lamp == UndoLamp::undone)  lamp = UndoLamp::unused;      // what was undone has gone (cleared, or another song)

    bool anyAudio = false;
    for (const auto& tr : in.track)
        anyAudio = anyAudio || tr.hasAudio || tr.undone;

    if (! anyAudio)
        lamp = UndoLamp::unused;
}

int SwitchScreens::switchForScreen (int screen) const
{
    if (screen >= 0 && screen < (int) now.bench.size())
        return juce::jlimit (0, numPedalSwitches - 1, now.bench[(size_t) screen]);

    return screen >= 0 && screen < numPedalSwitches ? screen : -1;
}

//==============================================================================
void SwitchScreens::setSettings (const Settings& s)
{
    if (s == now)
        return;

    now = s;
    now.offLook = juce::jlimit (0, (int) numOffLooks - 1, now.offLook);
    now.show = juce::jlimit (0, 2, now.show);
    now.page = juce::jlimit (0, numDemoPages() - 1, now.page);
    for (auto& b : now.bench)
        b = juce::jlimit (0, numPedalSwitches - 1, b);

    hasSent.fill (false);       // everything goes out afresh
    sentShow = sentPage = -1;

    if (! now.send)
        output.reset();         // off: nothing stays open
}

void SwitchScreens::follow (AudioEngine& e, PedalInput& p)
{
    engine = &e;
    pedal = &p;
    startTimer (50);
}

void SwitchScreens::timerCallback()
{
    if (engine != nullptr && pedal != nullptr)
        update (readRig (*engine, *pedal));
}

void SwitchScreens::lookForController()
{
    const auto time = juce::Time::getMillisecondCounter();

    if (output != nullptr || ! realDevices || time - lastLook < 2000)
        return;

    lastLook = time;

    // The Teensy's own name, and the name the kick pad's program gives it.
    for (const auto& device : juce::MidiOutput::getAvailableDevices())
        if (device.name.containsIgnoreCase ("teensy") || device.name.containsIgnoreCase ("gig rig controller"))
            if (auto opened = juce::MidiOutput::openDevice (device.identifier))
            {
                output = std::move (opened);
                output->startBackgroundThread();        // sending never holds this thread up
                outputName = device.name;
                hasSent.fill (false);
                sentShow = sentPage = -1;
                return;
            }
}

void SwitchScreens::send (const uint8_t* body, int length)
{
    if (length <= 0)
        return;

    const auto message = juce::MidiMessage::createSysExMessage (body, length);

    if (sendMidi != nullptr)
    {
        sendMidi (message);
    }
    else if (output != nullptr)
    {
        juce::MidiBuffer buffer;
        buffer.addEvent (message, 0);
        output->sendBlockOfMessages (buffer, juce::Time::getMillisecondCounterHiRes(), 44100.0);
    }
    else
    {
        return;
    }

    ++sent;
    bytesSent += length + 2;
}

void SwitchScreens::update (const Inputs& in)
{
    followUndo (in);

    for (int sw = 0; sw < numPedalSwitches; ++sw)
        faces[(size_t) sw] = faceFor (sw, in);

    if (! now.send)
        return;                 // off: no device, no messages

    if (sendMidi == nullptr)
    {
        lookForController();

        if (output == nullptr)
            return;
    }

    uint8_t body[maxMessage];
    const auto time = juce::Time::getMillisecondCounter();
    ++ticks;

    // "Are you there?" every two seconds, with what to show. It also tells the controller the app is alive.
    if (sentShow != now.show || sentPage != now.page || time - lastHello >= 2000 || lastHello == 0)
    {
        send (body, encodeShow (now.show, now.page, body));
        send (body, encodeHello (body));
        sentShow = now.show;
        sentPage = now.page;
        lastHello = juce::jmax ((juce::uint32) 1, time);
    }

    // Only as many screens as the controller says it has (three on the bench, until it has answered).
    int screens = 3;
    {
        const juce::ScopedLock sl (statusLock);
        if (everAnswered)
            screens = juce::jlimit (1, (int) maxScreens, status.screens);
    }

    // A tile goes out when it changes. One screen in turn is also sent again four times a second,
    // so a controller that was unplugged, or missed a message, is put right by itself.
    const int again = ticks % 5 == 0 ? (refreshNext++ % screens) : -1;

    for (int screen = 0; screen < screens; ++screen)
    {
        const int sw = switchForScreen (screen);

        if (sw < 0)
            continue;

        const auto& t = faces[(size_t) sw].tile;

        if (screen == again || ! hasSent[(size_t) screen] || ! sameTile (t, sentTiles[(size_t) screen]))
        {
            send (body, encodeTile (screen, t, body));
            sentTiles[(size_t) screen] = t;
            hasSent[(size_t) screen] = true;
        }
    }
}

//==============================================================================
void SwitchScreens::handleMidi (const juce::String&, const juce::MidiMessage& message)
{
    if (! message.isSysEx())
        return;

    Message m;

    if (decode (message.getSysExData(), message.getSysExDataSize(), m) && m.command == commandStatus)
    {
        const juce::ScopedLock sl (statusLock);
        status = m.status;
        statusAt = juce::Time::getMillisecondCounter();
        everAnswered = true;
    }
}

bool SwitchScreens::controllerAnswering() const
{
    const juce::ScopedLock sl (statusLock);
    return everAnswered && juce::Time::getMillisecondCounter() - statusAt < 5000;
}

Status SwitchScreens::controllerStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}

juce::String SwitchScreens::statusLine() const
{
    if (! now.send)
        return "Off. Nothing is opened or sent.";

    if (output == nullptr && sendMidi == nullptr)
        return "On, but the board's controller is not plugged in (no MIDI device with Teensy or Gig Rig Controller in its name).";

    const auto name = outputName.isNotEmpty() ? "\"" + outputName + "\"" : juce::String ("the controller");

    if (! controllerAnswering())
        return "Sending to " + name + ", but it has not answered. It needs the newest controller program (GigRig-Controller-Test.hex from the Releases page).";

    const auto s = controllerStatus();
    return "Sending to " + name + ", and it is answering: controller program " + juce::String (s.program) + ", " + juce::String (s.screens)
         + (s.screens == 1 ? " screen, " : " screens, ") + juce::String (s.tilesHeard) + " tiles taken in.";
}

juce::String SwitchScreens::reportText() const
{
    juce::String t;
    t << "=== Switch screens (the small screens on the board's footswitches) ===\n";
    t << statusLine() << "\n";
    t << "Messages sent: " << sent << " (" << bytesSent << " bytes).\n";
    t << "Off look for effect switches: " << offLookName (now.offLook) << ". The screens show: "
      << (now.show == showDemo ? juce::String ("the sample pages, turning by themselves")
          : now.show == showDemoPage ? "sample page " + juce::String (now.page + 1) + " (" + demoPageName (now.page) + ")"
                                     : juce::String ("what the rig is doing")) << ".\n";
    t << "Bench screens: ";
    for (int i = 0; i < (int) now.bench.size(); ++i)
        t << (i > 0 ? ", " : "") << "screen " << (i + 1) << " = " << pedalSwitchName (now.bench[(size_t) i]);
    t << ".\nWhat each switch's screen shows now:\n";

    for (int sw = 0; sw < numPedalSwitches; ++sw)
        t << "  " << pedalSwitchName (sw) << ": " << faces[(size_t) sw].words << "\n";

    return t;
}

//==============================================================================
juce::File SwitchScreens::stateFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("GigRig").getChildFile ("switch-screens.txt");
}

juce::String SwitchScreens::stateText() const
{
    juce::String t;
    t << "# The footswitch screens (the Switch screens tab). The words on the left are never renamed.\n";
    t << "send = " << (now.send ? 1 : 0) << "\n";
    t << "off look = " << (now.offLook == lookWhiteOutline ? "white outline" : now.offLook == lookFullWhite ? "full white" : "chain border") << "\n";
    t << "show = " << (now.show == showDemo ? "samples" : now.show == showDemoPage ? "sample page" : "rig") << "\n";
    t << "sample page = " << (now.page + 1) << "\n";

    for (int i = 0; i < (int) now.bench.size(); ++i)
        t << "bench screen " << (i + 1) << " = " << (now.bench[(size_t) i] + 1) << "\n";

    return t;
}

void SwitchScreens::loadStateText (const juce::String& text)
{
    auto s = now;

    for (const auto& line : juce::StringArray::fromLines (text))
    {
        if (line.trimStart().startsWithChar ('#') || ! line.containsChar ('='))
            continue;

        const auto key = line.upToFirstOccurrenceOf ("=", false, false).trim().toLowerCase();
        const auto value = line.fromFirstOccurrenceOf ("=", false, false).trim().toLowerCase();

        if (key == "send")              s.send = value.getIntValue() != 0;
        else if (key == "off look")     s.offLook = value == "white outline" ? lookWhiteOutline : value == "full white" ? lookFullWhite : lookChainBorder;
        else if (key == "show")         s.show = value == "samples" ? showDemo : value == "sample page" ? showDemoPage : showRig;
        else if (key == "sample page")  s.page = value.getIntValue() - 1;
        else if (key.startsWith ("bench screen "))
        {
            const int i = key.fromLastOccurrenceOf (" ", false, false).getIntValue() - 1;
            if (i >= 0 && i < (int) s.bench.size())
                s.bench[(size_t) i] = value.getIntValue() - 1;
        }
    }

    setSettings (s);
}

void SwitchScreens::saveState() const
{
    if (! memoryOnDisk.load())
        return;

    const auto file = stateFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (stateText());
}

} // namespace gigrig
