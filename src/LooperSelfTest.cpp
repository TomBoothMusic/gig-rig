#include "SelfTest.h"
#include "Looper.h"

#include <cmath>
#include <map>

// Checks for stage 2 (press, hold, double-press) and stage 3 (the looper),
// run by the automatic builds. The looper is driven the way the audio device
// drives it, with a pretend instrument whose every sample is known, so its
// timing can be checked to the sample.

namespace gigrig
{

namespace
{
    using Check = std::function<void (bool, const juce::String&)>;

    int songIndex (const char* name)
    {
        for (int i = 0; i < numBuiltInSongs(); ++i)
            if (builtInSong (i).title() == name)
                return i;

        return -1;
    }

    /** A looper with a pretend world round it: an instrument, the delay into the
        computer, the delay back out, and a recording of what is heard. */
    struct Rig
    {
        static constexpr int inputs = 16;

        Looper looper;
        double rate;
        int block;
        int delayIn, delayOut;
        juce::int64 time = 0;                 // engine time, which is also real time here
        std::vector<float> heard;             // the loop playback as heard, by real time
        std::function<float (juce::int64, int)> instrument;   // (real time, input socket) -> sample
        std::vector<LoopLogEntry> log;

        Rig (double sampleRate, int blockSize, int in, int out, const char* song, bool fixedTempo = false)
            : rate (sampleRate), block (blockSize), delayIn (in), delayOut (out)
        {
            looper.prepare (rate, 30.0);
            looper.setCompensation (in + out);

            LooperOptions options;
            options.fixedTempo = fixedTempo;
            looper.setOptions (options);
            looper.setSong (builtInSong (songIndex (song)));

            // Every socket carries a different, noise-like signal that is the same every time it is asked for.
            instrument = [] (juce::int64 t, int socket)
            {
                auto x = (juce::uint64) (t + 1000003) * 0x9E3779B97F4A7C15ull + (juce::uint64) (socket + 1) * 0xBF58476D1CE4E5B9ull;
                x ^= x >> 31;  x *= 0x94D049BB133111EBull;  x ^= x >> 29;
                return (float) ((double) (x >> 40) / (double) (1 << 24) - 0.5) * 0.25f;
            };

            run (block * 4);
        }

        void run (juce::int64 samples)
        {
            std::vector<std::vector<float>> in ((size_t) inputs, std::vector<float> ((size_t) block)), out ((size_t) numLoopOutputs, std::vector<float> ((size_t) block));
            std::vector<const float*> inPointers;
            std::vector<float*> outPointers;

            for (auto& c : in)   inPointers.push_back (c.data());
            for (auto& c : out)  outPointers.push_back (c.data());

            for (const auto end = time + samples; time < end; time += block)
            {
                for (int ch = 0; ch < 10; ++ch)
                    for (int i = 0; i < block; ++i)
                        in[(size_t) ch][(size_t) i] = instrument (time + i - delayIn, ch);

                for (auto& c : out)
                    std::fill (c.begin(), c.end(), 0.0f);

                looper.process (inPointers.data(), inputs, outPointers.data(), block);

                const auto needed = (size_t) (time + block + delayOut);

                if (heard.size() < needed)
                    heard.resize (needed, 0.0f);

                // Returns 5 and 6 carry the same thing in the default routing; listen to 5.
                for (int i = 0; i < block; ++i)
                    heard[(size_t) (time + i + delayOut)] = out[4][(size_t) i];

                LoopLogEntry entries[64];
                const auto count = looper.takeLog (entries, 64);
                log.insert (log.end(), entries, entries + count);
            }
        }

        void runSeconds (double seconds)    { run ((juce::int64) (seconds * rate / block) * block); }

        void send (PedalEvent::Kind kind, int sw, bool isDouble = false)
        {
            PedalEvent e;
            e.kind = kind;
            e.sw = sw;
            e.isDouble = isDouble;
            looper.pedal (e);
        }

        /** A quick press and release. Returns the time the press landed. */
        juce::int64 tap (int sw)
        {
            const auto at = time;
            send (PedalEvent::Kind::down, sw);
            run (block * 8);
            send (PedalEvent::Kind::up, sw);
            run (block);
            return at;
        }

        juce::int64 doubleTap (int sw)
        {
            send (PedalEvent::Kind::down, sw);
            run (block * 8);
            send (PedalEvent::Kind::up, sw);
            runSeconds (0.15);
            const auto at = time;
            send (PedalEvent::Kind::down, sw, true);
            run (block * 8);
            send (PedalEvent::Kind::up, sw);
            run (block);
            return at;
        }

