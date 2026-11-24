#include "AmpModels.h"
#include "AudioEngine.h"
#include "Responses.h"
#include "effects/EffectChecks.h"
#include "effects/ir/IrTools.h"

#include <random>

#include <cmath>
#include <functional>

// Checks for the effects stage, run by the automatic builds (GigRig --self-test).
// The effects themselves are checked in src/effects/EffectChecks.cpp; this file checks
// the rack inside the engine: routing, listening, the looper's feed, saving, the
// feedback guard, pedal switches, the amp capture, the test take, the response maker and the
// starter presets.

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

    // ---- the test take: record once, then hear it round and round through the chains ----
    {
        Bench b;
        auto& rack = b.engine.effects();
        check (rack.startTake().isNotEmpty(), "test take: will not start while the effects are off (it says why)");
        rack.setEnabled (true);
        check (rack.startTake().isNotEmpty() && rack.takeState() == EffectsRack::TakeState::none, "test take: will not start with no chain switched on (it says why)");

        rack.chain (0).setModel (boostOnly (6.0f));
        rack.chain (1).setModel (boostOnly (0.0f));
        rack.chain (2).setModel (boostOnly (0.0f));
        for (int c = 0; c < 3; ++c)
        {
            auto r = rack.routing (c);
            r.on = c < 2;                   // chain 3 is off while recording, so it stays live later
            r.source = 9 - c;               // sockets 10, 9 and 8
            r.out = { 2 + c * 2, false };   // returns 3, 5 and 7
            r.reference = -1;
            r.looperHears = -2;
            r.listen = EffectsRack::Listen::effects;
            r.trimDb = 0.0f;
            rack.setRouting (c, r);
        }

        const auto silent = [&b] (int i, juce::int64) { b.ins[9][(size_t) i] = b.ins[8][(size_t) i] = b.ins[7][(size_t) i] = 0.0f; };
        const auto playing = [&b] (int i, juce::int64 t)
        {
            b.ins[9][(size_t) i] = (float) (0.1 * std::sin (twoPi * 220.0 * (double) t / rate));
            b.ins[8][(size_t) i] = (float) (0.05 * std::sin (twoPi * 330.0 * (double) t / rate));
            b.ins[7][(size_t) i] = 0.0f;
        };

        check (rack.startTake().isEmpty() && rack.takeState() == EffectsRack::TakeState::recording, "test take: starts recording");
        b.play (450, 2, silent);        // 0.3 s before the first note
        b.play (1500, 2, playing);      // 1 s of playing
        b.play (1500, 2, silent);       // 1 s after the last
        check (std::abs (rack.takeSeconds() - 2.3) < 0.01, "test take: counts up while recording (" + juce::String (rack.takeSeconds(), 2) + " s)");
        rack.stopTake();
        check (rack.takeState() == EffectsRack::TakeState::ready && rack.takeHolds (0) && rack.takeHolds (1) && ! rack.takeHolds (2),
               "test take: holds the two chains that were on, and not the one that was off");
        check (std::abs (rack.takeSeconds() - 1.7) < 0.02, "test take: the silence before the first note and after the last is trimmed to a breath ("
                                                           + juce::String (rack.takeSeconds(), 2) + " s kept of 2.8)");

        // Played back with nothing plugged in: chain 1 gets its guitar back, 6 dB up, and chain 2 its own part.
        check (rack.playTake().isEmpty() && rack.takeState() == EffectsRack::TakeState::playing, "test take: plays");
        b.play (200, 2, silent);
        const auto one = b.play (1100, 2, silent);
        b.t -= 1100 * block;
        const double want = 0.1 * 2.0 / std::sqrt (2.0);
        std::vector<float> middle (one.begin() + 2000, one.begin() + 60000);
        check (std::abs (rms (middle) / want - 1.0) < 0.05, "test take: chain 1 hears its guitar from the take in place of its socket, through its effects (6 dB up: "
                                                           + juce::String (20.0 * std::log10 (rms (middle) / (0.1 / std::sqrt (2.0))), 2) + " dB)");
        b.engine.effects().takeInputPeak (1);
        const auto two = b.play (800, 4, silent);
        check (rms (two) > 0.01 && rack.takeInputPeak (1) > 0.04f, "test take: chain 2 hears its own part at the same time, and its In meter shows it");
        const float* fed = rack.looperInputs (b.inPtrs.data(), 16)[9];
        check (fed != b.inPtrs[9], "test take: the looper hears the take through the effects too, as if it were played live");

        // Round and round: the whole take comes by again and again, with no click where it joins.
        const auto loops = b.play (6000, 2, silent);        // 4 seconds: more than two trips round
        float biggest = 0.0f;
        for (size_t i = 1; i < loops.size(); ++i) biggest = juce::jmax (biggest, std::abs (loops[i] - loops[i - 1]));
        const double steady = 0.2 * twoPi * 220.0 / rate;   // the biggest step the note itself makes
        std::vector<float> lateOn (loops.end() - 96000, loops.end());
        check (rms (lateOn) > 0.03 && biggest < steady * 2.0, "test take: it goes round and round without a click at the join (biggest step "
                                                              + juce::String (biggest, 4) + ", the note's own " + juce::String (steady, 4) + ")");

        // A chain that was off when the take was made stays live, to play or sing along.
        auto r3 = rack.routing (2);
        r3.on = true;
        rack.setRouting (2, r3);
        const auto live = b.play (600, 6, [&b] (int i, juce::int64 t) { b.ins[7][(size_t) i] = (float) (0.2 * std::sin (twoPi * 440.0 * (double) t / rate)); });
        check (std::abs (rms (live, live.size() / 2) - 0.2 / std::sqrt (2.0)) < 0.005, "test take: a chain that is not in the take stays live, to play along with it");

        rack.stopTake();
        b.play (40, 2, silent);
        check (rms (b.play (100, 2, silent)) == 0.0 && rack.takeState() == EffectsRack::TakeState::ready, "test take: stopped, the chains hear their sockets again and the take is kept");

        // Saved, loaded again, the same.
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GigRig self-test takes");
        folder.deleteRecursively();
        const auto file = folder.getChildFile ("A take.wav");
        const auto saveError = rack.saveTake (file);
        const double before = rack.takeSeconds();
        const auto loadError = rack.loadTake (file);
        check (saveError.isEmpty() && loadError.isEmpty() && std::abs (rack.takeSeconds() - before) < 0.001 && rack.takeHolds (0) && rack.takeHolds (1) && ! rack.takeHolds (2)
               && rack.takeName() == "A take",
               "test take: saved to a WAV and loaded again, it is the same take (" + rack.takeText() + ")" + saveError + loadError);
        rack.playTake();
        b.play (200, 2, silent);
        const auto again = b.play (1100, 2, silent);
        std::vector<float> middleAgain (again.begin() + 2000, again.begin() + 60000);
        check (std::abs (rms (middleAgain) / want - 1.0) < 0.05, "test take: and plays the same after loading");
        rack.stopTake();

        // One switch does it all: record, then keep it and play it round, then stop.
        const auto first = rack.takeStep();
        const bool recording = rack.takeState() == EffectsRack::TakeState::recording;
        b.play (1500, 2, playing);
        juce::File saved;
        const auto second = rack.takeStep();
        for (const auto& f : EffectsRack::takeFolder().findChildFiles (juce::File::findFiles, false, "*.wav"))
            if (f.getFileNameWithoutExtension() == rack.takeName())
                saved = f;
        const bool nowPlaying = rack.takeState() == EffectsRack::TakeState::playing;
        rack.takeStep();
        check (recording && nowPlaying && rack.takeState() == EffectsRack::TakeState::ready && saved.existsAsFile(),
               "test take: one pedal switch records, then keeps it and plays it round, then stops (\"" + first + "\" / \"" + second + "\")");
        saved.deleteFile();     // the self-test tidies up after itself

        rack.setPedalAssign (1, { 9, 0, 4 });
        check (rack.pedalText().contains ("test take") && rack.reportText().contains ("Test take \""), "test take: the report says which switch works it and what the take holds");
        folder.deleteRecursively();
    }

    // ---- the Bass9 copy beside the acoustic chain: two chains on one socket ----
    {
        Bench b;
        auto& rack = b.engine.effects();
        rack.setEnabled (true);
        rack.chain (0).setModel (boostOnly (0.0f));
        fx::ChainModel bass;
        bass.slots.push_back ({ "bass9", true, {}, {} });
        rack.chain (1).setModel (bass);

        auto r0 = rack.routing (0), r1 = rack.routing (1);
        r0.on = r1.on = true;
        r0.source = r1.source = 9;          // the acoustic's socket, for both
        r0.reference = r1.reference = -1;
        r0.out = { 2, false };
        r1.out = { 4, false };
        r0.looperHears = -2;
        r1.looperHears = 8;                 // the looper hears the bass where the real Bass9 is plugged in
        rack.setRouting (0, r0);
        rack.setRouting (1, r1);

        const auto guitar = [&b] (int i, juce::int64 t) { b.ins[9][(size_t) i] = (float) (0.2 * std::sin (twoPi * 110.0 * (double) t / rate)); };
        b.play (1500, 2, guitar);
        const auto acoustic = b.play (300, 2, guitar);
        const auto low = b.play (300, 4, guitar);
        const auto feed = rack.looperInputs (b.inPtrs.data(), 16);
        check (rms (acoustic) > 0.1 && rms (low) > 0.01 && feed[9] != b.inPtrs[9] && feed[8] != b.inPtrs[8] && feed[8] != feed[9],
               "effects: the acoustic chain and the Bass9 copy share the guitar's socket; the looper hears the guitar on socket 10 and the bass as socket 9");
    }

    // ---- the whole rig at once: electric, acoustic, vocal and bass chains, all full ----
    {
        // The longest response file the "ir" effect will play, so the acoustic chain is at its heaviest.
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GigRig self-test whole rig");
        folder.deleteRecursively();
        folder.createDirectory();
        const auto irFile = folder.getChildFile ("long.wav");
        {
            std::mt19937 gen (3);
            std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
            fx::ir::Wav wav;
            wav.rate = 48000.0;
            wav.channels.emplace_back (5000);
            for (size_t i = 0; i < wav.channels[0].size(); ++i)
                wav.channels[0][i] = 0.05f * dist (gen) * (float) std::exp (-(double) i / 3000.0) + (i == 0 ? 0.5f : 0.0f);
            std::string error;
            fx::ir::writeWav (irFile.getFullPathName().toStdString(), wav, error);
        }

        Bench b;
        auto& rack = b.engine.effects();
        rack.setEnabled (true);

        const auto build = [&] (int c, int socket, std::initializer_list<const char*> ids)
        {
            fx::ChainModel m;
            for (const auto* id : ids)
            {
                fx::SlotModel slot { id, true, {}, {} };
                if (juce::String (id) == "ir")
                    slot.texts.push_back ({ "file", irFile.getFullPathName().toStdString() });
                m.slots.push_back (slot);
            }
            rack.chain (c).setModel (m);
            auto r = rack.routing (c);
            r.on = true;
            r.source = socket;
            r.reference = -1;
            r.out = { c * 2, true };
            rack.setRouting (c, r);
            return (int) m.slots.size();
        };

        int count = 0;
        count += build (0, 7, { "comp", "eq", "ts", "klon", "gate", "amp", "spring", "tremolo", "analogdelay", "digitaldelay", "chorus", "vibrato" });
        count += build (1, 9, { "ir", "body", "peq", "comp", "analogdelay", "chorus", "tremolo", "univibe", "room" });
        count += build (2, 5, { "peq", "comp", "harmony", "flanger", "digitaldelay", "tapedelay", "plate" });
        count += build (3, 9, { "bass9", "comp", "peq" });

        const auto fill = [&b] (int i, juce::int64 t)
        {
            b.ins[7][(size_t) i] = (float) (0.2 * std::sin (twoPi * 196.0 * (double) t / rate));
            b.ins[9][(size_t) i] = (float) (0.2 * std::sin (twoPi * 110.0 * (double) t / rate));
            b.ins[5][(size_t) i] = (float) (0.2 * std::sin (twoPi * 220.0 * (double) t / rate) + 0.05 * std::sin (twoPi * 440.0 * (double) t / rate));
        };
        b.play (300, 2, fill);
        double best = 1.0e9;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto start = juce::Time::getMillisecondCounterHiRes();
            b.play (1500, 2, fill);
            best = juce::jmin (best, (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0 * 100.0);   // 1500 blocks of 64 at 96 kHz = 1 second
        }

        juce::String each;
        double sum = 0.0;
        for (int c = 0; c < 4; ++c)
        {
            const auto load = rack.chainLoad (c);
            sum += load.average;
            each << (c == 0 ? "" : ", ") << juce::String (load.average * 100.0, 1) << "%";
        }

        // The build machines are slower than the rig will be and shared with other work, so this only
        // fails when the rig could not keep up at all. Tom's laptop and the rig PC are the real test:
        // the report there shows each chain's share while he plays.
        check (best < 100.0, "effects: the whole rig at once (" + juce::String (count) + " effects in an electric, an acoustic, a vocal and a bass chain, with the looper) uses "
                             + juce::String (best, 1) + "% of the time available on this build machine (it must keep up: under 100%)");
        check (sum > 0.0 && sum * 100.0 < best * 1.5 + 5.0 && rack.reportText().contains ("Processor: this chain uses"),
               "effects: each chain reports its own share of the processor (" + each + ")");
        folder.deleteRecursively();
    }

    // ---- a response file made from a capture, as the Amp capture tab does it ----
    {
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GigRig self-test responses");
        folder.deleteRecursively();
        folder.createDirectory();
        const auto capture = folder.getChildFile ("Pedal 2026-11-25 01-02-03.wav"), target = folder.getChildFile ("Pedal.wav");

        // A pretend tone pedal: half as loud, 100 samples late, the top rolled off.
        std::mt19937 gen (11);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        fx::ir::Wav wav;
        wav.rate = 48000.0;
        wav.channels.assign (2, std::vector<float> (48000 * 6, 0.0f));
        float smooth = 0.0f;
        for (size_t i = 0; i < wav.channels[0].size(); ++i)
        {
            wav.channels[0][i] = 0.3f * dist (gen);
            smooth += 0.4f * (wav.channels[0][i] - smooth);
            if (i + 100 < wav.channels[1].size())
                wav.channels[1][i + 100] = 0.5f * smooth;
        }
        std::string error;
        fx::ir::writeWav (capture.getFullPathName().toStdString(), wav, error);

        const auto made = responses::makeFromCapture (capture, target);
        fx::ir::Wav back;
        const bool read = made.ok && fx::ir::readWav (target.getFullPathName().toStdString(), back, error);
        const double top = read ? fx::ir::responseDb (back.channels[0], back.rate, 8000.0) - fx::ir::responseDb (back.channels[0], back.rate, 500.0) : 0.0;
        check (read && made.text.contains ("2.08 ms late") && made.text.contains ("quieter") && top < -4.0 && top > -9.0
               && target.withFileExtension ("txt").loadFileAsString().contains ("What the file does to the tone"),
               "response file: made from a capture of a pretend tone pedal, with a note beside it (" + made.text.upToFirstOccurrenceOf (" To use it", false, false) + ")");

        const auto notCapture = responses::makeFromCapture (folder.getChildFile ("nothing.wav"), target);
        check (! notCapture.ok && notCapture.text.isNotEmpty(), "response file: a missing capture gives a plain reason (" + notCapture.text + ")");
        folder.deleteRecursively();
    }

    // ---- the starter presets built into the app ----
    {
        const auto presets = starterpresets::all();
        bool sound = presets.size() == 3;
        juce::String detail;

        for (const auto& [name, text] : presets)
        {
            std::vector<std::string> skipped;
            const auto m = fx::ChainModel::fromText (text.toStdString(), &skipped);
            sound = sound && skipped.empty() && ! m.slots.empty() && (int) m.slots.size() <= fx::Chain::maxSlots;
            // Every setting named in the file is one the effect really has.
            for (const auto& slot : m.slots)
            {
                const auto fx = fx::makeEffect (slot.type);
                for (const auto& [id, value] : slot.params)
                    if (fx == nullptr || fx->paramIndex (id) < 0)
                    {
                        sound = false;
                        detail << " [" << name << ": " << slot.type << " has no setting '" << id << "']";
                    }
            }
            detail << (detail.isEmpty() ? "" : ", ") << name << " (" << (int) m.slots.size() << " effects)";
        }

        check (sound, "starter presets: all three load, and every effect and setting in them exists: " + detail);

        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GigRig self-test presets");
        folder.deleteRecursively();
        const int wrote = starterpresets::setUp (folder);
        const auto mine = folder.getChildFile ("Starter - Vocal.txt");
        mine.replaceWithText ("slot=peq\non=1\n");
        const int wroteAgain = starterpresets::setUp (folder);
        const auto kept = mine.loadFileAsString();
        check (wrote == 3 && wroteAgain == 0 && kept.contains ("slot=peq") && ! kept.contains ("slot=harmony"),
               "starter presets: copied into the presets folder once; a starter preset Tom has changed is never overwritten");
        folder.deleteRecursively();
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
