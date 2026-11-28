#pragma once

#include "AudioEngine.h"
#include "Pedal.h"
#include "SongBook.h"

#include <functional>

namespace gigrig
{

/** What the remote screen (the page in remote/, shown on a phone or tablet) can see and do
    (built 27 Nov 2026; the guide is docs/remote.md).

    This is the one place that turns the rig into JSON and JSON back into actions. It has no
    network in it (RemoteServer does that), so the automatic builds check it directly
    (src/RemoteSelfTest.cpp) and the page's demo mode can copy it.

    Everything here is for the message thread, except pedal(). */
class RemoteApi
{
public:
    RemoteApi (AudioEngine&, PedalInput&, SongBook&);

    /** Everything that changes only when somebody changes it: songs, the looper layout, the
        effects chains, the mixer. RemoteServer hands it out with a version number and the
        page fetches it again when that number moves on. */
    juce::var state();

    /** What moves by itself: the looper's tracks, the beat, the meters. About ten times a second. */
    juce::var live();

    /** Does one thing the page asked for: { "op": "mix.ch", "c": 0, "key": "fader", "value": -6 }.
        Returns { "ok": true, "note": "..." } or { "ok": false, "error": "..." }; some ops add
        their own answer (layout.of returns "layout"). The ops are listed in docs/remote.md. */
    juce::var command (const juce::var&);

    /** A touch on one of the page's switches: the same as a foot on the real one. Any thread but the audio thread. */
    void pedal (int sw, bool isDown);

    /** The whole report (the Copy report button's text), supplied by the window. */
    std::function<juce::String()> wholeReport;

    /** Called after a command changed the rig, so the window's own tabs can follow. */
    std::function<void()> onRigChanged;

    /** How many commands have been carried out, for the report. */
    int commandsDone() const noexcept           { return done; }

private:
    juce::var chainJson (const fx::ChainModel&);
    juce::var typesJson();
    juce::var mixerJson();
    juce::var breakJson();
    juce::String doBreak (const juce::String& op, const juce::var& c, juce::var& answer);
    juce::var effectsJson();
    juce::String doRecord (const juce::String& op, const juce::var& command, juce::var& answer);
    juce::String doMixer (const juce::String& op, const juce::var& c, juce::var& answer);
    juce::String doEffects (const juce::String& op, const juce::var& c, juce::var& answer);
    juce::String doSongs (const juce::String& op, const juce::var& c, juce::var& answer);

    AudioEngine& engine;
    PedalInput& pedalInput;
    SongBook& book;
    int done = 0;
    void readLists();
    bool listsRead = false;
    juce::uint32 listsAt = 0;
    juce::StringArray presetList, actList, venueList, monitorList;
    juce::StringArray typesSeen;        // effect types the last state() used, so types are only described once each

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RemoteApi)
};

} // namespace gigrig