        void hold (int sw)
        {
            send (PedalEvent::Kind::down, sw);
            runSeconds (2.0);
            send (PedalEvent::Kind::hold, sw);
            runSeconds (0.3);
            send (PedalEvent::Kind::up, sw);
            run (block);
        }

        TrackView track (int t) const       { return looper.snapshot().track[(size_t) t]; }
        juce::int64 length (int t) const    { return (juce::int64) std::llround (track (t).seconds * rate); }
        juce::int64 bar() const             { return (juce::int64) std::llround (looper.snapshot().barSeconds * rate); }

        static juce::int64 mod (juce::int64 value, juce::int64 length)     { return ((value % length) + length) % length; }

        /** What a loop of `socket`, pressed into record at `recStart` and `length` long, must sound
            like at real time t: every sound exactly a whole number of loops after it was played.

            Only the round trip (in + out) can be measured on a real mixer, not its two halves, so the
            looper lines its memory up on the round trip. The loop then holds what was played from
            `delayOut` after the press, for one loop length. Going round and round that is the same
            loop; it only moves where the 8 ms join sits. */
        float loop (int socket, juce::int64 recStart, juce::int64 length, juce::int64 t) const
        {
            return instrument (recStart + delayOut + mod (t - delayOut - recStart, length), socket);
        }

        /** True in the last 8 ms before a loop's join, where the blend is (plus a little margin). */
        bool nearJoin (juce::int64 recStart, juce::int64 length, juce::int64 t) const
        {
            return mod (t - delayOut - recStart, length) >= length - (juce::int64) (rate * 0.008) - 16;
        }

        /** The biggest difference between what was heard over [from, to) and what `expected` says it
            should be, leaving out any time `skip` says to. The time asked for must already have run. */
        float worst (juce::int64 from, juce::int64 to, const std::function<float (juce::int64)>& expected,
                     const std::function<bool (juce::int64)>& skip = nullptr) const
        {
            if (from < 0 || to > (juce::int64) heard.size() || to <= from)
                return 1000.0f;

            float worstSoFar = 0.0f;

            for (auto t = from; t < to; ++t)
                if (skip == nullptr || ! skip (t))
                    worstSoFar = juce::jmax (worstSoFar, std::abs (heard[(size_t) t] - expected (t)));

            return worstSoFar;
        }

        int count (LoopLogEntry::Kind kind, int trackIndex = -1) const
        {
            int n = 0;

            for (const auto& e : log)
                n += e.kind == kind && (trackIndex < 0 || e.track == trackIndex) ? 1 : 0;

            return n;
        }
    };

