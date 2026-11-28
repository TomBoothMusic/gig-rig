#include "AudioEngine.h"
#include "KickPad.h"
#include "PadMessages.h"

#include <cmath>
#include <functional>

// Checks for the kick pad test (26 Nov 2026), run by the automatic builds (GigRig --self-test):
// which devices count as the pad, what is counted for the report, that a hit starts the kick
// on the first sample of the next block, that harder hits are louder and have more click, that
// the built-in kick is the sound Tom described (deep, empty in the middle, a click on top), and
// that nothing ticks, runs on for ever or leaks out while the app is measuring.

namespace gigrig
{

namespace
{
    constexpr double rate = 96000.0;
    constexpr int block = 64;
    constexpr double twoPi = 6.283185307179586;

    /** Runs the pad for some time and returns everything it played (left only: both sides are the same). */
    std::vector<float> run (KickPad& pad, double seconds, bool quiet = false)
    {
        std::vector<float> out;
        const auto blocks = (int) std::ceil (seconds * rate / block);
        out.reserve ((size_t) (blocks * block));

        for (int b = 0; b < blocks; ++b)
        {
            float left[block] = {}, right[block] = {};
            pad.process (left, right, block, quiet);
            out.insert (out.end(), left, left + block);
        }

        return out;
    }

    std::vector<float> oneHit (KickPad& pad, int strength, double seconds = 1.0)
    {
        run (pad, 1.0);     // whatever was sounding has ended
        pad.screenHit (strength);
        return run (pad, seconds);
    }

    float peak (const std::vector<float>& x, size_t from = 0, size_t to = (size_t) -1)
    {
        float p = 0.0f;

        for (size_t i = from; i < juce::jmin (to, x.size()); ++i)
            p = juce::jmax (p, std::abs (x[i]));

        return p;
    }

    bool allFinite (const std::vector<float>& x)
    {
        for (auto v : x)
            if (! std::isfinite (v))
                return false;

        return true;
    }

    /** How much of one frequency is in the first 'seconds' of a sound. */
    double level (const std::vector<float>& x, double hz, double seconds)
    {
        const auto n = juce::jmin (x.size(), (size_t) (seconds * rate));
        double re = 0.0, im = 0.0;

        for (size_t i = 0; i < n; ++i)
        {
            const auto window = 0.5 - 0.5 * std::cos (twoPi * (double) i / (double) n);
            re += window * x[i] * std::cos (twoPi * hz * (double) i / rate);
            im += window * x[i] * std::sin (twoPi * hz * (double) i / rate);
        }

        return std::sqrt (re * re + im * im) / (double) juce::jmax ((size_t) 1, n);
    }

    /** The loudest of a few frequencies in a range: a band, roughly. */
    double band (const std::vector<float>& x, double lowHz, double highHz, double seconds)
    {
        double best = 0.0;

        for (int i = 0; i <= 12; ++i)
            best = juce::jmax (best, level (x, lowHz * std::pow (highHz / lowHz, i / 12.0), seconds));

        return best;
    }

