#include "AudioEngine.h"

#include <cmath>
#include <functional>

#if JUCE_LINUX
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

// Checks for the break music (27 Nov 2026), run by the automatic builds (GigRig --self-test):
// that nothing plays until Play is pressed, that a mix plays round and round at the level set,
// that it fades down when Tom starts playing and NEVER comes back by itself, that it comes back
// by hand no louder than before, that no fade clicks, that with the mixer on it reaches the
// main output only and is in no recording and no loop, the knob (ENC 5), the settings file,
// the sample-rate change, and on Linux the phone's sound arriving through the pipe.

namespace gigrig
{

namespace
{
    using Check = std::function<void (bool, const juce::String&)>;
    constexpr int block = 64;
    constexpr double twoPi = 6.283185307179586;
    constexpr double leftHz = 440.0, rightHz = 660.0, mixLevel = 0.5;

    /** The engine with a pretend device. Each block the music's queue is topped up first, as
        its background thread would have done in real time. */
    struct Bench
    {
        AudioEngine engine;
        double rate;
        std::vector<std::vector<float>> ins, outs;
        std::vector<const float*> inPtrs;
        std::vector<float*> outPtrs;
        std::array<std::vector<float>, numReturns> heard;
        juce::int64 t = 0;
        std::function<float (int socket, juce::int64 time)> source = [] (int, juce::int64) { return 0.0f; };

        explicit Bench (double sampleRate = 96000.0) : rate (sampleRate)
        {
            engine.openSimulated (rate, block, 16, 16);
            ins.assign (16, std::vector<float> (block, 0.0f));
            outs.assign (16, std::vector<float> (block, 0.0f));
            for (auto& v : ins)  inPtrs.push_back (v.data());
            for (auto& v : outs) outPtrs.push_back (v.data());
        }

        void run (double seconds)
        {
            const int blocks = (int) std::ceil (seconds * rate / block);

            for (int b = 0; b < blocks; ++b)
            {
                for (int s = 0; s < 16; ++s)
                    for (int i = 0; i < block; ++i)
                        ins[(size_t) s][(size_t) i] = source (s, t + i);

                t += block;
                engine.breakMusic().fillNow();
                engine.processBlock (inPtrs.data(), 16, outPtrs.data(), 16, block);

                for (int r = 0; r < numReturns; ++r)
                {
                    const auto& o = outs[(size_t) returnToOutput (r, 16)];
                    heard[(size_t) r].insert (heard[(size_t) r].end(), o.begin(), o.end());
                }

                if (b % 16 == 15)
                    engine.mixer().tick();
            }
        }

        std::vector<float> last (int ret, double seconds) const
        {
            const auto& h = heard[(size_t) ret];
            const auto n = juce::jmin (h.size(), (size_t) (seconds * rate));
            return { h.end() - (std::ptrdiff_t) n, h.end() };
        }

        void forget()
        {
            for (auto& h : heard)
                h.clear();
        }
    };

    double strength (const std::vector<float>& x, double hz, double rate)
    {
        if (x.empty())
            return 0.0;

        const double w = twoPi * hz / rate, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;

        for (auto v : x)
        {
            const double s0 = v + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        return 2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (double) x.size();
    }

    float peak (const std::vector<float>& x)
    {
        float p = 0.0f;
        for (auto v : x) p = std::max (p, std::abs (v));
        return p;
    }

    float biggestStep (const std::vector<float>& x)
    {
        float s = 0.0f;
        for (size_t i = 1; i < x.size(); ++i)
            s = std::max (s, std::abs (x[i] - x[i - 1]));
        return s;
    }

    bool allFinite (const std::vector<float>& x)
    {
        for (auto v : x)
            if (! std::isfinite (v))
                return false;
        return true;
    }

    juce::String db (double gain)   { return juce::String (20.0 * std::log10 (std::max (gain, 1.0e-12)), 1) + " dB"; }

    /** A mix for the test: a whole number of cycles, so it runs round into itself with no step.
        440 Hz on the left and 660 Hz on the right, so the two sides can be told apart. */
    void writeMix (const juce::File& file, double fileRate, double seconds, int channels = 2)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        const int n = (int) (fileRate * seconds);
        juce::AudioBuffer<float> buffer (channels, n);

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
                buffer.setSample (ch, i, (float) (mixLevel * std::sin (twoPi * (ch == 0 ? leftHz : rightHz) * i / fileRate)));

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());

        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (fileRate).withNumChannels (channels).withBitsPerSample (24)))
            writer->writeFromAudioSampleBuffer (buffer, 0, n);
    }

    std::vector<float> readWav (const juce::File& file, int channel)
    {
        std::vector<float> data;
        juce::WavAudioFormat wav;

        if (auto reader = std::unique_ptr<juce::AudioFormatReader> (wav.createReaderFor (file.createInputStream().release(), true)))
        {
            juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
            const int ch = juce::jmin (channel, buffer.getNumChannels() - 1);
            data.assign (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + buffer.getNumSamples());
        }

        return data;
    }
}