    constexpr float exact = 1.0e-6f;
}

void runLooperSelfTest (const Check& check);

void runLooperSelfTest (const Check& check)
{
    // =========================================================================
    // Stage 2: press, hold and double-press
    // =========================================================================
    {
        check (pedalSwitchForCC (1, 20) == 0 && pedalSwitchForCC (1, 28) == 8 && pedalSwitchForCC (1, 30) == 10,
               "CC 20 to 28 are switches 1 to 9, CC 29 and 30 are CTL3 and CTL4");
        check (pedalSwitchForCC (2, 20) == -1 && pedalSwitchForCC (1, 31) == -1 && pedalSwitchForCC (1, 19) == -1,
               "other channels and other CC numbers are not switches");
        check (pedalSwitchName (0) == "Switch 1" && pedalSwitchName (9) == "CTL3" && pedalSwitchRole (6) == "bank",
               "switches have their names");

        GestureDecoder d;
        PedalEvent e, holds[numPedalSwitches];

        check (d.press (2, 10.0, e) && e.kind == PedalEvent::Kind::down && ! e.isDouble && e.sw == 2, "a press is reported the moment the switch goes down");
        check (! d.press (2, 10.01, e), "the same press arriving twice is reported once");
        check (d.pollHolds (11.9, holds, numPedalSwitches) == 0, "1.9 seconds is not a hold yet");
        check (d.pollHolds (12.0, holds, numPedalSwitches) == 1 && holds[0].kind == PedalEvent::Kind::hold && holds[0].sw == 2, "2 seconds held is a hold");
        check (d.pollHolds (12.5, holds, numPedalSwitches) == 0, "a hold is reported once");
        check (d.release (2, 12.6, e) && e.kind == PedalEvent::Kind::up, "the release is reported");
        check (! d.release (2, 12.7, e), "a release with no press is ignored");

        check (d.press (2, 20.0, e) && ! e.isDouble, "a press long after the last one is a single press");
        d.release (2, 20.1, e);
        check (d.press (2, 20.3, e) && e.isDouble, "a second press 0.3 s after the first is a double-press");
        d.release (2, 20.4, e);
        check (d.press (2, 20.6, e) && ! e.isDouble, "a third quick press starts again as a single press");
        d.release (2, 20.7, e);
        check (d.press (2, 21.2, e) && ! e.isDouble, "0.6 s apart is two single presses");
        d.release (2, 21.3, e);
        d.press (3, 21.4, e);
        check (! e.isDouble, "quick presses on two different switches are not a double-press");
        d.release (3, 21.5, e);
        d.press (4, 30.0, e);
        d.release (4, 30.5, e);
        check (d.pollHolds (33.0, holds, numPedalSwitches) == 0, "a switch that was let go is never a hold");

        d.setDoubleWindow (0.3);
        d.press (5, 40.0, e);
        d.release (5, 40.1, e);
        check (d.press (5, 40.35, e) && ! e.isDouble, "the double-press window can be made shorter");
    }

    // =========================================================================
    // Reading the RC-600's numbers
    // =========================================================================
    {
        const auto recPlay2 = decodeRcFunction (2);
        check (recPlay2.push == LoopAction::recPlay && recPlay2.hold == LoopAction::undoRedo && recPlay2.click == LoopAction::none && recPlay2.track == 0,
               "RC-600 function 2 is track 1 rec/play with hold to undo");
        const auto stop4 = decodeRcFunction (37);
        check (stop4.push == LoopAction::stop && stop4.hold == LoopAction::clear && stop4.track == 1, "function 37 is track 2 stop with hold to clear");
        const auto recPlay4 = decodeRcFunction (134);
        check (recPlay4.push == LoopAction::recPlay && recPlay4.hold == LoopAction::undoOrClear && recPlay4.click == LoopAction::stop && recPlay4.track == 5,
               "function 134 is track 6 rec/play with hold to undo or clear and double-press to stop");
        check (decodeRcFunction (171).push == LoopAction::undoRedo && decodeRcFunction (171).track == -1, "function 171 is undo/redo on the current track");
        check (decodeRcFunction (186).push == LoopAction::allStartStop && decodeRcFunction (193).push == LoopAction::bankToggle, "186 is all start/stop and 193 is the bank switch");
        check (describeFunction (decodeRcFunction (28)) == "T2 rec/play" && describeFunction (decodeRcFunction (196)) == "Effect (later)", "functions read in plain words");

        const auto a = decodeRcAssign (15, 35);
        check (a.source == LoopAssign::Source::pedal && a.bank == 0 && a.sw == 2 && a.action == LoopAction::stop && a.targetTrack == 3,
               "assign source 15, target 35 is: switch 3 in bank 1 also stops track 4");
        const auto b = decodeRcAssign (6, 23);
        check (b.source == LoopAssign::Source::trackPlayStop && b.sourceTrack == 0 && b.action == LoopAction::playStop && b.targetTrack == 2,
               "assign source 6, target 23 is: track 1 finishing a recording toggles track 3");
        check (decodeRcAssign (42, 34).sw == 9 && decodeRcAssign (42, 34).action == LoopAction::playStop, "assign source 42 is CTL3");
        check (songIndex ("Standard layout") >= 0 && songIndex ("One press per track") >= 0 && songIndex ("Dirty Slap") >= 0 && songIndex ("Dreams") >= 0 && songIndex ("Songbird") >= 0, "the test songs are built in");
        check (describeSong (builtInSong (songIndex ("Dreams"))).contains ("Switch 4 (bank 1) also does: T3 stop"), "a song's linked actions read in plain words");
    }

    // =========================================================================
    // The first loop: it sets the tempo, and comes back exactly where it was played
    // =========================================================================
    {
        Rig rig (48000.0, 64, 150, 250, "Standard layout");
        const auto f = [&] (juce::int64 t) { return rig.instrument (t, 6); };      // track 1 hears socket 7 (acoustic and stomp)

        const auto start = rig.tap (0);
        check (rig.track (0).state == TrackState::recording, "a press on an empty track starts recording at once");
        check (rig.looper.snapshot().running && ! rig.looper.snapshot().tempoKnown, "the tempo is not known until the first loop closes");

        rig.run (64 * 3000 - (rig.time - start));       // exactly 192,000 samples: 4.000 s
        const auto stop = rig.tap (0);
        const auto length = rig.length (0);

        check (rig.track (0).state == TrackState::playing, "the second press closes the loop and plays it");
        check (stop - start == 192000 && length == 192000, "the loop is exactly as long as the time between the two presses");
        check (rig.bar() == 96000 && std::abs (rig.looper.snapshot().bpm - 120.0) < 0.001, "4 seconds with no preset is taken as 2 bars at 120 BPM (inside 80 to 160)");
        check (std::abs (rig.track (0).bars - 2.0) < 1.0e-9, "and the track shows as 2 bars");

        rig.runSeconds (13.0);
        const int fadeIn = 96 + 8;                      // the 2 ms start ramp at 48 kHz
        const auto loop = [&] (juce::int64 t) { return rig.loop (6, start, length, t); };
        const auto join = [&] (juce::int64 t) { return rig.nearJoin (start, length, t); };

        check (rig.worst (stop + 250 + fadeIn, stop + length, [&] (juce::int64 t) { return f (t - length); }, join) < exact,
               "first time round, every sample comes back exactly one loop after it was played");
        check (rig.worst (stop + length, stop + 3 * length, loop, join) < exact,
               "and round again twice more: still exact, with only the 8 ms blend at the join left out");
        check (rig.worst (stop + length + 250, stop + 2 * length - 400, [&] (juce::int64 t) { return f (t - 2 * length); }) < exact,
               "what was played in the first pass is heard two loops later, to the sample");
        check (rig.worst (stop, stop + 250, [] (juce::int64) { return 0.0f; }) < exact,
               "nothing can come out before the output delay has passed (4 to 5 ms on the CQ): that is physics, not a bug");

        // The blend: the last 8 ms before the join turn into what was heard just before the loop began.
        const auto joinAt = start + 3 * length + 250;
        check (std::abs (rig.heard[(size_t) (joinAt - 1)] - f (start + 250 - 1)) < 0.002f,
               "the last sample before the join has become the sample from just before the loop's first");
        check (std::abs (rig.heard[(size_t) (joinAt - 384 - 1)] - loop (joinAt - 384 - 1)) < exact,
               "and 8 ms before the join the blend has not begun");
    }

    // ---- no click at the loop point, with a real-world mismatch -----------------
    {
        Rig rig (48000.0, 64, 150, 250, "Standard layout");
        // A steady low note that does not fit the loop: with no crossfade it would jump at the loop point.
        rig.instrument = [] (juce::int64 t, int) { return 0.5f * (float) std::sin ((double) t * 2.0 * juce::MathConstants<double>::pi * 110.3 / 48000.0); };

        const auto start = rig.tap (0);
        rig.run (64 * 2531 - (rig.time - start));
        const auto stop = rig.tap (0);
        const auto length = rig.length (0);
        rig.runSeconds (12.0);

        float biggestStep = 0.0f, stepAtLoopPoint = 0.0f;

        for (auto t = stop + 2000; t < stop + 3 * length; ++t)
        {
            const auto step = std::abs (rig.heard[(size_t) t] - rig.heard[(size_t) (t - 1)]);
            biggestStep = juce::jmax (biggestStep, step);

            if ((t - stop - 250) % length == 0)
                stepAtLoopPoint = juce::jmax (stepAtLoopPoint, step);
        }

        const auto naturalStep = 0.5f * (float) (2.0 * juce::MathConstants<double>::pi * 110.3 / 48000.0);
        check (biggestStep < naturalStep * 2.5f && stepAtLoopPoint < naturalStep * 1.5f,
               "no click at the loop point: the sound never jumps by more than the note itself moves");
        check (length == (64 * 2531 / 2) * 2, "an odd length is trimmed by at most one sample per bar so every bar is a whole number of samples");
    }

    // =========================================================================
    // A second track: waits for the bar, ends on the bar, stays lined up
    // =========================================================================
    {
        Rig rig (48000.0, 64, 150, 250, "Standard layout");

        const auto start = rig.tap (0);
        rig.run (192000 - (rig.time - start));
        rig.tap (0);                                    // 2 bars of 96,000
        const auto bar = rig.bar();

        // Press for track 2 a third of the way into a bar: it must wait for the next bar.
        rig.run ((start + 3 * bar + 32000) - rig.time);
        rig.tap (2);
        check (rig.track (1).state == TrackState::armed, "a new track pressed mid-bar waits for the next bar");
        rig.run ((start + 4 * bar + 64) - rig.time);
        check (rig.track (1).state == TrackState::recording, "and starts recording on the bar");

        // Press to finish a little early (0.2 s before the bar): it records on to the bar.
        rig.run ((start + 5 * bar - 9600) - rig.time);
        rig.tap (2);
        check (rig.track (1).state == TrackState::closing, "pressed a little early, it keeps recording to the bar");
        rig.run ((start + 5 * bar + 64) - rig.time);
        check (rig.track (1).state == TrackState::playing && rig.length (1) == bar, "and closes as exactly 1 bar: shorter than the first loop, fitting evenly inside it");

        rig.runSeconds (13.0);
        const auto t2Start = start + 4 * bar, from = start + 7 * bar;
        const auto guitar = [&] (juce::int64 t) { return rig.loop (6, start, 2 * bar, t); };
        const auto bass = [&] (juce::int64 t) { return rig.loop (8, t2Start, bar, t); };     // track 2 hears socket 9

        check (rig.worst (from, from + 4 * bar, [&] (juce::int64 t) { return guitar (t) + bass (t); },
                          [&] (juce::int64 t) { return rig.nearJoin (start, 2 * bar, t) || rig.nearJoin (t2Start, bar, t); }) < exact,
               "both tracks play back exactly where they were played, bar after bar");

        // A longer track, finished a little late (0.15 s after the bar): it ends on the bar just gone.
        rig.run ((start + 12 * bar + 20000) - rig.time);
        rig.tap (1);                                    // track 1 stop (switch 2), so only track 2 keeps the time
        rig.run ((start + 13 * bar + 5000) - rig.time);
        rig.tap (4);                                    // track 3 rec/play (switch 5)
        rig.run ((start + 14 * bar + 64) - rig.time);
        check (rig.track (2).state == TrackState::recording, "a third track starts on the bar with the first track stopped");
        rig.run ((start + 17 * bar + 7200) - rig.time);
        rig.tap (4);
        check (rig.track (2).state == TrackState::playing && rig.length (2) == 3 * bar, "pressed a little late, the loop ends on the bar just gone: 3 bars");

        const auto t3Start = start + 14 * bar;
        const auto vocal = [&] (juce::int64 t) { return rig.loop (5, t3Start, 3 * bar, t); };
        rig.runSeconds (13.0);
        const auto later = start + 20 * bar;
        check (rig.worst (later, later + 3 * bar, [&] (juce::int64 t) { return bass (t) + vocal (t); },
                          [&] (juce::int64 t) { return rig.nearJoin (t2Start, bar, t) || rig.nearJoin (t3Start, 3 * bar, t); }) < exact,
               "the 1-bar and 3-bar tracks stay lined up with each other");
    }

    // =========================================================================
    // Preset bar counts: Dirty Slap (4, 4, free, 4, 12 and 2 bars)
    // =========================================================================
    {
        Rig rig (96000.0, 64, 300, 594, "Dirty Slap");
        const auto start = rig.tap (0);
        rig.run (609600 - (rig.time - start));          // 6.35 s: 4 bars at 151.2 BPM
        rig.tap (0);
        check (rig.bar() == 152400 && std::abs (rig.looper.snapshot().bpm - 151.18) < 0.01, "the first loop is taken as its 4 preset bars: 151.2 BPM");

        rig.tap (6);                                    // bank 2
        check (rig.looper.snapshot().bank == 1, "switch 7 changes to bank 2");
        rig.tap (4);                                    // track 6 rec/play: preset 2 bars
        const auto t6 = start + 5 * 152400;             // the next bar line
        check (rig.track (5).state == TrackState::armed, "track 6 waits for the next bar line, not for the 4-bar loop to come round");
        rig.run ((t6 + 64) - rig.time);
        check (rig.track (5).state == TrackState::recording, "track 6 starts on the bar");
        rig.run ((t6 + 2 * 152400 + 64) - rig.time);
        check (rig.track (5).state == TrackState::playing && rig.length (5) == 2 * 152400, "a track with 2 preset bars closes itself after 2 bars, with no second press");

        rig.runSeconds (8.0);
        const auto from = t6 + 3 * 152400;
        check (rig.worst (from, from + 400000, [&] (juce::int64 t) { return rig.loop (6, start, 609600, t) + rig.loop (5, t6, 304800, t); },
                          [&] (juce::int64 t) { return rig.nearJoin (start, 609600, t) || rig.nearJoin (t6, 304800, t); }) < exact,
               "at 96 kHz with the CQ's 9.3 ms round trip, both loops come back to the sample");
    }

    // ---- the other way to treat the first loop: the song's saved tempo ----------
    {
        Rig rig (48000.0, 64, 150, 250, "Dirty Slap", true);
        check (rig.looper.snapshot().tempoKnown && rig.bar() == 76190, "with the saved-tempo option, the tempo is known before anything is recorded");
        const auto start = rig.tap (0);
        rig.run ((start + 4 * 76190 + 64) - rig.time);
        check (rig.track (0).state == TrackState::playing && rig.length (0) == 4 * 76190, "and the first track closes itself after its 4 preset bars");
    }

    // =========================================================================
    // A hold on the very press that started a recording throws the take away
    // =========================================================================
    {
        Rig rig (48000.0, 64, 150, 250, "One press per track");
        rig.hold (0);
        check (rig.track (0).state == TrackState::empty && ! rig.track (0).hasAudio && rig.count (LoopLogEntry::Kind::recordingCancelled, 0) == 1,
               "holding the switch that just started a first recording cancels it and leaves the track empty");

        const auto start = rig.tap (0);
        rig.run (96000 - (rig.time - start));
        rig.tap (0);
        check (rig.track (0).state == TrackState::playing && rig.length (0) == 96000, "and the track records normally straight afterwards");

        rig.runSeconds (0.5);
        rig.hold (1);                                   // track 2 was only waiting for the bar line
        check (rig.track (1).state == TrackState::empty && rig.track (0).state == TrackState::playing,
               "holding a switch whose track was waiting for the bar cancels that too, and leaves the others playing");
    }

    // =========================================================================
    // Overdub, undo and redo, and a press that turns into a hold or a double-press
    // =========================================================================
    {
        Rig rig (48000.0, 64, 150, 250, "One press per track");
        const auto f = [&] (juce::int64 t) { return rig.instrument (t, 6); };
        const auto start = rig.tap (0);
        rig.run (96000 - (rig.time - start));
        const auto stop = rig.tap (0);                  // 1 bar of 2 s (120 BPM)
        const auto length = rig.length (0);
        check (length == 96000 && rig.bar() == 96000, "a 2 second first loop is 1 bar at 120 BPM");

        // First overdub: one whole pass and a bit.
        rig.run ((stop + 2 * length + 20000) - rig.time);
        const auto dubOn = rig.tap (0);
        check (rig.track (0).state == TrackState::overdubbing && rig.track (0).tentative, "a press while playing shows as overdubbing straight away");
        rig.runSeconds (0.5);
        check (rig.track (0).state == TrackState::overdubbing && ! rig.track (0).tentative && rig.track (0).hasOverdub, "and becomes a real overdub once the foot is off and no second press came");
        rig.run ((dubOn + 60000) - rig.time);
        const auto dubOff = rig.tap (0);
        check (rig.track (0).state == TrackState::playing, "the next press ends the overdub");
        rig.runSeconds (7.0);

        const auto layer1 = [&] (juce::int64 t)        // what the overdub added, by position in the loop
        {
            const auto played = dubOn + ((t - dubOn) % length);
            return played >= dubOn + 400 && played < dubOff - 400 ? f (played) : 0.0f;
        };
        const auto base = [&] (juce::int64 t) { return rig.loop (6, start, length, t); };

        auto from = rig.time - length - 500;
        const auto inside = [&] (juce::int64 t, juce::int64 a, juce::int64 b)   // away from the loop point and the overdub's own edges
        {
            const auto p = (t - start) % length, pa = (a - start) % length, pb = (b - start) % length;
            return p > 600 && p < length - 600 && std::abs (p - pa) > 800 && std::abs (p - pb) > 800;
        };
        const auto worstInside = [&] (juce::int64 a, juce::int64 b, const std::function<float (juce::int64)>& expected)
        {
            float w = 0.0f;
            for (auto t = from; t < from + length; ++t)
                if (inside (t, a, b))
                    w = juce::jmax (w, std::abs (rig.heard[(size_t) t] - expected (t)));
            return w;
        };

        check (worstInside (dubOn, dubOff, [&] (juce::int64 t) { return base (t) + layer1 (t); }) < exact,
               "the overdub sits exactly where it was played, including the half second before it was confirmed");

        // Hold to undo. The press that begins the hold must not record anything.
        rig.hold (0);
        check (rig.track (0).state == TrackState::playing && rig.track (0).overdubUndone, "holding the switch for 2 seconds undoes the overdub and stays playing");
        check (rig.count (LoopLogEntry::Kind::undo, 0) == 1, "one undo");
        rig.runSeconds (5.0);
        from = rig.time - 2 * length;
        check (rig.worst (from, from + length - 600, [&] (juce::int64 t) { const auto p = (t - start) % length; return p > 600 && p < length - 600 ? base (t) : rig.heard[(size_t) t]; }) < exact,
               "after the undo only the first recording is heard, and the hold itself added nothing");

        rig.hold (0);
        check (! rig.track (0).overdubUndone && rig.count (LoopLogEntry::Kind::redo, 0) == 1, "holding again brings the overdub back (redo)");
        rig.runSeconds (5.0);
        from = rig.time - 2 * length;
        check (worstInside (dubOn, dubOff, [&] (juce::int64 t) { return base (t) + layer1 (t); }) < exact, "and it is back exactly as it was");

        // A second overdub makes the first one permanent; undo then takes off only the second.
        rig.run (length - ((rig.time - start) % length) + 30000);
        const auto dub2On = rig.tap (0);
        rig.runSeconds (0.9);
        const auto dub2Off = rig.tap (0);
        rig.runSeconds (5.0);
        const auto layer2 = [&] (juce::int64 t)
        {
            const auto played = dub2On + ((t - dub2On) % length);
            return played >= dub2On + 400 && played < dub2Off - 400 ? f (played) : 0.0f;
        };
        const auto worstInside2 = [&] (const std::function<float (juce::int64)>& expected)
        {
            float w = 0.0f;
            for (auto t = from; t < from + length; ++t)
                if (inside (t, dubOn, dubOff) && inside (t, dub2On, dub2Off))
                    w = juce::jmax (w, std::abs (rig.heard[(size_t) t] - expected (t)));
            return w;
        };
        from = rig.time - 2 * length;
        check (worstInside2 ([&] (juce::int64 t) { return base (t) + layer1 (t) + layer2 (t); }) < exact, "a second overdub is added on top of the first");
        rig.hold (0);
        rig.runSeconds (5.0);
        from = rig.time - 2 * length;
        check (worstInside2 ([&] (juce::int64 t) { return base (t) + layer1 (t); }) < exact, "undo now takes off only the second overdub");

        // Double-press to stop: the first press of the pair must not leave an overdub behind.
        const auto undosBefore = rig.count (LoopLogEntry::Kind::undo, 0);
        rig.hold (0);                                   // redo, so the state is known
        rig.doubleTap (0);
        check (rig.track (0).state == TrackState::stopped, "a double-press stops the track");
        check (rig.count (LoopLogEntry::Kind::overdubCancelled, 0) == 1 && ! rig.looper.snapshot().running, "and the overdub its first press began is taken back");
        rig.runSeconds (1.0);
        check (rig.worst (rig.time - 20000, rig.time, [] (juce::int64) { return 0.0f; }) < exact, "a stopped track is silent");
        juce::ignoreUnused (undosBefore);

        // Hold on a stopped track clears it (REC/PLAY4).
        rig.hold (0);
        check (rig.track (0).state == TrackState::empty && ! rig.looper.snapshot().tempoKnown, "holding a stopped track's switch clears it, and with every track empty the tempo is forgotten");
    }

    // =========================================================================
    // Stop and start again from the top; all start/stop; undo of a whole recording
    // =========================================================================
    {
        Rig rig (48000.0, 256, 150, 250, "Standard layout");
        const auto f = [&] (juce::int64 t) { return rig.instrument (t, 6); };
        const auto start = rig.tap (0);
        rig.run (192000 - (rig.time - start));
        rig.tap (0);
        const auto length = rig.length (0);
        rig.runSeconds (3.0);

        rig.tap (1);                                    // switch 2: track 1 stop
        check (rig.track (0).state == TrackState::stopped && ! rig.looper.snapshot().running && rig.looper.snapshot().tempoKnown,
               "stop stops the track; the tempo is kept");
        rig.runSeconds (1.0);
        const auto again = rig.tap (0);
        check (rig.track (0).state == TrackState::playing, "rec/play on a stopped track plays it");
        rig.runSeconds (5.0);
        check (rig.worst (again + 250 + 200, again + length - 500, [&] (juce::int64 t) { return f (start + (t - again)); }) < exact,
               "started with everything stopped, the loop plays from its beginning");

        rig.tap (8);                                    // switch 9: all stop
        check (rig.track (0).state == TrackState::stopped, "all start/stop stops everything");
        rig.tap (8);
        check (rig.track (0).state == TrackState::playing, "and starts everything again");

        rig.tap (7);                                    // switch 8: undo/redo on the current track
        check (rig.track (0).undone, "undo with no overdub hides the whole recording");
        rig.runSeconds (1.0);
        check (rig.worst (rig.time - 20000, rig.time, [] (juce::int64) { return 0.0f; }) < exact, "and it is silent");
        rig.tap (7);
        check (! rig.track (0).undone, "redo brings it back");

        rig.tap (1);
        rig.hold (1);                                   // switch 2 held: clear (STOP4)
        check (rig.track (0).state == TrackState::empty, "holding the stop switch clears the track");
    }

    // =========================================================================
    // Linked actions (Dreams) and stopping at the end of the loop (Songbird)
    // =========================================================================
    {
        Rig rig (48000.0, 64, 150, 250, "Dreams");
        const auto start = rig.tap (0);
        rig.run (96000 - (rig.time - start));
        rig.tap (0);                                    // T1: 1 bar
        rig.runSeconds (0.6);

        const auto record = [&] (int sw)
        {
            rig.tap (sw);
            rig.run (rig.bar() - ((rig.time - start) % rig.bar()) + rig.bar() / 2);
            rig.tap (sw);
            rig.run (rig.bar());
        };

        record (3);                                     // switch 4 = track 4 (and, linked: stops tracks 3, 1, 6 and 5)
        check (rig.track (3).state == TrackState::playing, "Dreams: track 4 records and plays from switch 4");
        check (rig.track (0).state == TrackState::stopped, "Dreams: switch 4 also stops track 1 (linked action)");

        rig.tap (2);                                    // switch 3 = track 3 rec (linked: stops track 4, toggles track 1)
        check (rig.track (3).state == TrackState::stopped && rig.track (0).state == TrackState::playing,
               "Dreams: switch 3 also stops track 4 and starts track 1 again (linked actions)");
        check (rig.track (2).state == TrackState::armed || rig.track (2).state == TrackState::recording || rig.track (2).state == TrackState::closing, "Dreams: and its own track 3 goes into record");
    }

    // ---- the saved tempo as a guide: it says how many bars the first loop was ----
    {
        Rig rig (48000.0, 64, 150, 250, "Dreams");      // saved at 120.6 BPM, track 1 has no preset length
        const auto start = rig.tap (0);
        rig.run (64 * 4600 - (rig.time - start));       // 6.13 s: three bars played a touch fast, at 117.4 BPM
        rig.tap (0);
        check (std::abs (rig.track (0).bars - 3.0) < 1.0e-9 && std::abs (rig.looper.snapshot().bpm - 117.4) < 0.1,
               "with a saved tempo of 120.6, a 6.13 s first loop is 3 bars at 117.4 BPM, not 2 bars at half speed or 4");
    }

    {
        Rig rig (48000.0, 64, 150, 250, "CCR Acoustic");    // saved at 88.2 BPM
        const auto start = rig.tap (0);
        rig.run (64 * 4000 - (rig.time - start));           // 5.33 s: two bars at 90 BPM
        rig.tap (0);
        check (std::abs (rig.track (0).bars - 2.0) < 1.0e-9 && std::abs (rig.looper.snapshot().bpm - 90.0) < 0.1,
               "a 2-bar first loop comes out at the song's real tempo (90 BPM), not half of it");
    }

    {
        Rig rig (48000.0, 64, 150, 250, "Songbird");
        const juce::int64 bar = 48000;
        const auto start = rig.tap (0);
        rig.run (bar - (rig.time - start));
        rig.tap (0);                                    // track 1: 1 preset bar, sets the tempo
        check (rig.bar() == bar, "Songbird: track 1's single preset bar sets the tempo");

        rig.runSeconds (0.25);
        rig.tap (2);                                    // track 3: 17 preset bars, stops at the end of its loop
        rig.run ((start + 2 * bar + 64) - rig.time);
        check (rig.track (2).state == TrackState::recording, "Songbird: track 3 starts on the bar");
        rig.run ((start + 19 * bar + 64) - rig.time);
        check (rig.track (2).state == TrackState::playing && rig.length (2) == 17 * bar, "Songbird: and closes itself after 17 bars");

        rig.runSeconds (3.0);
        rig.doubleTap (2);                              // double-press = stop; this track's stop mode is "end of the loop"
        check (rig.track (2).state == TrackState::stopping, "Songbird: stop on track 3 waits for the end of its loop");
        rig.run ((start + 36 * bar - 9600) - rig.time);
        check (rig.track (2).state == TrackState::stopping, "still playing just before the loop ends");
        rig.run (9600 + 640);
        check (rig.track (2).state == TrackState::stopped, "and stopped at the loop's end");
    }

    // =========================================================================
    // Five minutes: no drift
    // =========================================================================
    {
        Rig rig (96000.0, 64, 300, 594, "Standard layout");
        const auto start = rig.tap (0);
        rig.run (64 * 5777 - (rig.time - start));       // an awkward length: 3.851 s
        rig.tap (0);
        const auto length = rig.length (0);
        const auto bar = rig.bar();

        rig.tap (2);
        rig.run (bar - ((rig.time - start) % bar) + bar - 4800);
        rig.tap (2);
        rig.runSeconds (300.0);

        const auto t2 = start + length + bar;
        const auto from = (juce::int64) rig.heard.size() - 2 * length;
        check (rig.length (1) == bar, "the second track is 1 bar");
        check (rig.worst (from, from + length, [&] (juce::int64 t) { return rig.loop (6, start, length, t) + rig.loop (8, t2, bar, t); },
                          [&] (juce::int64 t) { return rig.nearJoin (start, length, t) || rig.nearJoin (t2, bar, t); }) < exact,
               "after 5 minutes at 96 kHz both loops are still exact to the sample: no drift");
    }
}

} // namespace gigrig
