#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "MixRecorder.h"
#include "effects/Chain.h"
#include "effects/mixer/MixerBlocks.h"

#include <array>
#include <atomic>
#include <functional>
#include <vector>

namespace gigrig
{

/** The mixer stage inside the audio engine (built 26 Nov 2026, see docs/mixer.md and the
    design doc's "Signal flow" and "Mixer, presets and IEMs").

    Switched off (the default) it does nothing at all: effect chains and loops go to the
    returns exactly as before, and the CQ12T does the mixing. Switched on, the app is the mixer:

        socket, after its effects chain (the same point the looper hears)
          + every loop that was recorded from that socket          <- each loop returns to its own channel
          -> record point "after looper"
          -> channel gate, source enhancer, warmth, EQ, then compressor, 1176-style and LA-2A style compressors
          -> fader, pan, mute -> main mix
          -> sends: monitor 1, monitor 2, and the four mixer effects (reverbs and delays)
        mixer effects -> wet low cut and high cut -> main mix and monitors
        main mix / each monitor -> EQ, notches, compressor, bus compressor, tape, limiter, line delay, level -> a return (or a pair)

    So the mixer's reverbs and delays come after the looper and are never baked into a loop,
    and a monitor send carries its channel live and looped together with no extra rule.

    Four kinds of preset, loaded independently:
      act       every channel (what is plugged in, levels, EQ, compressor, sends)
      venue/PA  the main output's whole chain, and the mixer effects
      monitors  the two monitor outputs
      output    one output's processing, loadable into any output
    Changing songs never touches any of them.

    Nothing here adds delay (except the line delay, when it is set). */

constexpr int numMixChannels = 8;
constexpr int numMixFx = 4;
constexpr int numMixOutputs = 3;        // 0 = main, 1 = monitor 1 (Tom), 2 = monitor 2 (the duo partner)
constexpr int numMixMonitors = 2;
constexpr int numMixSockets = 16;
constexpr float mixOffDb = -90.0f;      // a fader or send at or below this is off

/** Where each block sits in a channel's processing (MixChannelSetup::strip). */
constexpr int stripGate = 0, stripEnhancer = 1, stripWarmth = 2, stripEq = 3, stripComp = 4, stripInsert = 5, stripOpto = 6, numStripBlocks = 7;
/** The first blocks (gate, source enhancer, warmth, EQ) are the channel's tone; from here on it is the compressors. A monitor can take its sends between the two. */
constexpr int stripSplit = 4;
/** Where each block sits in an output's processing (MixOutputSetup::chain). */
constexpr int outEq = 0, outNotches = 1, outComp = 2, outBus = 3, outTape = 4, outLimiter = 5, outDelay = 6, numOutBlocks = 7;

/** Where a monitor takes its sends from in each channel. */
enum class TapPoint { beforeProcessing, afterEq, afterCompressors };

/** What a channel records when Record is pressed (the Recording tab, docs/recording.md).
    afterLooper is the channel's live sound before its own gate, EQ and compressors, with its loops;
    afterProcessing is the same point after them (the sound its fader gets). */
enum class RecordPoint { none, raw, afterEffects, afterLooper, afterProcessing };
constexpr int numRecordPoints = 5;

/** The recorder's slots: one for each channel, then the main mix, the recording mix and the mixer's effects. */
constexpr int recSlotMain = numMixChannels, recSlotMix = numMixChannels + 1, recSlotFx = numMixChannels + 2;

struct MixChannelSetup
{
    juce::String name;
    bool on = false;
    int socket = -1;                    // 0-based input socket, as the looper hears it (after that socket's effects chain); -1 = none
    float trimDb = 0.0f;
    bool mute = false;
    float faderDb = 0.0f;               // into the main mix
    float pan = 0.0f;                   // -1 left .. 1 right
    std::array<float, numMixMonitors> monDb { mixOffDb, mixOffDb };
    std::array<float, numMixFx> fxDb { mixOffDb, mixOffDb, mixOffDb, mixOffDb };
    RecordPoint record = RecordPoint::afterLooper;
    int directReturn = -1;              // sound-crew direct out: this channel alone (live and looped, before its EQ) on a return; -1 = none
    bool link = false;                  // stereo: the next channel is this one's right side and follows every setting (this one pans left, that one right)
    fx::ChainModel strip;               // gate, enhancer, warmth, mixeq | mixcomp, the 1176-style compressor (comp), the LA-2A style one (la2a)
    // The recording mix (Tom, 28 Nov 2026): a second balance of the same channels, only ever written to a file.
    float recDb = 0.0f;                 // this much louder or quieter than in the main mix (it follows the fader and the mute); mixOffDb = not in it
    float recPan = 0.0f;                // its own pan, -1 left .. 1 right
};

struct MixFxSetup
{
    juce::String name;
    bool on = true;
    fx::ChainModel effect;              // one reverb or delay from the effects list
    float lowCutHz = 20.0f, highCutHz = 20000.0f;   // on the wet sound only
    float returnDb = 0.0f;              // into the main mix
    std::array<float, numMixMonitors> monDb { mixOffDb, mixOffDb };
    fx::ChainModel eq;                  // one mixeq, on what goes INTO the effect (Tom, 27 Nov 2026)
    float recDb = 0.0f;                 // in the recording mix: this much louder or quieter than its return into the main mix
};

struct MixOutputSetup
{
    juce::String name;
    float levelDb = 0.0f;
    bool mute = false;
    bool mono = true;                   // left and right summed and sent as one signal
    bool preFader = true;               // monitors: sends are taken before the channel faders and mutes
    TapPoint tap = TapPoint::afterCompressors;   // monitors: where in each channel the sends are taken
    int firstReturn = -1;               // 0-based return, -1 = not connected
    bool pair = true;                   // also on the next return (the right side, or the same mono signal again)
    fx::ChainModel chain;               // mixeq, notches, mixcomp, buscomp, tape, limiter, linedelay
};

/** What the Recording tab sets apart from each channel's own record point: which mixes are
    written, and the recording mix's own level, EQ and limiter. */
struct MixRecordSetup
{
    bool mainMix = true;                // "Main mix": a stereo file of the main output, as the PA gets it
    bool mix = true;                    // "Recording mix": a stereo file of the recording mix
    bool fxTracks = false;              // each of the mixer's reverbs and delays (its wet sound alone) as a file of its own
    float levelDb = 0.0f;               // the recording mix's level
    fx::ChainModel chain;               // mixeq, buscomp, limiter
};

constexpr int recEq = 0, recBus = 1, recLimiter = 2, numRecBlocks = 3;

struct MixerSetup
{
    MixRecordSetup rec;
    std::array<MixChannelSetup, numMixChannels> channel;
    std::array<MixFxSetup, numMixFx> fx;
    std::array<MixOutputSetup, numMixOutputs> output;
};

/** The mixer as it starts: Tom's sources in the design doc's order, three ready effects with
    every send off, everything flat. */
MixerSetup defaultMixerSetup();
fx::ChainModel defaultStripModel();
fx::ChainModel defaultOutputModel();
fx::ChainModel defaultRecordModel();
/** "after the looper" and so on: a record point as a file name and the screens show it. */
juce::String recordPointName (RecordPoint);
/** The effects a mixer effect slot may hold (ids from the effects list). */
const std::vector<std::string>& mixerFxChoices();
juce::String mixOutputName (int output);

//==============================================================================
class Mixer
{
public:
    Mixer();
    ~Mixer();

