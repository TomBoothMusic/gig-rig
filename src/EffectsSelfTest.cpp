#include "AmpModels.h"
#include "AudioEngine.h"
#include "effects/EffectChecks.h"

#include <cmath>
#include <functional>

// Checks for the effects stage, run by the automatic builds (GigRig --self-test).
// The effects themselves are checked in src/effects/EffectChecks.cpp; this file checks
// the rack inside the engine: routing, listening, the looper's feed, saving, the
// feedback guard, pedal switches and the amp capture.

namespace gigrig
{

namespace
{
    using Check = std::function<void (bool, const juce::String&)>;
    constexpr double rate = 96000.0;
    constexpr int block = 64;
    constexpr double twoPi = 6.283185307179586;

    struct Bench
    {
        AudioEngine engine;
        std::vector<std::vector<float>> ins, outs;
        std::vector<const float*> inPtrs;
        std::vector<float*> outPtrs;
        juce::int64 t = 0;

        Bench()
        {
            engine.openSimulated (rate, block, 16, 16);
            ins.assign (16, std::vector<float> (block, 0.0f));
            outs.assign (16, std::vector<float> (block, 0.0f));
            for (auto& v : ins)  inPtrs.push_back (v.data());
            for (auto& v : outs) outPtrs.push_back (v.data());
        }

        /** Plays blocks; fill(i, t) writes the inputs for sample i at time t. Returns what return r carried. */
        std::vector<float> play (int blocks, int returnIndex, const std::function<void (int, juce::int64)>& fill)
        {
            std::vector<float> heard;
            const auto o = returnToOutput (returnIndex, 16);

            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < block; ++i)
                    fill (i, t++);

                engine.processBlock (inPtrs.data(), 16, outPtrs.data(), 16, block);
                heard.insert (heard.end(), outs[(size_t) o].begin(), outs[(size_t) o].end());
            }

            return heard;
        }
    };

    double rms (const std::vector<float>& x, size_t from = 0)
    {
        double s = 0.0;
        for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i];
        return x.size() > from ? std::sqrt (s / (double) (x.size() - from)) : 0.0;
    }

    fx::ChainModel boostOnly (float db)
    {
        fx::ChainModel m;
        m.slots.push_back ({ "boost", true, { { "boost", db }, { "bright", 0.0f } }, {} });
        return m;
    }
}