void runBreakMusicSelfTest (const Check& check)
{
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GigRig self-test break music");
    temp.deleteRecursively();
    const auto folder = temp.getChildFile ("Break music");
    writeMix (folder.getChildFile ("Chill.wav"), 44100.0, 1.0);
    writeMix (folder.getChildFile ("Party.wav"), 48000.0, 2.0);
    writeMix (folder.getChildFile ("One side.wav"), 44100.0, 1.0, 1);
    folder.getChildFile ("notes.txt").replaceWithText ("not music");

    const double usual = std::pow (10.0, -12.0 / 20.0) * mixLevel;     // the level it starts at (-12 dB) times the file's own

    // ---- the resampler ----
    {
        BreakMusic::Resampler r;
        std::vector<float> in (4410), outL (12000), outR (12000);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = (float) std::sin (twoPi * 1000.0 * (double) i / 44100.0);

        // Step 1 (same rate): what goes in comes out, two samples later, to the last bit.
        int made = r.push (in.data(), in.data(), (int) in.size(), 1.0, outL.data(), outR.data(), (int) outL.size());
        float worst = made == (int) in.size() ? 0.0f : 1000.0f;
        for (int i = 2; i < made; ++i)
            worst = std::max (worst, std::abs (outL[(size_t) i] - in[(size_t) (i - 2)]));
        check (worst == 0.0f, "break music: at the interface's own rate the music is passed on untouched, sample for sample (" + juce::String (made) + " of " + juce::String ((int) in.size()) + ")");

        // 44.1 kHz up to 96 kHz, fed in uneven pieces: the right amount comes out and the note is still clean.
        r.reset();
        made = 0;
        const double step = 44100.0 / 96000.0;
        for (size_t at = 0; at < in.size();)
        {
            const int n = (int) std::min (in.size() - at, (size_t) (37 + (at % 5) * 100));
            made += r.push (in.data() + at, in.data() + at, n, step, outL.data() + made, outR.data() + made, (int) outL.size() - made);
            at += (size_t) n;
        }
        const std::vector<float> body (outL.begin() + 100, outL.begin() + made - 100);
        const double note = strength (body, 1000.0, 96000.0);
        const double dirt = std::max ({ strength (body, 2000.0, 96000.0), strength (body, 3000.0, 96000.0), strength (body, 43100.0, 96000.0) });
        check (std::abs (made - 9600) <= 2 && std::abs (note - 1.0) < 0.01 && dirt < 0.01 && biggestStep (body) < 0.07f,
               "break music: a 44.1 kHz file is brought to 96 kHz cleanly however it arrives (" + juce::String (made) + " samples from 4410, the note at " + db (note) + ", anything else under " + db (dirt) + ")");
    }

    // ---- nothing plays until Play is pressed; then a mix plays round and round at its level ----
    {
        Bench b;
        auto& music = b.engine.breakMusic();
        music.setFolder (folder);
        const auto list = music.mixes();
        check (list.size() == 3 && list[0] == "Chill" && list[2] == "Party", "break music: the mixes are the music files in the folder, A to Z, and nothing else (" + list.joinIntoString (", ") + ")");

        b.run (0.5);
        float loudest = 0.0f;
        for (int r = 0; r < numReturns; ++r)
            loudest = std::max (loudest, peak (b.heard[(size_t) r]));
        check (loudest == 0.0f && ! music.isPlaying(), "break music: nothing plays until Play is pressed (an app that has just started is silent)");

        check (music.play ("No such mix").isNotEmpty() && ! music.isPlaying(), "break music: asking for a mix that is not in the folder says so and plays nothing");

        const auto error = music.play ("Chill");
        b.run (0.2);
        const auto early = b.last (4, 0.1);
        b.run (5.0);        // past the 4 second fade up
        b.forget();
        b.run (3.0);        // three times round the one-second file
        const auto left = b.last (4, 3.0), right = b.last (5, 3.0);
        const double l = strength (left, leftHz, b.rate), r = strength (right, rightHz, b.rate), crossed = strength (left, rightHz, b.rate);
        float elsewhere = 0.0f;
        for (int ret : { 0, 1, 2, 3, 6, 7 })
            elsewhere = std::max (elsewhere, peak (b.heard[(size_t) ret]));
        check (error.isEmpty() && std::abs (l / usual - 1.0) < 0.01 && std::abs (r / usual - 1.0) < 0.01 && crossed < usual * 0.01 && elsewhere == 0.0f,
               "break music: with the mixer off a mix plays in stereo on returns 5 and 6 only, at the level set (left " + db (l) + ", right " + db (r) + ", wanted " + db (usual) + ")");
        check (peak (early) < usual * 0.2 && peak (early) > 0.0f, "break music: Play fades the music in, it does not jump in (" + db (peak (early)) + " after a fifth of a second)");
        check (biggestStep (left) < (float) (usual * twoPi * leftHz / b.rate * 1.3) && allFinite (left),
               "break music: the mix runs round into its start with no click (biggest step " + juce::String (biggestStep (left), 5) + ")");

        // ---- Tom starts playing: it fades down, keeps going silently, and never comes back by itself ----
        b.forget();
        music.startedPlaying (BreakMusic::Started::footswitch);
        b.run (2.5);
        const auto fading = b.heard[4];
        const auto after = b.last (4, 0.2);
        b.forget();
        b.run (20.0);
        const float later = std::max (peak (b.heard[4]), peak (b.heard[5]));
        check (peak (after) == 0.0f && biggestStep (fading) < (float) (usual * twoPi * leftHz / b.rate * 1.3) && music.fadedByPlaying() == 1,
               "break music: a footswitch press fades the music away in 2 seconds with no click");
        check (later == 0.0f && music.isPlaying() && ! music.isUp(), "break music: once faded down it NEVER comes back by itself (20 seconds of silence, still playing underneath)");

        // ---- by hand: back to the same level, never louder ----
        music.bringUp();
        b.run (1.0);
        const auto rising = peak (b.last (4, 0.1));
        b.run (4.0);
        b.forget();
        b.run (1.0);
        const double back = strength (b.heard[4], leftHz, b.rate);
        check (rising > 0.0f && rising < usual * 0.5 && std::abs (back / usual - 1.0) < 0.01 && peak (b.heard[4]) <= usual * 1.01,
               "break music: brought up by hand it comes back slowly to the level it had, never louder (" + db (back) + ")");

        // ---- the level ----
        music.setLevelDb (-18.0f);
        b.run (0.5);
        const auto gliding = b.last (4, 0.5);
        b.forget();
        b.run (0.5);
        const double quieter = strength (b.heard[4], leftHz, b.rate);
        check (std::abs (quieter / (usual * 0.5012) - 1.0) < 0.01 && biggestStep (gliding) < (float) (usual * twoPi * leftHz / b.rate * 1.5),
               "break music: the level slider moves the music by the amount shown, with no click (" + db (quieter) + ")");
        music.setLevelDb (-12.0f);

        // ---- the kinds of start that are not ticked do nothing ----
        b.run (0.5);
        music.startedPlaying (BreakMusic::Started::song);
        b.source = [&] (int socket, juce::int64 t) { return socket == 5 ? (float) (0.3 * std::sin (twoPi * 200.0 * (double) t / b.rate)) : 0.0f; };
        b.run (1.0);
        check (music.isUp() && music.fadedByPlaying() == 1, "break music: a song loading or sound on an input does not fade the music unless that is ticked");

        auto s = music.settings();
        s.onSound = true;
        s.soundDb = -20.0f;
        music.setSettings (s);
        b.source = [&] (int socket, juce::int64 t) { return socket == 5 ? (float) (0.01 * std::sin (twoPi * 200.0 * (double) t / b.rate)) : 0.0f; };
        b.run (1.0);
        const bool quietIgnored = music.isUp();
        b.source = [&] (int socket, juce::int64 t) { return socket == 5 ? (float) (0.3 * std::sin (twoPi * 200.0 * (double) t / b.rate)) : 0.0f; };
        b.run (0.1);
        const bool notYet = music.isUp();
        b.run (0.2);
        check (quietIgnored && notYet && ! music.isUp() && music.fadedByPlaying() == 2,
               "break music: with 'an input is louder than' ticked, sound over the mark for a sixth of a second fades the music, and quieter sound does not");
        b.source = [] (int, juce::int64) { return 0.0f; };
        s.onSound = false;
        s.onSwitch = false;
        music.setSettings (s);
        music.bringUp();
        b.run (0.2);
        music.startedPlaying (BreakMusic::Started::footswitch);
        check (music.isUp(), "break music: with 'a footswitch is pressed' unticked, a footswitch leaves the music alone");

        // ---- Stop, and another mix ----
        music.stop();
        b.run (1.0);
        b.forget();
        b.run (0.5);
        check (peak (b.heard[4]) == 0.0f && ! music.isPlaying(), "break music: Stop fades out and then there is silence");

        // A stereo pair of returns chosen for the mixer-off case, a mono file, and a file at another rate.
        s.returnPair = 0;
        music.setSettings (s);
        check (music.play ("One side").isEmpty(), "break music: a mono file can be played");
        b.run (6.0);
        b.forget();
        b.run (1.0);
        const double monoL = strength (b.heard[0], leftHz, b.rate), monoR = strength (b.heard[1], leftHz, b.rate);
        check (std::abs (monoL / usual - 1.0) < 0.01 && std::abs (monoR / usual - 1.0) < 0.01 && peak (b.heard[4]) == 0.0f,
               "break music: a mono file plays on both sides, and the pair of returns chosen for 'mixer off' is the pair used (" + db (monoL) + ", " + db (monoR) + ")");

        music.play ("Party");
        b.run (0.3);
        b.forget();
        b.run (5.0);
        b.forget();
        b.run (2.0);
        const double party = strength (b.heard[0], leftHz, b.rate);
        check (std::abs (party / usual - 1.0) < 0.01 && music.mixName() == "Party" && std::abs (music.lengthSeconds() - 2.0) < 0.01,
               "break music: choosing another mix while one plays goes to it, and a 48 kHz file plays at the right pitch and level (" + db (party) + ")");

        // ---- the place in a mix is remembered ----
        music.play ("Chill", true);
        b.run (0.4);
        music.stop();
        b.run (1.0);        // it fades out for a moment more, then stays where it is
        const double left1 = music.positionSeconds();
        music.play ("Chill");
        b.run (0.01);
        const double resumed = music.positionSeconds();
        music.play ("Chill", true);
        b.run (0.01);
        const double restarted = music.positionSeconds();
        check (left1 > 0.5 && left1 < 0.95 && std::abs (resumed - left1) < 0.05 && restarted < 0.05,
               "break music: Play carries on from where the mix was left, and 'Play from the start' starts it again (left at " + juce::String (left1, 2) + " s, back at " + juce::String (resumed, 2) + " s)");

        // ---- the settings file ----
        s.fadeDownSeconds = 3.5;
        s.fadeUpSeconds = 6.0;
        s.onSong = true;
        s.soundDb = -24.0f;
        s.returnPair = 1;
        music.setSettings (s);
        music.setLevelDb (-7.5f);
        const auto text = music.stateText();
        BreakMusic other;
        other.loadStateText (text);
        check (other.settings() == music.settings() && other.levelDb() == -7.5f && other.mixName() == "Chill" && ! other.isPlaying() && other.stateText() == text,
               "break music: the level, the fades, the ticks and the mix that played last come back from the app's memory file, and nothing starts playing by itself");

        const auto report = music.reportText();
        check (report.contains ("Break music") && report.contains ("3 mixes") && report.contains ("by hand only"), "break music: the report has its part");
    }

    // ---- with the mixer on: the main output only, and never in a recording or a loop ----
    {
        Bench b (48000.0);
        auto& music = b.engine.breakMusic();
        music.setFolder (folder);
        auto s = defaultMixerSetup();

        for (auto& ch : s.channel)
        {
            ch.on = false;
            for (auto& slot : ch.strip.slots) slot.on = false;
        }
        for (auto& f : s.fx)
            f.on = false;
        for (auto& o : s.output)
            for (auto& slot : o.chain.slots) slot.on = false;

        s.channel[0].on = true;             // the vocal, socket 6
        s.channel[0].monDb = { 0.0f, 0.0f };
        s.output[0].mono = false;
        s.output[0].levelDb = -6.0f;
        s.output[0].firstReturn = 4;
        s.output[1].firstReturn = 0;
        s.output[2].firstReturn = 2;
        b.engine.mixer().setAll (s);
        b.engine.mixer().setOn (true);
        b.source = [&] (int socket, juce::int64 t) { return socket == s.channel[0].socket ? (float) (0.1 * std::sin (twoPi * 1000.0 * (double) t / b.rate)) : 0.0f; };

        auto set = music.settings();
        set.onSwitch = false;
        music.setSettings (set);
        music.setLevelDb (-12.0f);
        music.play ("Chill", true);
        b.run (5.0);
        const auto error = b.engine.mixer().startRecording (temp.getChildFile ("recordings"));
        b.forget();
        b.run (1.0);
        b.engine.mixer().stopRecording();

        const double mainMusic = strength (b.heard[4], leftHz, b.rate), mainRight = strength (b.heard[5], rightHz, b.rate), mainVoice = strength (b.heard[4], 1000.0, b.rate);
        const double monitorMusic = std::max ({ strength (b.heard[0], leftHz, b.rate), strength (b.heard[1], rightHz, b.rate), strength (b.heard[2], leftHz, b.rate), strength (b.heard[3], rightHz, b.rate) });
        const double monitorVoice = strength (b.heard[0], 1000.0, b.rate);
        check (std::abs (mainMusic / (usual * 0.5012) - 1.0) < 0.01 && std::abs (mainRight / (usual * 0.5012) - 1.0) < 0.01 && std::abs (mainVoice / 0.05012 - 1.0) < 0.01,
               "break music: with the mixer on it joins the main output in stereo and follows the main level (music " + db (mainMusic) + " with the main at -6 dB)");
        check (monitorMusic < 1.0e-6 && monitorVoice > 0.09, "break music: it is never in a monitor (the voice is, at " + db (monitorVoice) + "; the music is at " + db (monitorMusic) + ")");

        auto& rec = b.engine.mixer().recorder();
        juce::String mainName;
        for (const auto& name : rec.fileNames())
            if (name.containsIgnoreCase ("main"))
                mainName = name;
        const auto recorded = readWav (rec.folder().getChildFile (mainName), 0);
        const double recMusic = strength (recorded, leftHz, b.rate), recVoice = strength (recorded, 1000.0, b.rate);
        check (error.isEmpty() && recorded.size() == 48000 && recMusic < 1.0e-5 && std::abs (recVoice / 0.05012 - 1.0) < 0.01,
               "break music: it is never in a recording: the main mix file holds the voice (" + db (recVoice) + ") and none of the music (" + db (recMusic) + ")");

        // A loop recorded while the music plays holds none of it.
        SongSetup song;
        juce::String ("Break test").copyToUTF8 (song.name, sizeof (song.name));
        song.quantise = false;
        song.track[0].rcInputs = rcMic1;
        song.bank[0][0] = 1;
        b.engine.looper().setSong (song);
        const auto tap = [&]
        {
            PedalEvent e;
            e.kind = PedalEvent::Kind::down;
            e.sw = 0;
            b.engine.looper().pedal (e);
            b.run (0.01);
            e.kind = PedalEvent::Kind::up;
            b.engine.looper().pedal (e);
            b.run (0.01);
        };
        b.run (0.2);
        tap();
        b.run (1.0);
        tap();
        b.source = [] (int, juce::int64) { return 0.0f; };
        music.stop();
        b.run (1.5);
        b.forget();
        b.run (1.0);
        const double loopVoice = strength (b.heard[4], 1000.0, b.rate), loopMusic = std::max (strength (b.heard[4], leftHz, b.rate), strength (b.heard[5], rightHz, b.rate));
        check (loopVoice > 0.02 && loopMusic < 3.0e-4, "break music: it is never in a loop: a loop recorded while it played holds the voice (" + db (loopVoice) + ") and none of the music (" + db (loopMusic) + ")");

        // Mono main output: both sides carry the two sides of the music summed, and the main mute silences it.
        s.output[0].mono = true;
        b.engine.mixer().setAll (s);
        b.engine.looper().setSong (song);       // clears the loop
        music.play ("Chill", true);
        b.run (5.0);
        b.forget();
        b.run (1.0);
        const double monoMain = strength (b.heard[4], leftHz, b.rate), monoOther = strength (b.heard[5], leftHz, b.rate);
        s.output[0].mute = true;
        b.engine.mixer().setAll (s);
        b.run (0.5);
        b.forget();
        b.run (0.5);
        check (std::abs (monoMain / (usual * 0.5012 * 0.5) - 1.0) < 0.02 && std::abs (monoOther / monoMain - 1.0) < 0.01 && peak (b.heard[4]) == 0.0f,
               "break music: a mono main output carries both sides of the music on each return, and muting the main output mutes the music too");
    }

    // ---- the interface at 44.1 kHz and at 96 kHz ----
    for (const double rate : { 44100.0, 96000.0 })
    {
        Bench b (rate);
        auto& music = b.engine.breakMusic();
        music.setFolder (folder);
        music.play ("Party", true);
        b.run (5.0);
        b.forget();
        b.run (2.0);
        const double level = strength (b.heard[4], leftHz, rate);
        check (std::abs (level / usual - 1.0) < 0.01, "break music: a 48 kHz file plays at the right pitch with the interface at " + juce::String (rate / 1000.0, 1) + " kHz (" + db (level) + ")");
    }

    // ---- the knob (ENC 5): rests on PA/venue, a hold hands it to the break music ----
    {
        Bench b;
        auto& music = b.engine.breakMusic();
        auto& knob = b.engine.venueKnob();
        music.setFolder (folder);
        b.engine.mixer().setPresetRoot (temp.getChildFile ("Mixer"));
        double now = 1000.0;

        const bool rests = ! knob.onBreakMusic() && knob.screen (now).contains ("Venue");
        const auto held = knob.hold (now += 300.0);
        const bool handed = knob.onBreakMusic() && held.contains ("BREAK MUSIC");
        const auto nothing = knob.turn (1, now += 300.0);
        const auto unchosen = knob.push (now += 300.0);
        check (rests && handed && nothing.contains ("No break music") && ! music.isPlaying() && unchosen.contains ("Choose a mix"),
               "knob: it rests on PA/venue, a hold hands it to the break music, and with no mix ever chosen a push starts nothing");

        music.play ("Chill", true);
        music.stop();
        b.run (1.0);
        knob.push (now += 300.0);
        const bool started = music.isPlaying() && music.mixName() == "Chill" && music.isUp();
        knob.turn (-3, now += 300.0);
        const bool turned = std::abs (music.levelDb() - -15.0f) < 0.01f;
        knob.push (now += 300.0);
        const bool down = ! music.isUp();
        knob.turn (-1, now += 300.0);
        const bool stillDown = ! music.isUp();
        knob.turn (2, now += 300.0);
        const bool upAgain = music.isUp() && std::abs (music.levelDb() - -14.0f) < 0.01f;
        check (started && turned && down && stillDown && upAgain,
               "knob: on the break music a push starts the last mix, a turn is one dB a click, a push fades it down, and turning it up brings it back");

        auto s = music.settings();
        s.onSwitch = true;
        music.setSettings (s);
        music.startedPlaying (BreakMusic::Started::footswitch);
        const bool back = ! knob.onBreakMusic() && knob.screen (now += 300.0).contains ("Venue") && ! music.isUp();
        const float level = music.levelDb();
        knob.turn (1, now += 300.0);
        check (back && music.levelDb() == level && ! music.isUp(), "knob: when Tom starts playing it goes back to PA/venue by itself, and a turn no longer touches the music");
        knob.hold (now += 300.0);
        const auto again = knob.hold (now += 300.0);
        check (! knob.onBreakMusic() && again.contains ("Venue"), "knob: a second hold hands it back to PA/venue");
    }

   #if JUCE_LINUX
    // ---- the phone's sound, arriving through the pipe as a receiver would send it ----
    {
        Bench b (48000.0);
        auto& music = b.engine.breakMusic();
        music.setFolder (folder);
        const auto pipe = temp.getChildFile ("phone.pipe");
        music.setPipe (pipe);
        auto s = music.settings();
        s.fadeUpSeconds = 0.5;
        music.setSettings (s);
        const auto error = music.play (BreakMusic::phoneName());
        b.run (0.05);       // the app makes the pipe and opens its end
        const int fd = ::open (pipe.getFullPathName().toRawUTF8(), O_WRONLY | O_NONBLOCK);
        std::vector<juce::int16> piece (882 * 2);
        juce::int64 sent = 0;
        bool wroteAll = fd >= 0;

        // Twenty milliseconds at a time, in step with the audio: four seconds of a 440 Hz note at 44.1 kHz.
        for (int chunk = 0; chunk < 200 && fd >= 0; ++chunk)
        {
            for (int i = 0; i < 882; ++i, ++sent)
                piece[(size_t) i * 2] = piece[(size_t) i * 2 + 1] = (juce::int16) (16384.0 * std::sin (twoPi * 440.0 * (double) sent / 44100.0));

            wroteAll = wroteAll && ::write (fd, piece.data(), piece.size() * 2) == (ssize_t) (piece.size() * 2);
            b.run (0.02);
        }

        const auto tail = b.last (4, 1.0);
        const double level = peak (tail), wanted = std::pow (10.0, -12.0 / 20.0) * 0.5;       // its loudest point: the speed is nudged to hold the queue, so the pitch is a hair off 440
        const auto report = music.reportText();
        check (error.isEmpty() && wroteAll && std::abs (level / wanted - 1.0) < 0.02 && strength (tail, 440.0, b.rate) > wanted * 0.9 && biggestStep (tail) < (float) (wanted * twoPi * 440.0 / b.rate * 1.5) && report.contains ("Times the music ran dry: 0"),
               "break music: the phone's sound, written into the pipe 20 ms at a time, plays cleanly at the right pitch and level (" + db (level) + ", wanted " + db (wanted) + ")");

        if (fd >= 0)
            ::close (fd);

        // The sender goes away: silence, no crash, and it is counted, not hidden.
        b.run (1.5);
        b.forget();
        b.run (0.5);
        check (peak (b.heard[4]) == 0.0f && music.isPlaying(), "break music: when the phone stops sending there is silence, and the app goes on waiting for it");
        music.stop();
        b.run (0.6);
    }
   #else
    {
        BreakMusic music;
        check (music.play (BreakMusic::phoneName()).isNotEmpty() && ! music.isPlaying(), "break music: on this computer the phone's sound is not offered, and asking for it says why");
    }
   #endif

    temp.deleteRecursively();
}

} // namespace gigrig