    //==============================================================================
    // Message thread.

    void setOn (bool shouldBeOn) noexcept               { wanted.store (shouldBeOn); markChanged(); }
    bool isOn() const noexcept                          { return wanted.load(); }

    const MixerSetup& setup() const noexcept            { return now; }
    void setChannel (int c, const MixChannelSetup&);
    void setFx (int f, const MixFxSetup&);
    void setOutput (int o, const MixOutputSetup&);
    void setAll (const MixerSetup&);
    /** The Recording tab's own settings. They are saved with the act. */
    void setRecord (const MixRecordSetup&);

    // ---- a song's own sends ----
    // A song may set how much of a channel goes to a reverb or a delay (its file's "send" lines).
    // These sit on top of the act: they are not saved in it, they go when another song loads,
    // and they never touch the venue (the reverbs and delays themselves, the main output).
    struct SongSend { int channel = 0, fx = 0; float db = mixOffDb; };
    void setSongSends (const std::vector<SongSend>&);
    std::vector<SongSend> songSends() const;
    bool hasSongSend (int c, int f) const noexcept      { return songHas[(size_t) c][(size_t) f]; }
    void setSongSend (int c, int f, float db);
    void clearSongSend (int c, int f);
    // ---- a song's own reverb and delay settings (Tom's decision, 27 Nov 2026) ----
    // A song may also say which reverb or delay one of the mixer's four effects is, and how it
    // is set (its file's "mixer effect" lines). Like the sends this sits on top: the venue's own
    // effect comes back when another song loads. The EQ, the cuts and the levels stay the venue's.
    struct SongFx { int fx = 0; fx::ChainModel effect; };
    void setSongFx (const std::vector<SongFx>&);
    std::vector<SongFx> songFx() const;
    bool hasSongFx (int f) const noexcept               { return f >= 0 && f < numMixFx && songFxHas[(size_t) f]; }
    void setSongFx (int f, const fx::ChainModel&);
    void clearSongFx (int f);
    /** The reverb or delay an effect is right now: the song's where it has one, else the venue's. */
    const fx::ChainModel& effectNow (int f) const noexcept  { return songFxHas[(size_t) f] ? songFxModel[(size_t) f] : now.fx[(size_t) f].effect; }

