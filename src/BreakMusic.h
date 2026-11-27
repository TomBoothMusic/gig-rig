#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace gigrig
{

class Mixer;

/** Break music: the music between sets, played by the rig itself (built 27 Nov 2026; the
    guide is docs/break-music.md, the design doc section is "Intermission / break music").

    It is a stereo channel of its own that uses none of the interface's inputs. With the mixer
    on it joins the main output only, after the main mix has been recorded, so it is never in
    a loop, a recording or a monitor. With the mixer off it goes straight to a pair of returns.

    What it plays: one of Tom's own mixes (any audio file in Documents/GigRig/Break music),
    round and round, or on the rig computer the phone's sound from a Bluetooth or Wi-Fi
    receiver (see "the phone" below).

    The two rules Tom decided (27 Nov 2026):
      - it fades down by itself when he starts playing;
      - it only ever comes back up by hand.
    While it is down it keeps playing silently, so it is somewhere new at the next break.

    Nothing plays until somebody presses Play: an app that has just started is silent.

    Threads: a background thread reads the file (or the phone's sound), brings it to the
    interface's sample rate and fills a queue. The audio thread only empties the queue and
    fades; it never touches a file, a lock or the heap. */
class BreakMusic final : private juce::Thread
{
public:
    BreakMusic();
    ~BreakMusic() override;

    //==============================================================================
    // Message thread.

    static juce::File defaultFolder();
    /** Uses another folder (the self-test does, so it never touches Tom's files). */
    void setFolder (const juce::File& f)                { root = f; }
    juce::File folder() const                           { return root; }
    /** The mixes in the folder, by name (the file name without its ending), in A to Z order. */
    juce::StringArray mixes() const;

    /** The name the phone's sound goes by in the list of things to play. */
    static juce::String phoneName()                     { return "Phone (Bluetooth or Wi-Fi)"; }
    /** True where the phone's sound can be played at all (the Linux rig computer). */
    static bool phoneSupported() noexcept;
    /** The pipe a receiver writes the phone's sound into: 16-bit stereo at 44.1 kHz, nothing else. */
    static juce::File defaultPipe();
    void setPipe (const juce::File& f)                  { pipe = f; }
    juce::File pipeFile() const                         { return pipe; }

    /** Starts a mix where it was left last time (or at its start), fading in. The phone's name
        plays the phone. Returns an error, or an empty string. */
    juce::String play (const juce::String& name, bool fromTheStart = false);
    /** Fades out and stops. The place in the mix is remembered. */
    void stop();
    bool isPlaying() const noexcept                     { return playing.load(); }
    bool playingPhone() const noexcept                  { return phone.load(); }
    /** The mix that is playing, or the one that played last. */
    juce::String mixName() const                        { return current; }
    double positionSeconds() const noexcept;
    double lengthSeconds() const noexcept               { return lengthSecs.load(); }

    static constexpr float minLevelDb = -60.0f, maxLevelDb = 6.0f;
    void setLevelDb (float db);
    float levelDb() const noexcept                      { return level; }

    /** By hand: the music comes back up to the level set, slowly. */
    void bringUp();
    /** By hand: the music fades down (it keeps playing, silently). */
    void fadeDown();
    /** Where the fade is heading: up (heard) or down (silent). */
    bool isUp() const noexcept                          { return up.load(); }
    /** Where the fade is right now, 0 (silent) to 1 (at the level set). */
    float fadeNow() const noexcept                      { return fadeShown.load(); }
    float takePeak() noexcept                           { return peak.exchange (0.0f); }

    struct Settings
    {
        double fadeDownSeconds = 2.0, fadeUpSeconds = 4.0;
        bool onSwitch = true;       // a footswitch press counts as "started playing"
        bool onSong = false;        // a song loading counts
        bool onSound = false;       // sound on an input socket counts
        float soundDb = -30.0f;     // how loud that sound has to be
        int returnPair = 2;         // mixer off: 0-based pair of returns it plays on (2 = returns 5 and 6, the CQ's USB stereo channel)
        bool operator== (const Settings&) const = default;
    };
    Settings settings() const                           { return set; }
    void setSettings (const Settings&);

    /** "He has started playing": fades the music down if that kind of start is ticked in the
        settings. Any thread, the audio thread included. */
    enum class Started { footswitch, song, sound };
    void startedPlaying (Started) noexcept;
    /** How many times playing has faded the music down since the app started. */
    int fadedByPlaying() const noexcept                 { return byPlaying.load(); }

    /** Saves the state if it changed, and notes the place in the mix. Call about 10 times a second. */
    void tick();

    juce::String stateText() const;
    void loadStateText (const juce::String& text);
    static juce::File stateFile();
    void saveState();
    /** False while the self-test runs, so it never writes over the real memory file. */
    static inline std::atomic<bool> memoryOnDisk { true };

    /** One line for the screen: "Playing 'Chill': 12:40 of 58:03, up, level -12 dB". */
    juce::String statusLine() const;
    /** The break music's part of the report. */
    juce::String reportText() const;

    //==============================================================================
    // Audio thread.

    void prepare (double sampleRate, int maxBlock);

    /** Fills left() and right() with this block of music, faded and at its level. False when
        there is nothing to hear (the buffers are then not valid). */
    bool render (int numSamples) noexcept;
    const float* left() const noexcept                  { return outL.data(); }
    const float* right() const noexcept                 { return outR.data(); }
    int returnPair() const noexcept                     { return pairNow.load (std::memory_order_relaxed); }

    /** Listens to the input sockets for the "sound on an input" kind of start. */
    void hear (const float* const* inputs, int numIns, int numSamples) noexcept;

    /** For the self-test: fills the queue now instead of waiting for the background thread. */
    void fillNow();

    /** Brings sound from one sample rate to another as it arrives, a few samples at a time.
        Four-point (Catmull-Rom) interpolation. Public so the self-test can check it. */
    struct Resampler
    {
        void reset() noexcept;
        /** step = input samples per output sample. Returns how many output frames were written. */
        int push (const float* inL, const float* inR, int numIn, double step, float* outL, float* outR, int maxOut) noexcept;
        static int mostOut (int numIn, double step) noexcept    { return (int) ((double) numIn / juce::jmax (0.01, step)) + 4; }

    private:
        std::array<std::array<float, 4>, 2> hist {};
        double phase = 0.0;
    };

private:
    void run() override;
    bool pump();                    // reader thread: one step of filling the queue; true if it did something
    bool pumpFile();
    bool pumpPhone();
    void queue (const float* l, const float* r, int n) noexcept;
    void rememberPlace();
    juce::File fileFor (const juce::String& name) const;

    juce::File root, pipe;
    juce::String current;                               // message thread
    float level = -12.0f;
    Settings set;
    std::map<juce::String, double> places;              // where each mix was left, in seconds
    bool dirty = false;
    int ticksSinceChange = 0, ticksSincePlace = 0;
    juce::String lastError, lastDownNote;

    // ---- the source: swapped by the message thread, used by the reader thread, both under sourceLock ----
    juce::CriticalSection sourceLock;
    juce::AudioFormatManager formats;
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::int64 readPos = 0;
    Resampler resampler;
    juce::AudioBuffer<float> readBuf;
    std::vector<float> resL, resR;
    int pipeFd = -1;
    std::vector<char> pipeBytes;
    size_t pipeHeld = 0;                                // bytes of a part frame kept for the next read
    double fillAverage = 0.0;
    double deviceRate = 48000.0;

    // ---- the queue between the reader thread and the audio thread ----
    juce::AbstractFifo fifo { 16 };
    std::vector<float> fifoL, fifoR;
    std::atomic<int> flush { 0 };

    std::atomic<bool> playing { false }, phone { false }, up { false };
    std::atomic<float> levelGain { 0.25f };
    std::atomic<float> fadeShown { 0.0f }, peak { 0.0f };
    std::atomic<double> downStep { 0.0 }, upStep { 0.0 };
    std::atomic<bool> hearOn { false };
    std::atomic<float> hearGain { 0.03f };
    std::atomic<int> pairNow { 2 };
    std::atomic<int> byPlaying { 0 }, lastStart { -1 };
    std::atomic<double> sourceRate { 44100.0 }, lengthSecs { 0.0 };
    std::atomic<juce::int64> shownPos { 0 };
    std::atomic<int> underruns { 0 };
    std::atomic<juce::int64> phoneFrames { 0 };

    // ---- audio thread ----
    std::vector<float> outL, outR;
    double rate = 48000.0;
    int capacity = 0;
    int flushSeen = 0;
    double fade = 0.0;              // 0..1
    float gainNow = 0.0f;
    bool primed = false;
    int hotSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakMusic)
};

//==============================================================================
/** The fifth knob on the board, ENC 5 (design doc, "Intermission / break music", Fades).

    It rests on PA/venue: turning browses venue presets and a push loads one, exactly as
    Mixer::turnVenue and pushVenue do. A hold hands the knob to the break music: turning is its
    level (one dB a click; turning it up also brings faded-down music back), a push fades it up
    or down, and a push while nothing plays starts the mix that played last. Another hold, or
    Tom starting to play, hands the knob back to PA/venue.

    Until the board exists, buttons on the Mixer tab and the remote screen stand in for it. */
class VenueKnob
{
public:
    VenueKnob (Mixer& m, BreakMusic& b) : mixer (m), music (b) {}

    /** True while the knob works the break music. */
    bool onBreakMusic();
    juce::String turn (int steps, double nowMs);
    juce::String push (double nowMs);
    juce::String hold (double nowMs);
    /** What the main screen shows. */
    juce::String screen (double nowMs);

private:
    Mixer& mixer;
    BreakMusic& music;
    bool breakJob = false;
    int fadesSeen = 0;
};

} // namespace gigrig