    float biggestJump (const std::vector<float>& x)
    {
        float j = 0.0f;

        for (size_t i = 1; i < x.size(); ++i)
            j = juce::jmax (j, std::abs (x[i] - x[i - 1]));

        return j;
    }
}

void runKickPadSelfTest (const std::function<void (bool, const juce::String&)>& check)
{
    // ---- which devices are the pad -------------------------------------------------
    check (looksLikeSpdOne ("SPD-1K"),                  "kick pad: 'SPD-1K' is an SPD::ONE");
    check (looksLikeSpdOne ("MIDIIN2 (SPD-1K)"),        "kick pad: a Windows-style SPD-1K port is an SPD::ONE");
    check (looksLikeSpdOne ("SPD-1K MIDI 1"),           "kick pad: the Linux name is an SPD::ONE");
    check (! looksLikeSpdOne ("RC-600"),                "kick pad: the RC-600 is not an SPD::ONE");
    check (! looksLikeSpdOne ("SPD-SX"),                "kick pad: an SPD-SX is not taken for an SPD::ONE");

    // ---- strength to loudness ------------------------------------------------------
    check (std::abs (kickGainForStrength (127, 1.5f) - 1.0f) < 1.0e-6f, "kick pad: the hardest hit plays at full level");
    check (std::abs (kickGainForStrength (64, 1.0f) - 64.0f / 127.0f) < 1.0e-6f, "kick pad: touch 1 is a straight line");

    {
        bool rising = true, clickRising = true;

        for (int s = 2; s <= 127; ++s)
        {
            rising = rising && kickGainForStrength (s, 1.5f) > kickGainForStrength (s - 1, 1.5f);
            clickRising = clickRising && kickClickForStrength (s) > kickClickForStrength (s - 1);
        }

        check (rising,      "kick pad: every step of strength is a little louder than the one before");
        check (clickRising, "kick pad: every step of strength has a little more click than the one before");
        check (kickGainForStrength (20, 2.5f) < kickGainForStrength (20, 1.0f), "kick pad: a higher touch setting makes soft hits softer");
    }

    // ---- the sound itself ----------------------------------------------------------
    {
        // A fresh install starts on the sound Tom locked in (27 Nov 2026).
        KickPad fresh;
        check (std::abs (fresh.getLevel() - 0.8f) < 1.0e-6f && std::abs (fresh.getTouch() - 1.1f) < 1.0e-6f && std::abs (fresh.getClick() - 0.86f) < 1.0e-6f
                 && std::abs (fresh.getPitch() - 49.0f) < 1.0e-6f && std::abs (fresh.getLength() - 0.155f) < 1.0e-6f,
               "kick pad: a fresh install starts on Tom's settings (level 0.80, touch 1.10, click 0.86, pitch 49 Hz, length 0.155 s)");
        fresh.prepare (rate);
        fresh.setLevel (1.0f);
        check (peak (oneHit (fresh, 127)) < 1.0f, "kick pad: Tom's sound at full level does not clip on the hardest hit");
    }

    // The checks below measure the sound at fixed settings of their own, so they do not move when the starting settings do.
    const auto testSettings = [] (KickPad& k) { k.setLevel (0.8f); k.setTouch (1.5f); k.setClick (1.0f); k.setPitch (55.0f); k.setLength (0.10f); };

    KickPad pad;
    pad.prepare (rate);
    testSettings (pad);
    pad.setLevel (0.8f);

    {
        const auto silence = run (pad, 0.1);
        check (peak (silence) == 0.0f, "kick pad: no hit, no sound");
    }

    {
        pad.screenHit (100);
        const auto out = run (pad, 1.2);
        size_t first = 0;

        while (first < out.size() && out[first] == 0.0f)
            ++first;

        // The kick starts from nothing (so there is no tick), and is under way by the second sample.
        check (first <= 1 && std::abs (out[0]) < 0.05f, "kick pad: a hit starts the kick on the first sample of the next block (first sound at sample " + juce::String ((int) first) + ")");
        check (allFinite (out), "kick pad: the kick has no NaN or infinity");
        check (peak (out, (size_t) (0.95 * rate)) == 0.0f, "kick pad: the kick has ended within a second");
        check (peak (out, (size_t) (0.85 * rate)) < 0.01f && biggestJump ({ out.begin() + (int) (0.85 * rate), out.end() }) < 0.0005f, "kick pad: it fades to nothing, with no tick at the end");
    }

    {
        const auto soft = oneHit (pad, 25), medium = oneHit (pad, 75), hard = oneHit (pad, 127);
        check (peak (soft) > 0.01f && peak (soft) < peak (medium) && peak (medium) < peak (hard),
               "kick pad: soft, medium and hard hits are each louder than the last (" + juce::String (peak (soft), 3) + ", "
                 + juce::String (peak (medium), 3) + ", " + juce::String (peak (hard), 3) + ")");

        // Tom's kick: strong around 55 Hz, very little in the middle, a click up high.
        const auto deep = band (hard, 45.0, 70.0, 0.4), middle = band (hard, 300.0, 1200.0, 0.4);
        check (deep > middle * 20.0, "kick pad: the body is deep, with next to nothing in the middle (deep is "
                                       + juce::String (20.0 * std::log10 (deep / juce::jmax (1.0e-12, middle)), 1) + " dB above the middle)");
        check (level (hard, 55.0, 0.4) > level (hard, 110.0, 0.4) * 2.0, "kick pad: it sits at 55 Hz, under the bass guitar, not an octave up");

        const auto clickHard = band (hard, 2500.0, 5000.0, 0.008), clickSoft = band (soft, 2500.0, 5000.0, 0.008);
        check (clickHard > 0.002, "kick pad: a hard hit has a click a small speaker can play");

        // A soft hit is duller as well as quieter: its click has dropped by more than its body has.
        const auto bodyHard = level (hard, 55.0, 0.4), bodySoft = level (soft, 55.0, 0.4);
        check (clickSoft / bodySoft < 0.5 * clickHard / bodyHard, "kick pad: a soft hit has less click for its size than a hard one");

        // Pitch and Length: the two things about the built-in kick Tom can move without a new build.
        pad.setPitch (75.0f);
        const auto higher = oneHit (pad, 127);
        check (level (higher, 75.0, 0.4) > level (higher, 55.0, 0.4) && level (hard, 55.0, 0.4) > level (hard, 75.0, 0.4), "kick pad: the Pitch setting moves where the body sits");
        pad.setPitch (55.0f);
        pad.setLength (0.04f);
        const auto tight = oneHit (pad, 127);
        check (peak (tight, (size_t) (0.3 * rate)) < 0.2f * peak (hard, (size_t) (0.3 * rate)) && peak (tight, (size_t) (0.5 * rate)) == 0.0f,
               "kick pad: a shorter Length setting makes a tighter kick");
        pad.setLength (0.10f);

        // Tom, 26 Nov 2026: he only ever used the bottom third of the old 0.05 to 0.40 s slider.
        pad.setLength (5.0f);
        const auto longest = pad.getLength();
        pad.setLength (0.0f);
        check (std::abs (longest - 0.2f) < 1.0e-6f && std::abs (pad.getLength() - 0.03f) < 1.0e-6f, "kick pad: the Length setting runs from 0.03 to 0.20 seconds");
        pad.setLength (0.10f);

        // The beater is a slap, not a note (the first one rang at 3.2 kHz and Tom heard a wooden block):
        // no one pitch in it stands far above its neighbours.
        double loudestNote = 0.0, sum = 0.0;
        int count = 0;

        for (double hz = 1500.0; hz <= 6000.0; hz += 125.0, ++count)
        {
            const auto l = level (hard, hz, 0.008);
            loudestNote = juce::jmax (loudestNote, l);
            sum += l;
        }

        check (loudestNote < 3.0 * sum / count, "kick pad: the beater is a slap with no note of its own (its loudest pitch is "
                                                  + juce::String (loudestNote * count / juce::jmax (1.0e-12, sum), 2) + " times the average)");

        // A drum head makes more than one note: while the hit is fresh there is a second one above the body, and it is gone soon after.
        const std::vector<float> early (hard.begin(), hard.begin() + (int) (0.03 * rate)), late (hard.begin() + (int) (0.15 * rate), hard.begin() + (int) (0.3 * rate));
        check (level (early, 55.0 * 1.59, 0.03) > 0.1 * level (early, 55.0, 0.03) && level (late, 55.0 * 1.59, 0.15) < 0.25 * level (late, 55.0, 0.15),
               "kick pad: the head's second note sounds with the hit and dies before the body does");

        pad.setClick (0.0f);
        const auto noClick = oneHit (pad, 127);
        check (band (noClick, 2500.0, 5000.0, 0.008) < clickHard * 0.1, "kick pad: the Click setting at 0 takes the click away");
        pad.setClick (2.0f);
        pad.setLevel (1.0f);
        const auto loudest = oneHit (pad, 127);
        check (peak (loudest) < 1.0f, "kick pad: everything turned up, the hardest hit still does not clip (" + juce::String (peak (loudest), 3) + ")");
        pad.setClick (1.0f);
        pad.setLevel (0.8f);
    }

    {
        bool rising = true;
        float last = 0.0f;

        for (int s = 1; s <= 127; s += 7)
        {
            const auto p = peak (oneHit (pad, s, 0.1));
            rising = rising && p > last;
            last = p;
        }

        check (rising, "kick pad: across the whole range, a harder hit is always louder");
    }

    {
        // A second hit while the first still sounds: the first fades out underneath, with no tick.
        run (pad, 1.0);
        pad.screenHit (127);
        auto out = run (pad, 0.06);
        pad.screenHit (127);
        const auto more = run (pad, 0.3);
        out.insert (out.end(), more.begin(), more.end());
        const auto alone = oneHit (pad, 127, 0.36);
        check (biggestJump (out) < biggestJump (alone) * 1.6f + 0.01f, "kick pad: a quick second hit does not tick");
        check (peak (out) < 1.0f, "kick pad: two quick hard hits do not clip");
    }

    {
        // While the app measures latency or checks for glitches the kick stays out of the way.
        run (pad, 1.0);
        pad.screenHit (127);
        const auto quiet = run (pad, 0.2, true);
        const auto after = run (pad, 0.2);
        check (peak (quiet) == 0.0f && peak (after) == 0.0f, "kick pad: silent while a measurement runs, and the hit is not played late afterwards");

        pad.setOn (false);
        const auto hitsBefore = pad.recentHits().size();
        pad.screenHit (127);
        check (peak (run (pad, 0.2)) == 0.0f && pad.recentHits().size() == juce::jmin ((size_t) KickPad::maxRecent, hitsBefore + 1),
               "kick pad: switched off, a hit is shown but not played");
        pad.setOn (true);
    }

    {
        // A recording in place of the built-in kick: half a second of a 200 Hz tone made at 48 kHz.
        std::vector<float> tone ((size_t) 24000);

        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.5f * (float) std::sin (twoPi * 200.0 * (double) i / 48000.0);

        pad.setSample (tone, 48000.0, "tone.wav");
        const auto out = oneHit (pad, 127, 1.0);
        size_t last = out.size();

        while (last > 0 && out[last - 1] == 0.0f)
            --last;

        check (std::abs ((int) last - 48000) < 200, "kick pad: a 48 kHz recording plays at the right speed at 96 kHz (lasts " + juce::String ((double) last / rate, 3) + " s)");
        check (level (out, 200.0, 0.4) > level (out, 400.0, 0.4) * 10.0, "kick pad: and at the right pitch");
        check (peak (oneHit (pad, 30, 0.3)) < peak (out) * 0.5f, "kick pad: a recording also plays softer for a soft hit");
        check (pad.reportText().contains ("recording \"tone.wav\""), "kick pad: the report names the recording");

        pad.setSample ({}, 48000.0, {});
        const auto back = oneHit (pad, 127, 0.4);
        check (level (back, 55.0, 0.4) > level (back, 200.0, 0.4) * 5.0, "kick pad: choosing the built-in kick again brings it back");
    }

    // ---- what arrives over USB, and what the report says about it --------------------
    {
        KickPad p;
        p.prepare (rate);
        testSettings (p);

        p.handleMidi ("RC-600", juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100));
        check (p.hitCount() == 0 && p.padDevice().isEmpty(), "kick pad: a note from the RC-600 is not a kick");
        check (p.reportText().contains ("Pad: none seen yet"), "kick pad: with no pad the report says so");

        for (auto strength : { 12, 40, 40, 90, 127 })
        {
            p.handleMidi ("SPD-1K", juce::MidiMessage::noteOn (10, 36, (juce::uint8) strength));
            p.handleMidi ("SPD-1K", juce::MidiMessage::noteOff (10, 36));
        }

        p.handleMidi ("SPD-1K", juce::MidiMessage::controllerEvent (10, 4, 20));
        p.handleMidi ("SPD-1K", juce::MidiMessage::midiClock());

        const auto text = p.reportText();
        check (p.padDevice() == "SPD-1K" && p.hitCount() == 5,             "kick pad: an SPD-1K is taken as the pad by its name, and its hits are counted");
        check (text.contains ("Softest strength 12, hardest 127"),         "kick pad: the report gives the softest and hardest hit");
        check (text.contains ("4 different strengths seen"),               "kick pad: the report says how many different strengths were seen");
        check (text.contains ("C2 (number 36)") && text.contains ("MIDI channel 10"), "kick pad: the report names the note and channel the pad uses");
        check (text.contains ("5 note-off, 1 other (CC 4 = 20 (channel 10))"), "kick pad: the report counts what else the pad sent");
        check (! text.contains ("EVERY HIT HAD THE SAME STRENGTH"),        "kick pad: hits of different strengths are not flagged");
        check (peak (run (p, 0.1)) > 0.1f,                                 "kick pad: a hit from the pad plays the kick");
        check (p.reportText().contains ("From a hit arriving to the kick starting"), "kick pad: the report times the wait for the audio");

        p.resetCounts();

        for (int i = 0; i < 6; ++i)
            p.handleMidi ("SPD-1K", juce::MidiMessage::noteOn (10, 36, (juce::uint8) 127));

        check (p.reportText().contains ("EVERY HIT HAD THE SAME STRENGTH"), "kick pad: a pad that sends one strength for every hit is called out");
        check (p.reportText().contains ("one stomp counted twice?): 5"),    "kick pad: hits a few milliseconds apart are counted as possible double triggers");

        // One stomp that reaches the pad twice: the weaker of the pair is not played, whichever came first.
        p.resetCounts();
        run (p, 1.0);
        p.handleMidi ("SPD-1K", juce::MidiMessage::noteOn (10, 60, (juce::uint8) 100));
        p.handleMidi ("SPD-1K", juce::MidiMessage::noteOn (10, 60, (juce::uint8) 40));
        const auto withBounce = run (p, 0.3);
        check (p.reportText().contains ("the weaker of the pair: 1") && withBounce == oneHit (p, 100, 0.3),
               "kick pad: a weaker second hit a few milliseconds after the first is a bounce, and is not played");
        juce::Thread::sleep (KickPad::doubleHitMs + 20);
        p.handleMidi ("SPD-1K", juce::MidiMessage::noteOn (10, 60, (juce::uint8) 40));
        p.handleMidi ("SPD-1K", juce::MidiMessage::noteOn (10, 60, (juce::uint8) 100));
        check (p.reportText().contains ("the weaker of the pair: 1") && peak (run (p, 0.3)) > 0.9f * peak (withBounce),
               "kick pad: a stronger second hit is played");
        juce::Thread::sleep (KickPad::doubleHitMs + 20);
        p.handleMidi ("SPD-1K", juce::MidiMessage::noteOn (10, 60, (juce::uint8) 40));
        juce::Thread::sleep (KickPad::doubleHitMs + 20);
        p.handleMidi ("SPD-1K", juce::MidiMessage::noteOn (10, 60, (juce::uint8) 30));
        check (p.reportText().contains ("the weaker of the pair: 1"), "kick pad: two soft stomps a normal time apart are both played");
        run (p, 1.0);

        // Learn pad: the next note from anything becomes the pad, and only that note.
        p.resetCounts();
        p.learnNextNote();
        p.handleMidi ("Some Other Pad", juce::MidiMessage::noteOn (1, 60, (juce::uint8) 80));
        check (p.padDevice() == "Some Other Pad" && p.padNote() == 60 && p.hitCount() == 1 && ! p.isLearning(), "kick pad: Learn pad takes the next note as the pad");
        p.handleMidi ("Some Other Pad", juce::MidiMessage::noteOn (1, 62, (juce::uint8) 80));
        p.handleMidi ("SPD-1K", juce::MidiMessage::noteOn (10, 36, (juce::uint8) 80));
        check (p.hitCount() == 1, "kick pad: after learning, other notes and other devices are ignored");

        p.setPad ({}, -1);
        p.handleMidi ("SPD-1K", juce::MidiMessage::noteOn (10, 36, (juce::uint8) 80));
        check (p.padDevice() == "SPD-1K", "kick pad: Forget pad goes back to finding the SPD::ONE by name");
    }

