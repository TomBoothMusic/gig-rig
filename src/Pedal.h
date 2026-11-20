#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <functional>

namespace gigrig
{

/** The RC-600 as a foot controller. In its MIDI CTRL memory (slot 65) the nine
    switches send CC 20 to 28 on channel 1 and the external footswitches CTL3
    and CTL4 send CC 29 and 30: value 127 on press, 0 on release. */
constexpr int numPedalSwitches = 11;    // 0 to 8 = switches 1 to 9, 9 = CTL3, 10 = CTL4
constexpr int firstPedalCC = 20;
constexpr int pedalChannel = 1;

/** "Switch 3", "CTL4". */
juce::String pedalSwitchName (int sw);

/** What the switch is for in the MIDI CTRL memory: "track", "bank", "undo / redo"... */
juce::String pedalSwitchRole (int sw);

/** The switch a CC number belongs to, or -1. */
int pedalSwitchForCC (int channel, int controller) noexcept;

/** One thing a foot did. A press is reported the instant the switch goes down,
    because a looper cannot wait to see what the foot does next. */
struct PedalEvent
{
    enum class Kind { down, up, hold };

    Kind kind = Kind::down;
    int sw = 0;
    bool isDouble = false;      // down only: the second press of a double-press
    double seconds = 0.0;       // when it happened, on the decoder's clock
};

/** Turns presses and releases into press, hold and double-press. Pure logic
    with the time passed in, so the automatic builds can check it. */
class GestureDecoder
{
public:
    /** The RC-600 manual gives a hold as "two seconds or longer". */
    static constexpr double holdSeconds = 2.0;

    void setDoubleWindow (double seconds) noexcept      { doubleWindow = seconds; }
    double getDoubleWindow() const noexcept             { return doubleWindow; }

    /** Each returns true and fills `event` if something should be reported.
        A press of a switch that is already down (the same message arriving
        on two MIDI ports) is ignored. */
    bool press (int sw, double now, PedalEvent& event) noexcept;
    bool release (int sw, double now, PedalEvent& event) noexcept;

    /** Call often. Reports each switch that has now been held for two seconds, once. */
    int pollHolds (double now, PedalEvent* events, int maxEvents) noexcept;

    bool isDown (int sw) const noexcept                 { return state[(size_t) sw].down; }

private:
    struct SwitchState
    {
        bool down = false, holdSent = false, lastWasDouble = false;
        double downAt = -1.0e9;
    };

    std::array<SwitchState, numPedalSwitches> state {};
    double doubleWindow = 0.4;
};

/** What has been seen from each switch since the last reset: the stage 2 checklist. */
struct PedalSeen
{
    bool down = false;          // held right now
    int presses = 0, releases = 0, holds = 0, doubles = 0;
};

/** Receives the RC-600's MIDI (and clicks on the on-screen switches), decodes
    the gestures and hands them on. Safe to call from any thread except the audio thread. */
class PedalInput final : private juce::HighResolutionTimer
{
public:
    PedalInput();
    ~PedalInput() override;

    /** Called for every gesture, on whichever thread it arrived on. Set once, before use. */
    std::function<void (const PedalEvent&)> onEvent;

    /** Raw MIDI from a device. Only an RC-600's switch messages and clock are used. */
    void handleMidi (const juce::String& deviceName, const juce::MidiMessage& message);

    /** A click on an on-screen switch: the same as a foot on the real one. */
    void screenSwitch (int sw, bool isDown);

    void setDoubleWindowMs (int ms);
    int doubleWindowMs() const;

    /** Tell it which MIDI inputs are open, so it can say whether the RC-600 is connected. */
    void setOpenDevices (const juce::StringArray& names);
    bool rc600Connected() const;

    std::array<PedalSeen, numPedalSwitches> seen() const;
    void resetSeen();

    /** Counts since the last resetCounts(): for the glitch check with the RC-600 on USB. */
    void resetCounts();

    /** Gesture lines that arrived since the last call, oldest first. */
    juce::StringArray takeLines();

    /** One gesture in words: "Switch 3 (track): double-press". */
    static juce::String describe (const PedalEvent& event, double heldSeconds);

    /** The stage 2 part of the report. */
    juce::String reportText() const;

private:
    void hiResTimerCallback() override;
    void dispatch (const PedalEvent& event);
    double now() const;

    mutable juce::CriticalSection lock;
    GestureDecoder decoder;
    std::array<PedalSeen, numPedalSwitches> seenSwitches {};
    std::array<double, numPedalSwitches> downAt {};
    juce::StringArray pending;
    bool connected = false;
    int switchMessages = 0, otherChannelMessages = 0;
    juce::int64 clockTicks = 0;
    double lastClockAt = -1.0e9;
    juce::Time countsSince;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalInput)
};

} // namespace gigrig