    /** What a channel sends to an effect right now: the song's value where it has one, else the act's. */
    float sendDb (int c, int f) const noexcept;

    // ---- presets: small text files anyone can read ----
    enum class Preset { act, venue, monitors, output };
    static juce::File presetRoot();
    static juce::File presetFolder (Preset kind, const juce::File& root = presetRoot());
    juce::StringArray presetNames (Preset kind) const;
    /** Uses another folder for presets (the self-test does, so it never touches Tom's files). */
    void setPresetRoot (const juce::File& folder)       { root = folder; }
    /** Writes the starter presets, each only if no file of that name is there. Returns how many it wrote. */
    int writeStarterPresets();

    juce::String actText() const;
    juce::String venueText() const;
    juce::String monitorsText() const;
    juce::String outputText (int output) const;
    /** Each returns an error, or an empty string. */
    juce::String saveAct (const juce::String& name);
    juce::String loadAct (const juce::String& name);
    juce::String saveVenue (const juce::String& name);
    juce::String saveMonitors (const juce::String& name);
    juce::String loadMonitors (const juce::String& name);
    juce::String saveOutput (int output, const juce::String& name);
    juce::String loadOutput (int output, const juce::String& name);

    /** Loads a venue (PA) preset the safe way: the new main chain fades in beside the old one
        over about a second, and the level never jumps up (a preset saved louder keeps the
        level as it is and says by how much it differs). Returns a line for the screen. */
    juce::String loadVenue (const juce::String& name);
    static constexpr double venueFadeSeconds = 1.0;

    juce::String actName() const                        { return act; }
    juce::String venueName() const                      { return venue; }
    juce::String monitorsName() const                   { return monitors; }
    bool actChanged() const noexcept                    { return actDirty; }
    bool venueChanged() const noexcept                  { return venueDirty; }
    bool monitorsChanged() const noexcept               { return monitorsDirty; }

    // ---- the venue encoder (ENC 5 on the board; three buttons on the Mixer tab for now) ----
    // Turning only browses: nothing changes until a push, and browsing gives up after 5 seconds.
    // A push loads the name shown. If tonight's changes to the venue in use are not saved, the
    // first push only warns and a second push (within 5 seconds) loads.
    // 'nowMs' is a running clock (Time::getMillisecondCounterHiRes); the self-test passes its own.
    juce::String turnVenue (int steps, double nowMs);
    juce::String pushVenue (double nowMs);
    /** What the main screen shows: the venue in use, or the one being browsed. */
    juce::String venueScreen (double nowMs) const;
    static constexpr double browseTimeoutMs = 5000.0;