    // ---- in the engine: where the kick comes out ---------------------------------------
    for (const auto outs : { 16, 2 })
    {
        AudioEngine engine;
        engine.openSimulated (rate, block, outs, outs);

        std::vector<std::vector<float>> in ((size_t) outs, std::vector<float> ((size_t) block, 0.0f)), out = in;
        std::vector<const float*> inPtr;
        std::vector<float*> outPtr;

        for (int ch = 0; ch < outs; ++ch)
        {
            inPtr.push_back (in[(size_t) ch].data());
            outPtr.push_back (out[(size_t) ch].data());
        }

        engine.processBlock (inPtr.data(), outs, outPtr.data(), outs, block);
        bool silentBefore = true;

        for (const auto& ch : out)
            silentBefore = silentBefore && peak (ch) == 0.0f;

        engine.kick().screenHit (127);
        engine.processBlock (inPtr.data(), outs, outPtr.data(), outs, block);

        // Returns 5 and 6 leave a 16-channel device on outputs 13 and 14; a small device uses its first pair.
        const auto first = outs >= 16 ? 12 : 0;
        bool onlyThere = true;

        for (int ch = 0; ch < outs; ++ch)
            onlyThere = onlyThere && ((peak (out[(size_t) ch]) > 0.0f) == (ch == first || ch == first + 1));

        check (silentBefore && onlyThere, outs >= 16 ? "kick pad: on the mixer the kick plays on returns 5 and 6 only, and nothing plays before a hit"
                                                    : "kick pad: on a two-channel device the kick plays on its one pair");
    }

