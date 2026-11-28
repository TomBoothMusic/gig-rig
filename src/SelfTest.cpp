#include "SelfTest.h"
#include "AudioEngine.h"
#include "DeviceReport.h"
#include "ExpressionPedal.h"
#include "LatencyProbe.h"
#include "MidiMonitor.h"

namespace gigrig
{

void runKickPadSelfTest (const std::function<void (bool, const juce::String&)>& check);
void runLooperSelfTest (const std::function<void (bool, const juce::String&)>& check);
void runEffectsSelfTest (const std::function<void (bool, const juce::String&)>& check);
void runSongSelfTest (const std::function<void (bool, const juce::String&)>& check);
void runMixerSelfTest (const std::function<void (bool, const juce::String&)>& check);
void runRecordingsSelfTest (const std::function<void (bool, const juce::String&)>& check);
void runRemoteSelfTest (const std::function<void (bool, const juce::String&)>& check);
void runBreakMusicSelfTest (const std::function<void (bool, const juce::String&)>& check);
void runExpressionSelfTest (const std::function<void (bool, const juce::String&)>& check);
void runTunerSelfTest (const std::function<void (bool, const juce::String&)>& check);

namespace
{
    juce::StringArray channels (int count)
    {
        juce::StringArray names;

        for (int i = 1; i <= count; ++i)
            names.add ("Ch " + juce::String (i));

        return names;
    }

    AudioDeviceEntry device (const juce::String& type, const juce::String& name, int ins, int outs)
    {
        AudioDeviceEntry e;
        e.typeName = type;
        e.name = name;
        e.inputChannels = channels (ins);
        e.outputChannels = channels (outs);
        return e;
    }

