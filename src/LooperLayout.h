#pragma once

#include "Looper.h"

namespace gigrig::layout
{

/** A song's own looper layout as lines in its song file (built 27 Nov 2026 for the remote screen's
    arrangement builder; the guide is docs/remote.md and docs/songs.md).

    Until this, a looper layout was one of the built-in songs (TestSongs.cpp, copied from the
    RC-600 backup) and a song file could only borrow one by name ("looper = Wicked Game"). Now a
    song file may carry the whole layout itself, written by the builder on the tablet or phone:

        looper beats = 4
        looper order = record play overdub
        looper new track = waits for the bar
        looper track 1 = acoustic + stomp / free / in place / stops at once / level 100
        looper track 2 = bass / 4 bars / from its first bar / stops at the end of its loop / level 96
        looper switch 1.1 = track 1 / record play, hold undo
        looper switch 1.9 = all start stop
        looper link = switch 1.3 / track 1 / play stop
        looper link = track 1 plays / track 3 / record play

    "looper switch 1.3" is bank 1, switch 3. A link is something extra that happens: when a switch
    is pressed, or when a track starts to record or to play.

    THE WORDS ARE SAVED ON TOM'S DISK: never rename one, only add. Anything the words cannot say
    (an RC-600 function the looper does not copy) is kept as its RC-600 number: "rc 201".

    Inside the engine a layout is still a SongSetup holding RC-600 function numbers, exactly as
    before, so the looper itself did not change for this. All of this is pure text work, checked
    by src/RemoteSelfTest.cpp. */

/** One thing a switch can be given, for the builder's list. */
struct FunctionChoice
{
    const char* words;      // as saved in a song file: "record play, hold undo"
    const char* label;      // as shown: "Record, play, overdub. Hold: undo"
    int index;              // the RC-600's per-track function (0 = REC/PLAY1 ...), or -1 for one that is not about a track
    int code;               // not about a track: the RC-600 function number itself
};

const std::vector<FunctionChoice>& functionChoices();

/** The RC-600 function number for a per-track choice on a track (0 to 5, or -1 = the current track). */
int codeFor (const FunctionChoice&, int track) noexcept;

/** "track 3 / record play, hold undo", "all start stop", "nothing", "rc 201". */
juce::String functionWords (int rcCode);
/** False when the words are not understood. */
bool functionFromWords (const juce::String& words, int& rcCode);

/** "switch 1.3 / track 1 / play stop". Empty for an assign that is off. */
juce::String linkWords (const LoopAssign&);
bool linkFromWords (const juce::String& words, LoopAssign&);

/** "acoustic + stomp", "nothing". */
juce::String sourceWords (int rcInputs);
bool sourcesFromWords (const juce::String& words, int& rcInputs);

/** The whole layout as song-file lines (each starts "looper "). */
juce::StringArray toLines (const SongSetup&);

/** Puts a file's layout lines onto a layout. Lines it cannot use are added to problems and the
    layout keeps what it had there. Returns true if any line was used. */
bool fromLines (const juce::StringArray& lines, SongSetup& onto, juce::StringArray& problems);

/** The same layout for the remote screen, as numbers (the screen has the list of choices):
    { name, beats, recThenPlay, quantise, tempo,
      tracks: [ { inputs, bars, stop, ret, level } x6 ], banks: [ [9 codes], [9 codes] ],
      links: [ { src, tgt } ... ] }   (src and tgt are the RC-600's assign numbers) */
juce::var toJson (const SongSetup&);
/** Returns an error for the screen, or an empty string. */
juce::String fromJson (const juce::var&, SongSetup& onto);

} // namespace gigrig::layout