    // ---- recording: each channel at its record point, and the main mix ----
    static juce::File recordingFolder();
    /** Starts a new folder of WAV files. Returns an error, or an empty string. */
    juce::String startRecording (const juce::File& parentFolder);
    void stopRecording();
    /** The files a recording started now would hold, in order (what the Recording tab lists). */
    struct RecordTrack { int slot = 0; juce::String name; int channels = 1; };
    std::vector<RecordTrack> recordTracks() const;
    /** Room left on the recordings drive, and how long that lasts with the tracks chosen now. Message thread. */
    struct RecordRoom { double freeGb = 0.0, gbPerHour = 0.0, hours = 0.0; };
    RecordRoom recordRoom (const juce::File& parentFolder) const;
    /** A recording stops by itself when the drive has less than this left. */
    static constexpr double recordStopGb = 1.0;
    /** Why the last recording stopped by itself, or empty. */
    juce::String recordNote() const                     { return recNote; }
    float takeRecordPeak (int side) noexcept            { return recOut.peak[(size_t) side].exchange (0.0f); }
    /** The Recording tab's part of the report. */
    juce::String recordingReportText() const;
    MixRecorder& recorder() noexcept                    { return rec; }

    // ---- meters and figures ----
    float takeChannelPeak (int c) noexcept              { return chans[(size_t) c].peak.exchange (0.0f); }
    float takeOutputPeak (int o, int side) noexcept     { return outs[(size_t) o].peak[(size_t) side].exchange (0.0f); }
    float takeFxPeak (int f) noexcept                   { return fxs[(size_t) f].peak.exchange (0.0f); }
    /** How far a channel's compressor (or an output's compressor or limiter) is turning down now, in dB (0 = not at all). */
    float channelCompDb (int c) const;
    float outputCompDb (int o) const;
    float outputLimiterDb (int o) const;
    /** Loudest the loops with no channel of their own got since the last call. */
    float takeStrayPeak() noexcept                      { return strayPeak.exchange (0.0f); }
    bool guardTripped() const noexcept                  { return guard.load(); }
    void clearGuard() noexcept                          { guard.store (false); }
    struct Load { double average = 0.0, busiest = 0.0; };
    Load load() const noexcept                          { return { (double) loadAverage.load(), (double) loadBusiest.load() }; }

    /** Frees old effect sets and saves the mixer's state if it changed. Call about 10 times a second. */
    void tick();

    /** The whole mixer as text, for the app's own memory (it comes back as it was left). */
    juce::String stateText() const;
    void loadStateText (const juce::String& text);
    static juce::File stateFile();
    void saveState();

    /** False while the self-test runs, so its pretend rigs never write over the real memory file
        (found 27 Nov 2026: a self-test run on a real machine left the test's settings behind). */
    static inline std::atomic<bool> memoryOnDisk { true };
    void markChanged() noexcept                         { dirty = true; }

    /** The mixer's part of the report. */
    juce::String reportText() const;
    /** Lines the report and the screen both show: anything that looks wrong in the set-up. */
    juce::StringArray warnings (const std::function<juce::String (int socket)>& socketNote = nullptr) const;

    //==============================================================================
    // Audio thread.

    void prepare (double sampleRate, int maxBlock);

    /** Call first in every block. True if the mixer is mixing this block (it is on, or fading out). */
    bool startBlock (int numSamples) noexcept;

    /** Where the looper adds each loop: one bus per socket (null where no channel listens to that socket). */
    float* const* laneBuses() noexcept                  { return busPtrs.data(); }
    float* strayBus() noexcept                          { return stray.data(); }

    /** raw = the sockets themselves; feed = the sockets as the looper hears them (after the effects). */
    /** Call after startBlock and before the looper. Runs every channel's own processing on its
        live sound and returns what the looper should hear: for a socket a channel listens to, that
        channel's sound after its gate, EQ and compressors; for any other socket, feed as it is.
        So a loop is processed once, as it was played, and layers never pile into a compressor. */
    const float* const* stripChannels (const float* const* feed, int numIns, int numSamples) noexcept;

    void process (const float* const* raw, const float* const* feed, int numIns, int numSamples) noexcept;

    /** Call in a block the mixer is not mixing (it is off): a recording carries on with what it
        can still hear, each channel's socket plain or after its effects, and silence in the mixes,
        so every file stays the same length. Not recording, it reads one flag and returns. It
        changes no sample anywhere. */
    void recordWithoutMixing (const float* const* raw, const float* const* feed, int numIns, int numSamples) noexcept;