    juce::MidiDeviceInfo midi (const juce::String& name)
    {
        return { name, name };
    }
}

juce::String runSelfTest (bool& passed)
{
    // The checks build pretend rigs: none of them may write over this machine's own memory of the Mixer and Effects tabs.
    Mixer::memoryOnDisk = false;
    EffectsRack::memoryOnDisk = false;
    BreakMusic::memoryOnDisk = false;
    ExpressionInput::memoryOnDisk = false;
    Tuner::memoryOnDisk = false;

    juce::StringArray log;
    int failures = 0;

    const auto check = [&] (bool condition, const juce::String& what)
    {
        log.add ((condition ? "ok    " : "FAIL  ") + what);

        if (! condition)
            ++failures;
    };

    // For working on one part without waiting for the rest (never used by the automatic builds):
    // GIGRIG_SELFTEST_ONLY=break runs only that part's checks. Parts: break, pedal, tuner, remote, mixer, recordings, songs.
    if (const auto only = juce::SystemStats::getEnvironmentVariable ("GIGRIG_SELFTEST_ONLY", {}); only.isNotEmpty())
    {
        if (only == "break")        runBreakMusicSelfTest (check);
        else if (only == "pedal")   runExpressionSelfTest (check);
        else if (only == "tuner")   runTunerSelfTest (check);
        else if (only == "remote")  runRemoteSelfTest (check);
        else if (only == "mixer")   runMixerSelfTest (check);
        else if (only == "recordings") runRecordingsSelfTest (check);
        else if (only == "songs")   runSongSelfTest (check);

        passed = failures == 0;
        log.add ({});
        log.add ((passed ? "Part '" + only + "' only: passed (" : "Part '" + only + "' only: FAILED (" + juce::String (failures) + " of ") + juce::String (log.size() - 1) + " checks)");
        return log.joinIntoString ("\n") + "\n";
    }

    using State = Verdict::State;

    // --- Name matching -------------------------------------------------------
    check (looksLikeCQ ("CQ12T"),                          "CQ12T is a CQ");
    check (looksLikeCQ ("Allen & Heath CQ ASIO Driver"),   "A&H CQ ASIO driver is a CQ");
    check (looksLikeCQ ("Line (CQ-12T)"),                  "Windows-style 'Line (CQ-12T)' is a CQ");
    check (looksLikeCQ ("Allen&Heath USB Audio"),          "Allen&Heath without the model is a CQ");
    check (! looksLikeCQ ("Realtek High Definition Audio"), "Realtek is not a CQ");
    check (! looksLikeCQ ("WING"),                         "Wing is not a CQ");
    check (! looksLikeCQ ("Acquire Capture"),              "a word containing 'cq' is not a CQ");
    check (! looksLikeCQ ("MSI MP341CQ"),                  "the monitor 'MSI MP341CQ' is not a CQ");
    check (looksLikeCQ ("CQ ASIO Driver"),                 "'CQ ASIO Driver' is a CQ");
    check (looksLikeRC600 ("RC-600"),                      "RC-600 is an RC-600");
    check (looksLikeRC600 ("MIDIIN2 (RC-600)"),            "second RC-600 port is an RC-600");
    check (! looksLikeRC600 ("RC-505mkII"),                "RC-505 is not an RC-600");

    // --- Nothing plugged in --------------------------------------------------
    {
        DeviceReport r;
        r.audioDevices.add (device ("Windows Audio", "Speakers (Realtek Audio)", 0, 2));
        judge (r);
        check (r.mixer.state == State::missing,  "no CQ -> mixer not found");
        check (r.looper.state == State::missing, "no RC-600 -> looper not found");
    }

    // --- Driver installed: ASIO shows all 16 ---------------------------------
    {
        DeviceReport r;
        r.audioDevices.add (device ("Windows Audio", "Line (CQ12T)", 16, 0));
        r.audioDevices.add (device ("Windows Audio", "Speakers (CQ12T)", 0, 16));
        r.audioDevices.add (device ("ASIO", "CQ ASIO Driver", 16, 16));
        r.midiInputs.add (midi ("RC-600"));
        r.midiOutputs.add (midi ("RC-600"));
        judge (r);
        check (r.mixer.state == State::found,           "CQ 16x16 -> mixer found");
        check (r.mixer.headline.contains ("ASIO"),      "ASIO preferred when channel counts tie");
        check (r.mixer.headline.contains ("16 inputs"), "headline gives the input count");
        check (r.looper.state == State::found,          "RC-600 input -> looper found");
        check (r.looper.detail.contains ("slot 65"),    "RC-600 advice points at the MIDI CTRL memory");
    }

    // --- No driver: Windows only shows a stereo pair --------------------------
    {
        DeviceReport r;
        r.audioDevices.add (device ("Windows Audio", "Line (CQ12T)", 2, 0));
        r.audioDevices.add (device ("Windows Audio", "Speakers (CQ12T)", 0, 2));
        r.midiOutputs.add (midi ("RC-600"));
        judge (r);
        check (r.mixer.state == State::partial,  "CQ with 2 channels -> partly");
        check (r.looper.state == State::partial, "RC-600 output only -> partly");
    }

    // --- Separate in and out entries still add up to a full device ------------
    {
        DeviceReport r;
        r.audioDevices.add (device ("ALSA", "CQ12T, USB Audio; Direct hardware device without any conversions", 16, 16));
        judge (r);
        check (r.mixer.state == State::found, "Linux ALSA CQ 16x16 -> mixer found");
    }

    // --- Report text and MIDI wording ----------------------------------------
    {
        DeviceReport r;
        r.audioTypeNames.add ("ASIO");
        r.audioDevices.add (device ("ASIO", "CQ ASIO Driver", 16, 16));
        judge (r);
        const auto text = r.toText();
        check (text.contains ("[FOUND]"),            "report shows a FOUND tag");
        check (text.contains ("16: Ch 16"),          "report numbers every channel");
        check (text.contains ("[NOT FOUND] RC-600"), "report shows the RC-600 as not found");

        AudioDeviceEntry leftAlone;
        leftAlone.typeName = "ASIO";
        leftAlone.name = "RC-600";
        leftAlone.note = "Installed, but left alone: only the CQ's ASIO driver is opened.";
        r.audioDevices.add (leftAlone);
        check (r.toText().contains ("RC-600\n      Installed, but left alone"), "an ASIO driver that was not opened says so");
    }

    check (MidiMonitor::describe (juce::MidiMessage::controllerEvent (1, 80, 127)) == "CC 80 = 127 (channel 1)",
           "CC message reads plainly");
    check (MidiMonitor::describe (juce::MidiMessage::programChange (1, 10)) == "Program change 11 (channel 1)",
           "program change is shown counting from 1");

    // --- Latency probe: find a ping in a recording ----------------------------
    {
        const double rate = 48000.0;
        const auto ping = LatencyProbe::makePing (rate);
        juce::Random random (1234);

        const auto recordingWith = [&] (int delay, float gain, float noise)
        {
            std::vector<float> recorded ((size_t) (rate * 0.35), 0.0f);

            for (auto& sample : recorded)
                sample = noise * (random.nextFloat() * 2.0f - 1.0f);

            if (delay >= 0)
                for (size_t i = 0; i < ping.size(); ++i)
                    recorded[(size_t) delay + i] += gain * ping[i];

            return recorded;
        };

        PingHit hit;
        auto recorded = recordingWith (257, 1.0f, 0.0f);
        check (LatencyProbe::findPing (recorded.data(), (int) recorded.size(), ping, hit) && hit.delaySamples == 257,
               "ping found at exactly 257 samples");

        recorded = recordingWith (1931, -0.05f, 0.002f);
        check (LatencyProbe::findPing (recorded.data(), (int) recorded.size(), ping, hit) && hit.delaySamples == 1931,
               "quiet, upside-down ping in hiss still found to the sample");

        recorded = recordingWith (0, 0.5f, 0.0f);
        check (LatencyProbe::findPing (recorded.data(), (int) recorded.size(), ping, hit) && hit.delaySamples == 0,
               "ping with no delay at all is found at zero");

        recorded = recordingWith (-1, 0.0f, 0.0f);
        check (! LatencyProbe::findPing (recorded.data(), (int) recorded.size(), ping, hit), "silence is not a ping");

        recorded = recordingWith (-1, 0.0f, 0.1f);
        check (! LatencyProbe::findPing (recorded.data(), (int) recorded.size(), ping, hit), "noise alone is not a ping");

        const auto ping96 = LatencyProbe::makePing (96000.0);
        check (ping96.size() == ping.size() * 2, "the ping lasts the same time at 96 kHz");
    }

    // --- Return routing ------------------------------------------------------
    check (returnToOutput (0, 16) == 8 && returnToOutput (7, 16) == 15, "CQ: returns 1 to 8 leave on USB channels 9 to 16");
    check (returnToOutput (1, 2) == 1 && returnToOutput (2, 2) == -1,    "a stereo device only has returns 1 and 2");
    check (returnName (4) == "Return 5 (USB left)",                      "return 5 is the CQ's USB stereo channel, left");
    check (inputName (9, true) == "input socket 10",                     "CQ USB channel 10 is input socket 10");
    check (inputName (14, true).contains ("inside the mixer"),           "CQ USB channel 15 is a copy from inside the mixer");

    // --- Boiling a run of pings down to one answer -----------------------------
    {
        const auto hit = [] (int input, int delay, float level) { PingHit h; h.input = input; h.delaySamples = delay; h.match = 0.9f; h.levelDb = level; return h; };

        // Cable on input 10, the mixer's own copy on 15 and 16, a microphone hearing a speaker on input 1.
        std::vector<std::vector<PingHit>> pings;

        for (int i = 0; i < 6; ++i)
            pings.push_back ({ hit (0, 900 + i, -40.0f), hit (9, 260 + (i == 2 ? 1 : 0), -14.0f), hit (14, 210, -12.0f), hit (15, 210, -12.0f) });

        pings[3].push_back (hit (5, 5000, -60.0f));   // one stray noise on input 6

        auto row = summarisePings (pings, 16, true);
        check (row.heard.size() == 4,                                         "a one-off noise on another input is ignored");
        check (row.best >= 0 && row.heard[(size_t) row.best].input == 9,      "the loudest input socket is the answer");
        check (row.best >= 0 && row.heard[(size_t) row.best].medianSamples == 260, "the answer is the middle value of the pings");
        check (row.bestIsAnalogue,                                            "and it counts as heard through a socket");

        for (auto& ping : pings)
            ping.erase (ping.begin(), ping.begin() + 2);   // no cable, no microphone

        row = summarisePings (pings, 16, true);
        check (row.best >= 0 && ! row.bestIsAnalogue, "with no cable, the mixer's own copy is used and flagged");

        // No cable, but a microphone hears the chirp from a speaker a few metres away, louder than the mixer's own copy.
        for (auto& ping : pings)
            ping.push_back (hit (5, 210 + 450, -6.0f));

        row = summarisePings (pings, 16, true, 48000.0);
        check (row.best >= 0 && ! row.bestIsAnalogue && row.heard[(size_t) row.best].medianSamples == 210,
               "a microphone hearing a speaker is not taken for the cable: the mixer's own copy is the answer");
        check (row.acousticInput == 5 && row.acousticSamples == 660, "and the report can say which socket heard it through the air");

        for (auto& ping : pings)
            ping.back() = hit (9, 210 + 17, -14.0f);    // a real cable: a third of a millisecond after the mixer's copy

        row = summarisePings (pings, 16, true, 48000.0);
        check (row.bestIsAnalogue && row.heard[(size_t) row.best].input == 9 && row.acousticInput < 0, "a real cable is still the answer");

        // The driver's delay creeping up at the same setting.
        std::vector<DriverDelayNote> history { { 96000, 64, 384, 512, 1000 }, { 96000, 64, 384, 224, 2000 }, { 96000, 128, 448, 320, 3000 } };
        check (driverDelayGrown (history, 96000, 64, 384 + 1856) != nullptr
                 && driverDelayGrown (history, 96000, 64, 384 + 1856)->total() == 608, "23 ms where the driver once said 6 ms is flagged, against the lowest seen");
        check (driverDelayGrown (history, 96000, 64, 896) == nullptr, "the 3 ms that safe mode adds is not flagged");

        const juce::Array<int> cqSizes { 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 };
        check (AudioEngine::nudgeBufferSize (64, cqSizes) == 128, "start-up nudge for the CQ: 64 is opened at 128 first");
        check (AudioEngine::nudgeBufferSize (4096, cqSizes) == 2048, "start-up nudge: the biggest size goes down instead of up");
        check (AudioEngine::nudgeBufferSize (64, { 64 }) == 0 && AudioEngine::nudgeBufferSize (0, cqSizes) == 0,
               "start-up nudge: nothing to do with only one size, or no size");
        check (driverDelayGrown (history, 96000, 256, 4000) == nullptr, "a setting never seen before is not flagged");

        row = summarisePings ({ {}, {}, {} }, 16, true);
        check (row.best < 0 && row.pingsSent == 3, "nothing heard gives no answer");
    }

    // --- The whole engine against a pretend loopback cable ---------------------
    {
        constexpr int ins = 16, outs = 16, maxBlock = 256, cableDelay = 311;
        constexpr int cableFrom = 12;   // return 5: USB channel 13, the CQ's USB stereo channel, left
        constexpr int cableTo = 9;      // input socket 10

        AudioEngine engine;
        engine.openSimulated (48000.0, 64, ins, outs);

        std::vector<std::vector<float>> inData (ins, std::vector<float> (maxBlock, 0.0f)),
                                        outData (outs, std::vector<float> (maxBlock, 0.0f));
        std::vector<const float*> inPointers;
        std::vector<float*> outPointers;

        for (auto& channel : inData)   inPointers.push_back (channel.data());
        for (auto& channel : outData)  outPointers.push_back (channel.data());

        std::vector<float> cable (cableDelay, 0.0f);
        size_t cablePosition = 0;

        const auto runAudio = [&] (int samples)
        {
            for (int done = 0; done < samples;)
            {
                const auto block = engine.bufferSize();

                for (int i = 0; i < block; ++i)
                    inData[cableTo][(size_t) i] = cable[(cablePosition + (size_t) i) % cable.size()];

                engine.processBlock (inPointers.data(), ins, outPointers.data(), outs, block);

                for (int i = 0; i < block; ++i)
                    cable[(cablePosition + (size_t) i) % cable.size()] = 0.5f * outData[cableFrom][(size_t) i];

                cablePosition = (cablePosition + (size_t) block) % cable.size();
                done += block;
            }
        };

        engine.measure ({ 64, 256, 999 });

        for (int tick = 0; tick < 2000 && engine.isMeasuring(); ++tick)
        {
            runAudio (2400);    // a twentieth of a second, like the screen's timer
            engine.tick();
        }

        const auto& rows = engine.latencyRows();
        check (! engine.isMeasuring(),                      "a measurement run finishes");
        check (rows.size() == 2,                            "one row per buffer size the device offers");
        check (engine.bufferSize() == 64,                   "the buffer size goes back to where it started");

        for (const auto& row : rows)
        {
            const bool right = row.best >= 0 && row.bestIsAnalogue
                                 && row.heard[(size_t) row.best].input == cableTo
                                 && row.heard[(size_t) row.best].medianSamples == cableDelay
                                 && row.heard[(size_t) row.best].minSamples == row.heard[(size_t) row.best].maxSamples;
            check (right, "pretend cable of " + juce::String (cableDelay) + " samples measured exactly at a buffer of "
                            + juce::String (row.bufferSize));
        }

        check (rows.size() == 2 && rows[1].bufferSize == 256, "the second row was measured at 256");
        check (engine.reportText().contains ("round trip 6.48 ms (311 samples), heard on input socket 10"),
               "the report gives the round trip in plain words");

        // Play-through: straight, then mixed, then the feedback guard.
        std::fill (cable.begin(), cable.end(), 0.0f);
        for (auto& channel : inData)
            std::fill (channel.begin(), channel.end(), 0.0f);

        std::fill (inData[2].begin(), inData[2].end(), 0.25f);
        engine.processBlock (inPointers.data(), ins, outPointers.data(), outs, 64);
        check (juce::approximatelyEqual (outData[10][0], 0.0f), "play-through is off to begin with");

        engine.setPlayThrough (PlayThrough::straight);
        engine.processBlock (inPointers.data(), ins, outPointers.data(), outs, 64);
        check (juce::approximatelyEqual (outData[10][5], 0.25f) && juce::approximatelyEqual (outData[11][5], 0.0f),
               "straight play-through sends input 3 to return 3 only");

        engine.setPlayThrough (PlayThrough::mixToUsb);
        engine.processBlock (inPointers.data(), ins, outPointers.data(), outs, 64);
        check (juce::approximatelyEqual (outData[12][5], 0.25f) && juce::approximatelyEqual (outData[13][5], 0.25f)
                 && juce::approximatelyEqual (outData[10][5], 0.0f),
               "mixed play-through sends the inputs to the USB stereo channel");

        std::fill (inData[8].begin(), inData[8].end(), 0.125f);    // bass on input socket 9
        std::fill (inData[14].begin(), inData[14].end(), 0.5f);    // the mixer's own output coming back on USB 15
        engine.processBlock (inPointers.data(), ins, outPointers.data(), outs, 64);
        check (juce::approximatelyEqual (outData[12][5], 0.375f),
               "mixed play-through includes input 9 and leaves out the mixer's own copies");
        std::fill (inData[8].begin(), inData[8].end(), 0.0f);
        std::fill (inData[14].begin(), inData[14].end(), 0.0f);

        engine.setPlayThrough (PlayThrough::off);
        engine.setToneReturn (0);
        engine.processBlock (inPointers.data(), ins, outPointers.data(), outs, 64);
        float tonePeak = 0.0f, elsewhere = 0.0f;

        for (int i = 0; i < 64; ++i)
        {
            tonePeak = juce::jmax (tonePeak, std::abs (outData[8][(size_t) i]));
            elsewhere = juce::jmax (elsewhere, std::abs (outData[9][(size_t) i]));
        }

        check (tonePeak > 0.01f && tonePeak <= 0.126f && juce::approximatelyEqual (elsewhere, 0.0f),
               "the test tone sounds on the chosen return only");
        engine.setToneReturn (-1);

        engine.setPlayThrough (PlayThrough::straight);
        std::fill (inData[2].begin(), inData[2].end(), 1.5f);   // a howl: stuck past full level

        for (int block = 0; block < 400; ++block)
            engine.processBlock (inPointers.data(), ins, outPointers.data(), outs, 64);

        check (engine.feedbackGuardTripped() && engine.playThrough() == PlayThrough::off,
               "the feedback guard switches play-through off");
        check (outData[10][5] <= 1.0f && juce::approximatelyEqual (outData[10][5], 0.0f), "and the output is silent afterwards");
    }

    // --- The glitch check against a pretend cable --------------------------------
    {
        constexpr int ins = 16, outs = 16, block = 64, cableDelay = 606;
        constexpr int cableFrom = 12, cableTo = 9;

        AudioEngine engine;
        engine.openSimulated (96000.0, block, ins, outs);

        std::vector<std::vector<float>> inData (ins, std::vector<float> (block, 0.0f)),
                                        outData (outs, std::vector<float> (block, 0.0f));
        std::vector<const float*> inPointers;
        std::vector<float*> outPointers;

        for (auto& channel : inData)   inPointers.push_back (channel.data());
        for (auto& channel : outData)  outPointers.push_back (channel.data());

        std::vector<float> cable (cableDelay, 0.0f);
        size_t cablePosition = 0;
        float cableGain = 0.5f;
        juce::Random hiss (7);

        // fault: 0 = none, 1 = one block of silence arrives, 2 = 96 samples go missing
        const auto runBlocks = [&] (int blocks, int fault = 0)
        {
            for (int b = 0; b < blocks; ++b)
            {
                if (fault == 2 && b == blocks / 2)
                    cablePosition = (cablePosition + 96) % cable.size();

                for (int i = 0; i < block; ++i)
                {
                    const auto sound = fault == 1 && b == blocks / 2 ? 0.0f : cable[(cablePosition + (size_t) i) % cable.size()];
                    inData[cableTo][(size_t) i] = sound + 0.00002f * (hiss.nextFloat() - 0.5f);
                    inData[0][(size_t) i] = 0.05f * (hiss.nextFloat() - 0.5f);     // an open microphone on input 1
                }

                engine.processBlock (inPointers.data(), ins, outPointers.data(), outs, block);

                for (int i = 0; i < block; ++i)
                    cable[(cablePosition + (size_t) i) % cable.size()] = cableGain * outData[cableFrom][(size_t) i];

                cablePosition = (cablePosition + (size_t) block) % cable.size();
            }
        };

        using State = GlitchStats::State;
        check (engine.glitchStats().state == State::off, "the glitch check is off to begin with");

        engine.setPlayThrough (PlayThrough::mixToUsb);
        engine.setGlitchCheck (true);
        runBlocks (3000);   // two seconds

        auto found = engine.glitchStats();
        check (found.state == State::locked && found.input == cableTo && found.analogue,
               "the glitch check finds its tone on the cable, not on a noisy microphone");
        check (found.glitches == 0, "a clean tone counts no glitches");
        check (std::abs (found.levelDb - (-32.0f)) < 1.0f, "and reports how loud the tone came back");

        float peak = 0.0f;
        for (int i = 0; i < block; ++i)
            peak = juce::jmax (peak, std::abs (outData[cableFrom][(size_t) i]));
        check (peak > 0.01f && peak <= 0.1001f, "play-through is silent while the glitch check runs: only the tone goes out");

        runBlocks (45000);  // thirty seconds of clean tone
        check (engine.glitchStats().glitches == 0, "thirty clean seconds still count no glitches");

        runBlocks (1500, 1);
        check (engine.glitchStats().glitches == 1, "one block of silence counts as one glitch");

        runBlocks (3000);
        runBlocks (1500, 2);
        runBlocks (3000);
        found = engine.glitchStats();
        check (found.glitches == 2, "96 samples going missing counts as one more");
        check (found.times.size() == 2 && found.times[0] > 31.0 && found.times[0] < 33.0, "each glitch is timed");
        check (engine.reportText().contains ("Glitches heard: 2") && engine.reportText().contains ("a single break"),
               "the report gives the glitch count");

        // A lead being unplugged: crackle for two seconds on the input the check is listening to.
        for (int burst = 0; burst < 20; ++burst)
            runBlocks (150, 1);

        found = engine.glitchStats();
        check (found.glitches == 3 && found.breaks >= 12 && found.breaksEach.size() == 3 && found.breaksEach[2] >= 10,
               "a run of breaks close together counts as one glitch, with its breaks noted");
        check (engine.reportText().contains ("a burst of"), "and the report calls it a burst");

        engine.resetDropoutStats();
        runBlocks (1500);
        check (engine.glitchStats().glitches == 0, "Reset count clears the glitch count");

        cableGain = 0.0f;   // the fader is pulled down
        runBlocks (6000);
        check (engine.glitchStats().state != State::locked, "a tone that goes away is reported as missing");

        cableGain = 0.5f;
        runBlocks (3000);
        check (engine.glitchStats().state == State::locked, "and it is found again when it comes back");

        engine.resetDropoutStats();
        runBlocks (1500, 1);
        engine.setGlitchCheck (false);
        check (engine.glitchStats().state == State::off && engine.glitchStats().glitches == 1
                 && engine.reportText().contains ("this is the last run") && engine.reportText().contains ("Glitches heard: 1"),
               "stopping the glitch check keeps its result in the report");

        engine.setGlitchCheck (true);
        check (engine.glitchStats().glitches == 0, "starting it again begins a fresh count");

        engine.measure ({});
        check (! engine.glitchCheckOn(), "starting a latency measurement stops the glitch check");
    }

    // ---- the looper inside the engine: pedal in, loop out on returns 5 and 6 ----
    {
        AudioEngine engine;
        const int ins = 16, outs = 16, block = 64;
        engine.openSimulated (48000.0, block, ins, outs);

        std::vector<std::vector<float>> in ((size_t) ins, std::vector<float> ((size_t) block, 0.0f)), out ((size_t) outs, std::vector<float> ((size_t) block, 0.0f));
        std::vector<const float*> inPointers;
        std::vector<float*> outPointers;

        for (auto& c : in)   inPointers.push_back (c.data());
        for (auto& c : out)  outPointers.push_back (c.data());

        std::fill (in[6].begin(), in[6].end(), 0.1f);       // a steady signal on socket 7, the acoustic

        const auto runFor = [&] (double seconds)
        {
            for (int i = 0; i < (int) (seconds * 48000.0 / block); ++i)
                engine.processBlock (inPointers.data(), ins, outPointers.data(), outs, block);
        };

        const auto tap = [&] (int sw)
        {
            PedalEvent e;
            e.sw = sw;
            e.kind = PedalEvent::Kind::down;
            engine.looper().pedal (e);
            runFor (0.01);
            e.kind = PedalEvent::Kind::up;
            engine.looper().pedal (e);
            runFor (0.01);
        };

        runFor (0.1);
        check (std::abs (out[12][0]) < 1.0e-6f, "with no loop recorded the looper sends nothing");
        tap (0);
        check (engine.looper().snapshot().track[0].state == TrackState::recording, "in the engine, switch 1 starts track 1 recording");
        runFor (2.0);
        tap (0);
        runFor (1.0);
        check (engine.looper().snapshot().track[0].state == TrackState::playing, "and the second press plays it");
        check (std::abs (out[12][0] - 0.1f) < 1.0e-4f && std::abs (out[13][0] - 0.1f) < 1.0e-4f, "the loop comes back on returns 5 and 6 (the CQ's USB stereo channel)");
        check (std::abs (out[8][0]) < 1.0e-6f && std::abs (out[10][0]) < 1.0e-6f, "and on no other return");

        engine.setGlitchCheck (true);
        runFor (0.2);
        engine.setGlitchCheck (false);
        runFor (0.2);
        check (engine.looper().snapshot().track[0].state == TrackState::playing, "a glitch check pauses the looper without losing the loop");
    }

    runKickPadSelfTest (check);
    runLooperSelfTest (check);
    runEffectsSelfTest (check);
    runSongSelfTest (check);
    runMixerSelfTest (check);
    runRecordingsSelfTest (check);
    runRemoteSelfTest (check);
    runBreakMusicSelfTest (check);
    runExpressionSelfTest (check);
    runTunerSelfTest (check);

    passed = failures == 0;
    log.add ({});
    log.add (passed ? "Self-test: passed (" + juce::String (log.size() - 1) + " checks)"
                    : "Self-test: FAILED (" + juce::String (failures) + " checks)");

    return log.joinIntoString ("\n") + "\n";
}

} // namespace gigrig