    //==============================================================================
    // The piezo pad test: knocks on an input socket become hits with a strength.
    {
        check (KickPad::piezoStrength (0.001f, -36.0f, -6.0f) == 0, "piezo pad: a spike under the threshold is not a hit");
        check (KickPad::piezoStrength (juce::Decibels::decibelsToGain (-35.9f), -36.0f, -6.0f) <= 3, "piezo pad: a spike just over the threshold is the softest hit");
        check (KickPad::piezoStrength (juce::Decibels::decibelsToGain (-6.0f), -36.0f, -6.0f) == 127 && KickPad::piezoStrength (1.0f, -36.0f, -6.0f) == 127,
               "piezo pad: a spike at the hardest-hit level, or over it, is strength 127");

        bool rising = true;
        int last = 0;

        for (float db = -35.0f; db <= -6.0f; db += 1.0f)
        {
            const auto s = KickPad::piezoStrength (juce::Decibels::decibelsToGain (db), -36.0f, -6.0f);
            rising = rising && s > last;
            last = s;
        }

        check (rising, "piezo pad: every decibel taller is a stronger hit");

        KickPad p;
        p.prepare (rate);
        testSettings (p);
        const int block = 64;

        // One block at a time, as the engine does: listen to the socket, then make the sound.
        const auto play = [&] (const std::vector<float>& socket, bool quiet = false)
        {
            std::vector<float> out (socket.size(), 0.0f);

            for (size_t at = 0; at + (size_t) block <= socket.size(); at += (size_t) block)
            {
                p.listen (socket.data() + at, block, quiet);
                p.process (out.data() + at, nullptr, block, quiet);
            }

            return out;
        };

        // A knock as a piezo gives it: a spike that rings down over a few milliseconds.
        const auto knock = [] (std::vector<float>& socket, size_t at, float height)
        {
            for (size_t i = 0; i < 2400 && at + i < socket.size(); ++i)
                socket[at + i] += height * std::exp (-(float) i / 300.0f) * std::cos ((float) i * 0.35f);
        };

        const size_t second = (size_t) rate;
        std::vector<float> socket (second, 0.0f);
        knock (socket, 9600, 0.3f);

        check (peak (play (socket)) == 0.0f, "piezo pad: with no socket chosen, a knock plays nothing");

        p.setPiezoSocket (10);
        const auto out = play (socket);
        size_t first = 0;

        while (first < out.size() && out[first] == 0.0f)
            ++first;

        // The spike starts at sample 9600; its top is known 2 ms later (192 samples); the kick starts with that block.
        const auto expected = (size_t) ((9600 + 192) / block) * (size_t) block;
        check (first >= expected && first <= expected + 1, "piezo pad: the kick starts with the block in which the spike's top is known, 2 ms after it began (sample "
                                                             + juce::String ((int) first) + ", expected " + juce::String ((int) expected) + ")");
        p.collect();
        check (p.hitCount() == 1 && p.recentHits().back().fromPiezo && std::abs (p.recentHits().back().peakDb - juce::Decibels::gainToDecibels (0.3f)) < 0.5f,
               "piezo pad: one knock is one hit, however long the plate rings, and its height is written down");
        check (p.reportText().contains ("Piezo pad: listening on socket 11") && p.reportText().contains ("Piezo hits: 1") && p.reportText().contains ("(piezo "),
               "piezo pad: the report says which socket, how many hits, and how tall each spike was");

        std::vector<float> three (second, 0.0f);
        knock (three, 4800, 0.03f);
        knock (three, 33600, 0.12f);
        knock (three, 62400, 0.45f);
        p.resetCounts();
        play (three);
        p.collect();
        const auto hits = p.recentHits();
        check (hits.size() == 3 && hits[0].strength < hits[1].strength && hits[1].strength < hits[2].strength,
               "piezo pad: a harder knock is a stronger hit");
        check (hits.size() == 3 && std::abs (hits[1].gapMs - 300) <= 1 && std::abs (hits[2].gapMs - 300) <= 1,
               "piezo pad: the time between knocks is measured from the sound itself, to the millisecond");

        const auto softOut = play ([&] { std::vector<float> s (second, 0.0f); knock (s, 4800, 0.03f); return s; }());
        const auto hardOut = play ([&] { std::vector<float> s (second, 0.0f); knock (s, 4800, 0.45f); return s; }());
        check (peak (softOut) > 0.0f && peak (softOut) < 0.5f * peak (hardOut), "piezo pad: a soft knock plays a softer kick");

        // A knock beside the pad: too small to count, but the report says how close it came.
        std::vector<float> beside (second, 0.0f);
        knock (beside, 4800, 0.008f);
        p.resetCounts();
        check (peak (play (beside)) == 0.0f, "piezo pad: a knock under the threshold plays nothing");
        p.collect();
        check (p.hitCount() == 0 && p.reportText().contains ("Tallest thing ignored") && p.reportText().contains ("-4"), "piezo pad: the report says how tall the tallest ignored knock was");

        p.setPiezoThresholdDb (-45.0f);
        check (peak (play (beside)) > 0.0f, "piezo pad: lowering the threshold lets that knock count");
        p.setPiezoThresholdDb (-36.0f);

        // A steady offset on the socket is not a hit.
        std::vector<float> offset (second, 0.2f);
        std::fill (offset.begin(), offset.begin() + 4800, 0.0f);
        for (size_t i = 4800; i < 4800 + 19200; ++i) offset[i] = 0.2f * (float) (i - 4800) / 19200.0f;
        p.resetCounts();
        play (offset);
        p.collect();
        check (p.hitCount() == 0, "piezo pad: a slow drift on the socket is not a hit");

        p.resetCounts();
        check (peak (play (socket, true)) == 0.0f, "piezo pad: silent while a measurement runs");
        p.collect();
        check (p.hitCount() == 0, "piezo pad: and nothing is counted then");

        p.setOn (false);
        check (peak (play (socket)) == 0.0f, "piezo pad: with the kick pad off a knock plays nothing");
        p.collect();
        check (p.hitCount() == 1, "piezo pad: but it is still counted");
    }

