#pragma once

#include "AudioEngine.h"
#include "Pedal.h"

#include "../firmware/shared/SwitchTile.h"

#include <array>
#include <atomic>
#include <functional>

namespace gigrig
{

/** The small screens on the board's footswitches, seen from the app (built 28 Nov 2026, before
    the board exists; the guide is docs/switch-screens.md).

    Each footswitch on the finished board has its own 1.54 inch colour screen, driven by the
    board's controller (a Teensy). The app knows what every switch is doing, so the app decides
    what each screen shows: it builds one "tile" per switch (a background colour, words, lane
    pictures, a pedal picture) and sends it to the controller, which only draws. The tile, its
    drawing and the messages are in firmware/shared/SwitchTile.*, shared with the controller's
    program, so the picture on the app's Switch screens tab is dot for dot what the Teensy draws.

    The looks are Tom's ideas of 28 Nov 2026 and are undecided until he has seen them on real
    screens in the sun, so every choice is a setting here, not something built into the Teensy.

    Off by default, and off must open no MIDI output and send nothing (a check). Everything here
    is on the message thread; nothing touches the audio thread or the looper.

    No screen in here (SwitchScreensPanel shows it). Checked by src/SwitchScreensSelfTest.cpp. */
class SwitchScreens final : private juce::Timer
{
public:
    /** What the tiles are built from: a plain copy of the rig's state, so the self-test can make one up. */
    struct Inputs
    {
        struct Track
        {
            TrackState state = TrackState::empty;
            bool hasAudio = false, undone = false;
            int rcInputs = 0;               // which sockets the track listens to (its lanes)
        };

        struct Effect
        {
            bool used = false;              // the effects have taken this switch from the looper
            int action = 0;                 // EffectsRack::PedalAssign::action
            int chain = 0;
            juce::String chainName;         // "Electric", "Vocal": gives the chain colour
            juce::String effectId, effectName;      // for a slot's on/off switch
            juce::String second;            // a second line: the harmony's key, the tempo
            int toggle = -1;                // 1 on, 0 off, -1 not an on/off action
        };

        std::array<Track, numLoopTracks> track {};
        int bank = 0, currentTrack = 0;
        std::array<SwitchFunction, numPedalSwitches> looperJob {};      // what the looper does with each switch in this bank
        std::array<Effect, numPedalSwitches> effect {};
        std::array<bool, numPedalSwitches> held {};
        double bpm = 0.0;
    };

    /** Reads the rig as it is now. Message thread. */
    static Inputs readRig (AudioEngine&, PedalInput&);

    struct Settings
    {
        bool send = false;                          // off: no MIDI output is opened, nothing is sent
        int offLook = tile::lookChainBorder;        // how an effect switch that is off looks
        int show = tile::showRig;                   // what the controller shows: the rig, the sample pages turning, or one sample page
        int page = 0;
        std::array<int, 3> bench { 0, 1, 2 };       // which switch each of the three bench screens shows
        bool operator== (const Settings&) const = default;
    };

    /** One switch's screen: the tile, and the same thing in words for the report. */
    struct Face
    {
        tile::Tile tile;
        juce::String words;
    };

    enum class UndoLamp { unused, undone, redone };

    SwitchScreens();
    ~SwitchScreens() override;

    /** Starts following the rig by itself, twenty times a second. The self-test calls update() instead. */
    void follow (AudioEngine&, PedalInput&);

    Settings settings() const                           { return now; }
    void setSettings (const Settings&);

    /** Builds every switch's tile from the rig's state and sends what has changed. Message thread. */
    void update (const Inputs&);

    const Face& face (int sw) const                     { return faces[(size_t) juce::jlimit (0, numPedalSwitches - 1, sw)]; }
    UndoLamp undoLamp() const noexcept                  { return lamp; }

    /** Which switch a screen on the board shows, or -1. The three bench screens are chosen on
        the tab; the rest follow the switch numbers until the board's own layout is decided. */
    int switchForScreen (int screen) const;

    /** Where messages go. Unset, they go to the controller's MIDI output (found by its name);
        the self-test sets it to catch them. */
    std::function<void (const juce::MidiMessage&)> sendMidi;
    /** False in the self-test: no real MIDI device is looked for or opened. */
    void setUseRealDevices (bool b) noexcept            { realDevices = b; }
    bool outputIsOpen() const noexcept                  { return output != nullptr; }

    /** Raw MIDI from any device, on whichever thread it arrives: the controller's answers. */
    void handleMidi (const juce::String& deviceName, const juce::MidiMessage&);
    /** True when the controller answered in the last few seconds. */
    bool controllerAnswering() const;
    tile::Status controllerStatus() const;

    int messagesSent() const noexcept                   { return sent; }

    /** One line for the tab. */
    juce::String statusLine() const;
    juce::String reportText() const;

    juce::String stateText() const;
    void loadStateText (const juce::String&);
    static juce::File stateFile();
    void saveState() const;
    static inline std::atomic<bool> memoryOnDisk { true };

    /** "Tube screamer style drive" -> "Tube screamer": an effect's name cut down for a small screen (15 letters at most). */
    static juce::String shortName (const juce::String& effectName);
    /** The chain colour for a chain's name ("Vocal" is red...), or grey for a name that says nothing. */
    static tile::Colour chainColour (const juce::String& chainName);
    static juce::String offLookName (int look);

    /** A tile as a picture, for the tab (and anything else that wants to look at one). */
    static juce::Image picture (const tile::Tile&, bool flashOn);

private:
    void timerCallback() override;
    Face faceFor (int sw, const Inputs&) const;
    Face effectFace (int sw, const Inputs&) const;
    void followUndo (const Inputs&);
    void send (const uint8_t* body, int length);
    void lookForController();

    Settings now;
    std::array<Face, numPedalSwitches> faces;
    UndoLamp lamp = UndoLamp::unused;
    std::array<bool, numLoopTracks> wasUndone {};

    AudioEngine* engine = nullptr;
    PedalInput* pedal = nullptr;
    bool realDevices = true;
    std::unique_ptr<juce::MidiOutput> output;
    juce::String outputName;
    juce::uint32 lastLook = 0, lastHello = 0;
    int ticks = 0, refreshNext = 0, sent = 0;
    juce::int64 bytesSent = 0;
    std::array<tile::Tile, tile::maxScreens> sentTiles;
    std::array<bool, tile::maxScreens> hasSent {};
    int sentShow = -1, sentPage = -1;

    mutable juce::CriticalSection statusLock;
    tile::Status status;
    juce::uint32 statusAt = 0;
    bool everAnswered = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwitchScreens)
};

} // namespace gigrig