void runEffectsSelfTest (const Check& check)
{
    // ---- the amp models built into the app: installed into a folder of their own ----
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GigRig self-test amp models");
        dir.deleteRecursively();
        const int wrote = ampmodels::setUp (dir);
        const auto list = ampmodels::names (dir);
        check (wrote == 3 && list.size() == 3 && list.contains (ampmodels::defaultName()),
               "amp models: the three built-in Dream '65 models are copied into the models folder (" + list.joinIntoString (", ") + ")");
        check (ampmodels::setUp (dir) == 0, "amp models: starting again leaves models that are already there alone");
    }

    // ---- every effect, the chain and the oversampler ----
    fx::runEffectChecks ([&check] (bool ok, const std::string& what) { check (ok, "effects: " + juce::String (what)); });

    // ---- the rack inside the engine ----
    {
        Bench b;
        auto& rack = b.engine.effects();
        const auto sine = [&b] (int i, juce::int64 t)
        {
            b.ins[9][(size_t) i] = (float) (0.1 * std::sin (twoPi * 220.0 * (double) t / rate));
            b.ins[8][(size_t) i] = (float) (0.05 * std::sin (twoPi * 330.0 * (double) t / rate));
        };

        rack.setEnabled (false);
        const auto off = b.play (300, 2, sine);
        check (rms (off) == 0.0, "effects: switched off, nothing comes out on the returns");
        check (rack.looperInputs (b.inPtrs.data(), 16)[9] == b.inPtrs[9], "effects: switched off, the looper hears the socket itself");

        rack.setEnabled (true);
        check (rack.routing (1).reference == 7, "effects: a new chain compares with socket 8 (where the board and the Dream come back)");

        // Tap tempo: four taps half a second apart give 120 bpm; a loop's tempo wins over taps.
        for (double ms : { 10000.0, 10500.0, 11000.0, 11500.0 })
            rack.tap (ms);
        rack.setTempo (0.0);
        check (std::abs (rack.effectsTempo() - 120.0) < 0.01 && rack.tempoText().contains ("tapped"),
               "effects: tapping every half second sets 120 bpm for synced delays (" + rack.tempoText() + ")");
        rack.tap (11520.0);     // a switch bounce is ignored
        rack.setTempo (0.0);
        check (std::abs (rack.effectsTempo() - 120.0) < 0.01, "effects: a bounce 20 ms after a tap is ignored");
        rack.setTempo (96.0);
        check (std::abs (rack.effectsTempo() - 96.0) < 0.01 && rack.tempoText().contains ("looper"),
               "effects: once a loop sets the tempo, the delays follow the loop (" + rack.tempoText() + ")");
        rack.tap (20000.0); rack.tap (20750.0);      // after a long gap: two fresh taps, 80 bpm
        rack.setTempo (0.0);
        check (std::abs (rack.effectsTempo() - 80.0) < 0.01, "effects: after a pause, two fresh taps start a new tempo (" + rack.tempoText() + ")");
        rack.chain (0).setModel (boostOnly (6.0f));
        auto r = rack.routing (0);
        r.on = true;
        r.source = 9;
        r.out = { 2, true };
        r.looperHears = -2;
        r.reference = -1;
        r.listen = EffectsRack::Listen::effects;
        r.trimDb = 0.0f;
        rack.setRouting (0, r);

        const auto onA = b.play (600, 2, sine);
        const auto onB = b.play (300, 3, sine);
        const double gainDb = 20.0 * std::log10 (rms (onA, onA.size() / 2) / (0.1 / std::sqrt (2.0)));
        check (std::abs (gainDb - 6.0) < 0.3, "effects: a 6 dB boost on socket 10 comes out 6 dB louder on return 3 (" + juce::String (gainDb, 2) + " dB)");
        check (rms (onB) > 0.1, "effects: and on return 4, the other half of the pair");

        const float* fed = rack.looperInputs (b.inPtrs.data(), 16)[9];
        double worst = 0.0;
        for (int i = 0; i < block; ++i)
            worst = juce::jmax (worst, (double) std::abs (fed[i] - b.outs[(size_t) returnToOutput (3, 16)][(size_t) i]));
        check (fed != b.inPtrs[9] && worst < 1.0e-6, "effects: the looper hears the effected sound in place of socket 10");

        rack.setListen (0, EffectsRack::Listen::dry);
        b.play (40, 2, sine);
        const auto dry = b.play (100, 2, sine);
        double dryErr = 0.0;
        for (size_t i = 0; i < dry.size(); ++i)
        {
            const auto tt = b.t - (juce::int64) dry.size() + (juce::int64) i;
            dryErr = juce::jmax (dryErr, std::abs ((double) dry[i] - 0.1 * std::sin (twoPi * 220.0 * (double) tt / rate)));
        }
        check (dryErr < 1.0e-5, "effects: Listen to Dry passes the socket straight through");

        r = rack.routing (0);
        r.reference = 8;
        r.listen = EffectsRack::Listen::reference;
        rack.setRouting (0, r);
        b.play (40, 2, sine);
        const auto refHeard = b.play (100, 2, sine);
        check (std::abs (rms (refHeard) - 0.05 / std::sqrt (2.0)) < 1.0e-3, "effects: Listen to Reference plays the real pedal on socket 9");

        rack.toggleListen (0);
        check (rack.routing (0).listen == EffectsRack::Listen::effects, "effects: A/B goes from the reference back to the effects");
        rack.toggleListen (0);
        check (rack.routing (0).listen == EffectsRack::Listen::reference, "effects: and back to the reference");

        rack.setListen (0, EffectsRack::Listen::effects);
        rack.takeLevels (0);
        b.play (1500, 2, sine);
        const auto lv = rack.takeLevels (0);
        check (lv.playedSeconds > 0.8 && std::abs ((lv.effectsDb - lv.compareDb) - (20.0 * std::log10 (2.0 * 0.1 / 0.05))) < 0.5,
               "effects: the level match hears the effects " + juce::String (lv.effectsDb - lv.compareDb, 1) + " dB above the reference");

        // Saved and loaded again, everything the same.
        rack.setPedalAssign (0, { 10, 0, 11 });
        const auto text = rack.stateText();
        EffectsRack other;
        other.loadStateText (text);
        check (other.stateText() == text, "effects: the rack's memory reads back exactly as it was saved");

        // Pedal switches.
        rack.setPedalAssign (0, { 10, 0, 0 });
        check (rack.takePedal (10, true) && ! rack.takePedal (3, true), "effects: CTL4 goes to the effects when chosen, other switches stay with the looper");
        rack.setEnabled (false);
        check (! rack.takePedal (10, true), "effects: with the effects off, CTL4 goes back to the looper");
        rack.setEnabled (true);
        rack.setPedalAssign (0, { -1, 0, 0 });

        // A held switch bends the whammy: toe on the press, heel on the release.
        {
            const auto before = rack.chain (0).model();
            fx::ChainModel wm;
            wm.slots.push_back ({ "boost", true, {}, {} });
            wm.slots.push_back ({ "whammy", true, {}, {} });
            rack.chain (0).setModel (wm);
            const bool down = rack.bendWhammy (0, true);
            const float toe = rack.chain (0).model().slots[1].param ("pedal", -1.0f);
            rack.bendWhammy (0, false);
            const float heel = rack.chain (0).model().slots[1].param ("pedal", -1.0f);
            rack.setPedalAssign (0, { 10, 0, 3 });
            const bool taken = rack.takePedal (10, true) && rack.takePedal (10, false);
            rack.setPedalAssign (0, { -1, 0, 0 });
            rack.chain (0).setModel (before);
            check (down && toe > 99.9f && std::abs (heel) < 0.1f && taken && ! rack.bendWhammy (0, true),
                   "effects: a held switch puts the whammy's pedal at the toe and letting go brings it back to the heel");
        }

        // The feedback guard.
        rack.chain (0).setModel (boostOnly (24.0f));
        r = rack.routing (0);
        r.listen = EffectsRack::Listen::effects;
        r.trimDb = 24.0f;
        rack.setRouting (0, r);
        b.play (600, 2, [&b] (int i, juce::int64 t) { b.ins[9][(size_t) i] = (t / 100) % 2 == 0 ? 0.9f : -0.9f; });
        check (rack.guardTripped (0), "effects: a howl (output stuck at full level) trips the feedback guard");
        const auto after = b.play (50, 2, sine);
        check (rms (after) == 0.0, "effects: and the chain goes quiet");
        rack.clearGuard (0);
    }

    // ---- processor use of a busy chain ----
    {
        Bench b;
        auto& rack = b.engine.effects();
        rack.setEnabled (true);
        fx::ChainModel m;
        for (const auto* id : { "boost", "ts", "bd", "klon", "marshall" })
            m.slots.push_back ({ id, true, {}, {} });
        rack.chain (0).setModel (m);
        auto r = rack.routing (0);
        r.on = true;
        rack.setRouting (0, r);
        fx::ChainModel bass;
        bass.slots.push_back ({ "bass9", true, {}, {} });
        rack.chain (1).setModel (bass);
        auto r1 = rack.routing (1);
        r1.on = true;
        r1.source = 8;
        r1.out = { 4, true };
        rack.setRouting (1, r1);

        const auto fill = [&b] (int i, juce::int64 t)
        {
            b.ins[9][(size_t) i] = (float) (0.2 * std::sin (twoPi * 196.0 * (double) t / rate));
            b.ins[8][(size_t) i] = (float) (0.2 * std::sin (twoPi * 110.0 * (double) t / rate));
        };
        b.play (200, 2, fill);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        b.play (1500, 2, fill);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
        const auto percent = ms / 1000.0 * 100.0;   // 1500 blocks of 64 at 96 kHz = 1 second
        check (percent < 40.0, "effects: five drives in a row plus the Bass9 copy, with the looper, use "
                               + juce::String (percent, 1) + "% of the time available (under 40%)");
    }

    // ---- the amp capture ----
    {
        const auto a = EffectsRack::makeTestSound (rate), again = EffectsRack::makeTestSound (rate);
        float top = 0.0f;
        for (auto v : a) top = juce::jmax (top, std::abs (v));
        const double seconds = (double) a.size() / rate;
        check (a == again && seconds > 150.0 && seconds < 220.0 && top <= 1.0f && top > 0.9f,
               "amp capture: the app's test sound is the same every time, " + juce::String (seconds, 0) + " s long, peak " + juce::String (top, 2));

        // A short 48 kHz test file through a pretend amp (a cable straight back, one block late).
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GigRig self-test amp capture");
        folder.deleteRecursively();
        folder.createDirectory();
        const auto soundFile = folder.getChildFile ("sound.wav");

        {
            juce::AudioBuffer<float> buf (1, 48000);
            for (int i = 0; i < buf.getNumSamples(); ++i)
                buf.setSample (0, i, (float) (0.5 * std::sin (twoPi * 440.0 * i / 48000.0)));

            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (soundFile);
            juce::WavAudioFormat wav;
            auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
            if (w != nullptr)
                w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        }

        Bench b;
        auto& rack = b.engine.effects();
        EffectsRack::AmpCaptureSetup setup;
        setup.out = { 2, true };
        setup.input = 7;
        setup.levelDb = -6.0f;
        setup.name = "Self test";
        setup.soundFile = soundFile;
        const auto error = rack.startAmpCapture (setup, folder, 9.3, "a test");
        check (error.isEmpty(), "amp capture: starts (" + (error.isEmpty() ? juce::String ("no error") : error) + ")");

        std::vector<float> lastOut (block, 0.0f);
        int guard = 0;
        while (! rack.ampCaptureFinished() && ++guard < 5000)
        {
            b.play (1, 2, [&] (int i, juce::int64) { b.ins[7][(size_t) i] = lastOut[(size_t) i]; });
            lastOut = b.outs[(size_t) returnToOutput (2, 16)];
        }
        rack.stopAmpCapture();

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (rack.lastAmpCaptureFile()));
        bool shapeOk = false, alignedOk = false, peakOk = false;
        juce::String detail = "no file";

        if (reader != nullptr)
        {
            juce::AudioBuffer<float> got (2, (int) reader->lengthInSamples);
            reader->read (&got, 0, got.getNumSamples(), 0, true, true);
            const auto expected = 96000 + (int) (0.5 * rate);
            shapeOk = reader->numChannels == 2 && std::abs ((int) reader->lengthInSamples - expected) <= block && reader->sampleRate == rate;
            peakOk = std::abs (juce::Decibels::gainToDecibels (got.getMagnitude (0, 0, got.getNumSamples())) + 6.0f) < 0.1f;

            double err = 0.0;
            for (int i = 1000; i < 90000; ++i)
                err = juce::jmax (err, (double) std::abs (got.getSample (1, i + block) - got.getSample (0, i)));
            alignedOk = err < 1.0e-4;
            detail = juce::String ((int) reader->lengthInSamples) + " samples, " + juce::String ((int) reader->numChannels) + " channels";
        }

        check (shapeOk, "amp capture: a 48 kHz test file plays at 96 kHz and the WAV holds the sound plus half a second (" + detail + ")");
        check (peakOk, "amp capture: the test sound peaks at the level chosen (-6 dBFS)");
        check (alignedOk, "amp capture: channel 2 is what came back, channel 1 what was sent, sample for sample");
        check (rack.lastAmpCaptureFile().withFileExtension ("txt").loadFileAsString().contains ("name=Self test"), "amp capture: a .txt beside it says what was captured");
        folder.deleteRecursively();
    }
}

} // namespace gigrig