    // ---- the board's own pad: the piezo wired to the board's controller (28 Nov 2026) --------
    // There is no Teensy on a build machine, so the controller's own thinking (when a reading is
    // a stomp, how tall, how strong, what it sends, what it takes from the app) lives in
    // firmware/shared/PadMessages.h and runs here: the pretend controller below is those very
    // lines, given a pretend pin to read.
    {
        using namespace padmsg;
        const auto cc = [] (int number, int value) { return juce::MidiMessage::controllerEvent (channel, number, value); };
        const juce::String name = "Gig Rig Controller";

        struct PretendController
        {
            PadLevels levels;
            PadWatcher watcher;
            uint32_t clock = 0xffff0000u;       // a little before the controller's clock wraps to nought, so the wrap is checked too
            KickPad* app = nullptr;
            juce::String name;
            int hits = 0, knocks = 0, lastStrength = 0, lastHeight = 0, lastKnock = 0;
            uint32_t lastHitAt = 0;

            void toApp (const PadMessage* messages, int count) const
            {
                for (int i = 0; i < count && app != nullptr; ++i)
                    app->handleMidi (name, messages[i].isNote ? juce::MidiMessage::noteOn (channel, messages[i].number, (juce::uint8) messages[i].value)
                                                               : juce::MidiMessage::controllerEvent (channel, messages[i].number, messages[i].value));
            }

            /** Reads the pin as often as the controller's timer does, for this long. pin is given the time since the start, in millionths. */
            void run (double ms, const std::function<int (double)>& pin)
            {
                const auto start = clock;

                for (; (double) (uint32_t) (clock - start) < ms * 1000.0; clock += (uint32_t) readEveryMicros)
                {
                    const auto event = watcher.read (pin != nullptr ? pin ((double) (uint32_t) (clock - start)) : 0, clock, levels.threshold, levels.full);

                    if (event.kind == PadWatcher::hit)      { ++hits; lastStrength = event.strength; lastHeight = event.height; lastHitAt = clock - start; }
                    if (event.kind == PadWatcher::knock)    { ++knocks; lastKnock = juce::jmax (lastKnock, event.height); }

                    PadMessage out[3];
                    toApp (out, event.messages (out));
                }
            }

            /** What a piezo under a plate gives the pin: a spike that reaches its top (this height)
                0.6 ms in, then rings on, dying away, with the downward halves cut off by the diode. */
            static std::function<int (double)> spike (int height)
            {
                return [height] (double micros)
                {
                    const auto swing = std::sin (twoPi * micros / 2400.0);
                    const auto fade = std::exp (-juce::jmax (0.0, micros - 600.0) / 6000.0);
                    return (int) std::lround ((double) height * juce::jmax (0.0, swing) * fade);
                };
            }

            /** One stomp, and the 60 ms after it. Returns its strength, or 0 if it did not count. */
            int stomp (int height)
            {
                const auto before = hits;
                run (60.0, spike (height));
                return hits > before ? lastStrength : 0;
            }

            /** What arrives from the app over USB. */
            void fromApp (const std::vector<juce::MidiMessage>& messages)
            {
                for (const auto& m : messages)
                {
                    if (! m.isController() || m.getChannel() != channel)
                        continue;

                    if (levels.take (m.getControllerNumber(), m.getControllerValue()) == PadLevels::asked)
                    {
                        PadMessage out[5];
                        toApp (out, levels.answer (out));
                    }
                }
            }
        };

        bool splits = true, rises = true, matchesSocket = true;

        for (int h = 0; h <= heightMax; ++h)
        {
            splits = splits && padJoin (padHigh (h), padLow (h)) == h && padHigh (h) <= 127 && padLow (h) <= 127;
            rises = rises && (h == 0 || padStrength (h, 80, 3000) >= padStrength (h - 1, 80, 3000));

            // The same curve as a piezo on a socket, with the heights as parts of full scale.
            const auto asSocket = KickPad::piezoStrength ((float) h / 4095.0f, juce::Decibels::gainToDecibels (80.0f / 4095.0f), juce::Decibels::gainToDecibels (3000.0f / 4095.0f));
            matchesSocket = matchesSocket && std::abs (asSocket - padStrength (h, 80, 3000)) <= 1;
        }

        check (splits, "board pad: every height from 0 to 4095 survives the trip as two CCs");
        check (rises && padStrength (79, 80, 3000) == 0 && padStrength (80, 80, 3000) == 1 && padStrength (3000, 80, 3000) == 127 && padStrength (4095, 80, 3000) == 127,
               "board pad: nothing under Counts from, strength 1 at it, 127 at Hardest at and above, never falling in between");
        check (matchesSocket, "board pad: the controller's strength curve is the one the app uses for a piezo on a socket");
        check (padThreshold (5) == thresholdMin && padThreshold (9000) == thresholdMax && padFull (100, 400) == 600 && padFull (9000, 80) == heightMax,
               "board pad: levels asked for out of range are brought back in, and Hardest at stays above Counts from");

        check (looksLikeController ("Gig Rig Controller") && looksLikeController ("Gig Rig Controller MIDI 1") && looksLikeController ("Teensy MIDI")
                 && ! looksLikeController ("SPD-1K") && ! looksLikeController ("RC-600") && ! looksLikeController ("CQ12T MIDI") && ! looksLikeSpdOne ("Gig Rig Controller"),
               "board pad: the controller is known by its name, and nothing else is taken for it");

        // ---- the controller alone: what it makes of the pin ----
        {
            PretendController c;
            c.run (300.0, nullptr);
            c.run (300.0, [] (double micros) { return 3 + (int) (micros / 100.0) % 5; });
            check (c.hits == 0 && c.knocks == 0, "controller: a still pad, or one with a little hum on it, sends nothing");

            check (c.stomp (1500) == padStrength (1500, thresholdDefault, fullDefault) && c.hits == 1 && c.lastHeight == 1500,
                   "controller: one stomp is one hit, however long the plate rings, and its height is the top of the spike");
            check (c.lastHitAt >= 2000 && c.lastHitAt <= 2100, "controller: the hit is sent two thousandths of a second after the spike begins");

            // A second spike 20 ms after the first is taken for the plate still ringing; one 50 ms after is another stomp.
            const auto twoSpikes = [] (double gapMicros)
            {
                return [gapMicros] (double micros)
                {
                    return PretendController::spike (2000) (micros) + (micros >= gapMicros ? PretendController::spike (2000) (micros - gapMicros) : 0);
                };
            };

            PretendController d, e;
            d.run (120.0, twoSpikes (20000.0));
            e.run (120.0, twoSpikes (50000.0));
            check (d.hits == 1 && e.hits == 2, "controller: the pad is ignored for 40 ms after a hit, and no longer");

            PretendController f;
            f.run (60.0, PretendController::spike (60));
            check (f.hits == 0 && f.knocks == 1 && f.lastKnock >= 55 && f.lastKnock <= 60, "controller: a knock under Counts from is not a hit, and the app is told how tall it was");
            f.knocks = 0;
            f.run (2000.0, [] (double micros) { return (int) (micros / 100.0) % 2 == 0 ? 50 : 0; });
            check (f.hits == 0 && f.knocks >= 7 && f.knocks <= 9, "controller: a pad that rattles on tells the app no more than four times a second");

            PretendController g;
            g.levels.set (400, 2000);
            check (g.stomp (300) == 0 && g.stomp (2000) == 127 && g.stomp (1000) == padStrength (1000, 400, 2000), "controller: it counts stomps by the levels it holds");

            PadMessage out[5];
            PadLevels held;
            check (held.take (ccThresholdLow, 3) == PadLevels::nothing && held.threshold == thresholdDefault
                     && held.take (ccThresholdHigh, padHigh (150)) == PadLevels::nothing && held.take (ccThresholdLow, padLow (150)) == PadLevels::changed && held.threshold == 150
                     && held.take (ccThresholdHigh, padHigh (150)) == PadLevels::nothing && held.take (ccThresholdLow, padLow (150)) == PadLevels::nothing
                     && held.take (ccAsk, 1) == PadLevels::asked && held.answer (out) == 5 && out[0].number == ccProgram && out[0].value == program,
                   "controller: a level is taken only when both its parts have arrived, and it says when one really changed (that is when it writes its memory)");
        }

        // ---- the controller and the app together ----
        KickPad p;
        p.prepare (rate);
        testSettings (p);
        PretendController board;
        board.app = &p;
        board.name = name;
        check (p.reportText().contains ("Controller: none seen yet") && p.takeControllerMessages().empty(), "board pad: with no controller the report says so and nothing is sent anywhere");

        run (p, 0.1);
        const auto strength = board.stomp (1500);
        float left[block] = {}, right[block] = {};
        p.process (left, right, block, false);
        check (strength > 1 && strength < 127 && p.hitCount() == 1 && p.boardHitCount() == 1 && p.padDevice().isEmpty(), "board pad: a stomp on the board's pad is a hit, with no pad to learn");
        check (left[0] != 0.0f || left[1] != 0.0f, "board pad: its kick starts on the first sample of the next block");
        check (p.numStarted() == 1 && p.startedStrength (0) == strength, "board pad: the hit is handed to a recording's notes, so a soundcheck makes it again");
        check (p.reportText().contains ("(board 1500)") && p.reportText().contains ("Smallest spike counted 1500, tallest 1500"), "board pad: the report gives each spike's height");

        // The same road as the SPD::ONE: the same strength makes the same sound, sample for sample.
        {
            KickPad a, b;
            a.prepare (rate);
            b.prepare (rate);
            testSettings (a);
            testSettings (b);
            PretendController other;
            other.app = &b;
            other.name = name;
            a.handleMidi ("SPD-1K", juce::MidiMessage::noteOn (10, 60, (juce::uint8) strength));
            other.stomp (1500);
            check (run (a, 0.5) == run (b, 0.5), "board pad: a hit plays exactly what the same strength from the SPD::ONE plays");

            // And the two can be plugged in together: each plays, and the SPD::ONE is still the pad found by name.
            a.resetCounts();
            other.app = &a;
            other.stomp (1500);
            check (a.hitCount() == 1 && a.boardHitCount() == 1 && a.padDevice() == "SPD-1K" && peak (run (a, 0.1)) > 0.05f, "board pad: it plays beside an SPD::ONE without taking its place");
        }

        // One stomp that reaches the app twice: the weaker one is not played, as with the SPD::ONE.
        p.resetCounts();
        p.handleMidi (name, juce::MidiMessage::noteOn (channel, note, (juce::uint8) 110));
        p.handleMidi (name, juce::MidiMessage::noteOn (channel, note, (juce::uint8) 20));
        check (p.reportText().contains ("weaker of the pair: 1"), "board pad: the bounce guard covers it too");

        // What else the controller may say.
        p.resetCounts();
        run (p, 1.0);
        p.handleMidi (name, juce::MidiMessage::noteOn (channel, note + 1, (juce::uint8) 100));
        p.handleMidi (name, juce::MidiMessage::noteOn (1, note, (juce::uint8) 100));
        p.handleMidi (name, juce::MidiMessage::controllerEvent (1, ccPeakLow, 5));
        p.handleMidi (name, juce::MidiMessage::noteOff (channel, note));
        check (p.hitCount() == 0 && peak (run (p, 0.1)) == 0.0f, "board pad: another note or another channel from the controller is not a kick");

        board.run (60.0, PretendController::spike (61));
        board.run (300.0, PretendController::spike (44));
        check (p.hitCount() == 0 && p.reportText().contains ("Tallest knock that did not count (a tap beside the pad, a footswitch): 61."), "board pad: the report keeps the tallest knock that did not count");

        p.learnNextNote();
        board.stomp (900);
        check (p.padDevice().isEmpty() && ! p.isLearning() && p.boardHitCount() == 1, "board pad: a Learn left waiting does not take the board's pad as the SPD::ONE");

        run (p, 1.0);
        p.resetCounts();
        board.stomp (900);
        check (p.hitCount() == 1 && peak (run (p, 0.1, true)) == 0.0f, "board pad: silent while a measurement runs");

        p.setBoardPadOn (false);
        p.resetCounts();
        board.stomp (900);
        check (p.hitCount() == 0 && peak (run (p, 0.1)) == 0.0f && p.reportText().contains ("Not played because the board's pad is off: 1"), "board pad: switched off, its hits are counted and not played");
        p.setBoardPadOn (true);

        p.setOn (false);
        board.stomp (900);
        check (p.hitCount() == 1 && peak (run (p, 0.1)) == 0.0f, "board pad: with the kick pad off a hit is counted and plays nothing");
        p.setOn (true);

        // The app tells the controller its two levels.
        p.setBoardThreshold (150);
        p.setBoardFull (2600);
        check (p.takeControllerMessages().empty() && p.reportText().contains ("THE APP CANNOT SET ITS LEVELS YET"), "board pad: with no way to the controller the levels wait, and the report says to scan again");

        p.controllerOutputOpened (name);
        const auto first = p.takeControllerMessages();
        board.fromApp (first);
        check (first.size() == 5 && board.levels.threshold == 150 && board.levels.full == 2600, "board pad: when the controller is found it is sent both levels and asked what it holds");
        check (p.takeControllerMessages().empty(), "board pad: and nothing more is sent until a level changes");
        check (p.reportText().contains ("program 2, holds: counts from 150, hardest at 2600. The app can set its levels.") && ! p.reportText().contains ("NOT THE APP'S"),
               "board pad: the report shows the program and the levels the controller says it holds");

        p.setBoardThreshold (150);
        check (p.takeControllerMessages().empty(), "board pad: setting a level to what it already is sends nothing");
        p.setBoardThreshold (1900);     // pushes Hardest at up with it
        check (p.getBoardFull() == 2850 && p.reportText().contains ("NOT THE APP'S"), "board pad: until the controller answers a change, the report says its levels are not the app's");
        board.fromApp (p.takeControllerMessages());
        check (board.levels.threshold == 1900 && board.levels.full == 2850 && ! p.reportText().contains ("NOT THE APP'S"), "board pad: a changed level reaches the controller, and Hardest at stays above Counts from on both sides");

        p.resetCounts();
        check (board.stomp (1000) == 0 && board.stomp (2850) == 127 && p.hitCount() == 1, "board pad: the controller then counts stomps by the new levels");

        // The pad's own messages are never taken for an expression pedal or anything else.
        check (isBoardPadMessage (name, cc (ccPeakHigh, 3)) && isBoardPadMessage ("Teensy MIDI", juce::MidiMessage::noteOn (channel, note, (juce::uint8) 90))
                 && ! isBoardPadMessage ("RC-600", cc (ccPeakHigh, 3)) && ! isBoardPadMessage (name, juce::MidiMessage::controllerEvent (1, 11, 64)),
               "board pad: its messages are known for what they are, and the controller's other messages are left alone");

        // A Teensy still carrying the first bench program sends the note alone.
        KickPad old;
        old.prepare (rate);
        testSettings (old);
        old.handleMidi ("Teensy MIDI", juce::MidiMessage::noteOn (channel, note, (juce::uint8) 70));
        check (peak (run (old, 0.1)) > 0.05f && old.reportText().contains ("(board)") && old.reportText().contains ("first bench program"),
               "board pad: a controller with the first bench program still plays, and the report says to load the newer one");
    }
}

} // namespace gigrig