    /** Adds the mixer's outputs to the returns. outputFor(r) gives return r's buffer or null. */
    template <typename OutputFor>
    void addOutputs (OutputFor&& outputFor, int numSamples) noexcept
    {
        for (int o = 0; o < numMixOutputs; ++o)
        {
            auto& out = outs[(size_t) o];
            const int first = out.firstReturn.load (std::memory_order_relaxed);

            if (first < 0)
                continue;

            if (auto* left = outputFor (first))
                juce::FloatVectorOperations::add (left, out.buf[0].data(), numSamples);

            if (out.pair.load (std::memory_order_relaxed))
                if (auto* right = outputFor (first + 1))
                    juce::FloatVectorOperations::add (right, out.buf[1].data(), numSamples);
        }

        for (int r = 0; r < 8; ++r)
            if (directUsed[(size_t) r])
                if (auto* to = outputFor (r))
                    juce::FloatVectorOperations::add (to, directData.data() + (size_t) r * (size_t) capacity, numSamples);
    }

    void setTempo (double bpm) noexcept;

    /** The break music for this block (BreakMusic.h), or nulls when there is none. Call before
        process(). It joins the main output only, after the main mix has been recorded, so it is
        never in a recording, a loop or a monitor. It follows the main output's level and mute. */
    void setBreakFeed (const float* left, const float* right) noexcept  { breakL = left; breakR = right; }
    /** Audio thread, before stripChannels() and process(): what the tuner wants silent in this block.
        `mainMix`: the whole main mix fades out over a few milliseconds; the monitors, the recordings
        and the break music are not touched. `guitars`: the live sound of the channels named by
        setTunerChannels fades out of the main mix and of the sends to the reverbs and delays,
        while loops already playing, the other channels and reverb tails carry on. With
        `monitorsKeep` the monitors still carry the guitar (Tom, 19 Nov 2026: the in-ears stay
        live so he can hear it); without it the guitar fades out where it enters the channel, as
        a tuner pedal in front of everything would do it, so it is in no monitor and no new loop either. */
    void setTuning (bool wholeMain, bool guitars, bool monitorsKeep) noexcept
    {
        tuningNow = wholeMain;
        tuningGuitars = guitars && ! monitorsKeep;
        tuningGuitarsMain = guitars && monitorsKeep;
    }
    /** Message thread: the channels that go silent under `guitars`, one bit each. */
    void setTunerChannels (juce::uint32 mask) noexcept                  { tunerChannels.store (mask); }
    juce::uint32 tunerChannelMask() const noexcept                      { return tunerChannels.load(); }

    /** For the self-test: what an output carried in the last block. */
    const float* outputBuffer (int o, int side) const noexcept  { return outs[(size_t) o].buf[(size_t) side].data(); }

private:
    /** A gain that moves towards its target at a steady pace, so nothing clicks. */
    struct Glide
    {
        float now = 0.0f;
        /** Works out this block's start value and per-sample step, and moves on. */
        void advance (float target, float mostPerSample, int n, float& start, float& step) noexcept
        {
            start = now;
            const float gap = target - now, most = mostPerSample * (float) n;
            const float end = std::abs (gap) <= most ? target : now + (gap > 0.0f ? most : -most);
            step = (end - now) / (float) n;
            now = end;
        }
    };

    struct Chan
    {
        fx::Chain strip, strip2;        // the tone blocks, then the compressors (stripSplit)
        std::atomic<bool> on { false }, mute { false };
        std::atomic<int> socket { -1 }, record { 0 }, direct { -1 };
        std::atomic<float> trim { 1.0f }, fader { 1.0f }, pan { 0.0f };
        std::array<std::atomic<float>, numMixMonitors> mon {};
        std::array<std::atomic<float>, numMixFx> fx {};
        std::atomic<float> peak { 0.0f };
        std::atomic<float> recGain { 1.0f }, recPan { 0.0f };

        // audio thread
        Glide trimG, mainL, mainR, directG, recL, recR;
        std::array<Glide, numMixMonitors> monL, monR;
        std::array<Glide, numMixFx> fxG;
        bool wasOn = false;
        bool tunedOut = false;          // silent for the tuner, or on the way back from it
        float tuneMain = 1.0f;          // how much of the live sound the main mix gets: 0 while tuning with the monitors kept
    };

    struct Fx
    {
        fx::Chain chain, pre;           // the effect, and the EQ on what goes into it
        fx::WetFilter filter;
        std::atomic<bool> on { true };
        std::atomic<float> ret { 1.0f };
        std::array<std::atomic<float>, numMixMonitors> mon {};
        std::atomic<float> peak { 0.0f };

        std::atomic<float> recGain { 1.0f };

        Glide retG, recG;
        std::array<Glide, numMixMonitors> monG;
        std::vector<float> in, wet;
        bool live = false;
    };

    struct Out
    {
        // [set][side]. Only the main output uses the second set: a venue load builds the new
        // chain there and the audio thread fades across.
        std::array<std::array<fx::Chain, 2>, 2> chain;
        std::atomic<float> level { 1.0f };
        std::atomic<bool> mute { false }, mono { true }, preFader { true }, pair { true };
        std::atomic<int> firstReturn { -1 }, tap { 2 };
        std::array<std::atomic<float>, 2> peak {};

        Glide levelG;
        std::array<std::vector<float>, 2> bus, buf, other;
    };

    /** The recording mix: a stereo bus that is only worked on while it is being recorded. */
    struct RecOut
    {
        std::array<fx::Chain, 2> chain;
        std::atomic<float> level { 1.0f };
        std::array<std::atomic<float>, 2> peak {};
        Glide levelG;
        std::array<std::vector<float>, 2> bus;
    };

    void pushRecord (bool first = false);
    void pushChannel (int c, bool first = false);
    void pushFx (int f, bool first = false);
    void pushOutput (int o, bool first = false);
    void rebuildBuses() noexcept;
    void followLinks();
    juce::String applyVenue (const MixerSetup& from, const juce::String& name);
    juce::StringArray venueList() const;

    MixerSetup now;                                     // message thread's copy
    std::array<std::array<float, numMixFx>, numMixChannels> songDb {};
    std::array<std::array<bool, numMixFx>, numMixChannels> songHas {};
    std::array<bool, numMixFx> songFxHas {};
    std::array<fx::ChainModel, numMixFx> songFxModel;
    juce::String act, venue, monitors;
    bool actDirty = false, venueDirty = false, monitorsDirty = false;
    bool dirty = false;
    int ticksSinceChange = 0;
    juce::File root;

    // the venue encoder
    int browseIndex = -1;
    double browseAtMs = -1.0e12, armedAtMs = -1.0e12;
    juce::String armedFor, lastVenueNote;

    std::array<Chan, numMixChannels> chans;
    std::array<Fx, numMixFx> fxs;
    std::array<Out, numMixOutputs> outs;

    std::atomic<bool> wanted { false };
    std::atomic<int> mainSet { 0 };                     // which of the main output's two chain sets is in use
    std::atomic<bool> slowLevel { false };              // the main level is fading to a venue's level
    std::atomic<bool> guard { false };
    std::atomic<float> strayPeak { 0.0f };
    std::atomic<float> loadAverage { 0.0f }, loadBusiest { 0.0f };
    std::atomic<int> socketMask { 0 };                  // sockets a channel that is on listens to

    // audio thread
    double rate = 48000.0;
    int capacity = 0;
    float master = 0.0f;                                // 0..1: the fade when the mixer is switched on or off
    float mainMix = 0.0f;                               // 0 = set 0 .. 1 = set 1
    float fastStep = 0.0f, slowStep = 0.0f, masterStep = 0.0f;
    std::vector<float> laneData, stray, silence, directData;
    std::vector<float> chanPre, chanMid, chanPost;      // each channel's live sound before, between and after its two strips
    std::array<const float*, 64> loopFeed {};
    std::array<bool, numMixChannels> stripped {};
    std::array<bool, 8> directUsed {};
    std::array<float*, numMixSockets> busPtrs {};
    float guardWindowPeak = 0.0f;
    int guardWindowLeft = 0, guardHotWindows = 0;
    float loadPeakHold = 0.0f;
    int loadHoldLeft = 0;
    const float* breakL = nullptr;
    const float* breakR = nullptr;
    bool tuningNow = false, tuningGuitars = false, tuningGuitarsMain = false;
    std::vector<float> tuneFeed;        // a guitar channel as the main mix hears it while tuning: its loops without its live sound
    std::atomic<juce::uint32> tunerChannels { 0 };
    float tuneGain = 1.0f;              // 1 = heard, 0 = silent for the tuner

    MixRecorder rec;
    RecOut recOut;
    std::vector<float> recScratch;
    bool recMixWas = false;
    juce::String recNote;
    juce::File recParent;
    int recTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Mixer)
};

} // namespace gigrig
