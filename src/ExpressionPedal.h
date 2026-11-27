#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <functional>

namespace gigrig
{

/** A real expression pedal, arriving as MIDI (built 27 Nov 2026, before Tom owns one, so it
    works the day he plugs one in; docs/songs.md, "The expression pedal").

    Any pedal that sends a MIDI controller will do: one plugged into the RC-600's EXP jack and
    given a CC number there, a USB one, or later the board's own Teensy. Nothing is assumed
    about the number: Learn watches every controller while the pedal is rocked from heel to
    toe, picks the one that swept, and keeps the lowest and highest values it saw as the heel
    and the toe. A footswitch (which only ever sends two values) is never taken for a pedal.

    Each move is handed on as a position, 0 (heel) to 100 (toe), on the message thread;
    MainComponent passes it to EffectsRack::moveExpressionPedal, the one place the blend, the
    wah and the whammy already take the pedal from. Moves that arrive faster than the message
    thread can take them are merged: only the newest position is delivered.

    No screen in here; the Pedal tab shows it. Checked by src/ExpressionSelfTest.cpp. */
class ExpressionInput
{
public:
    struct Setup
    {
        int cc = -1;                // -1 = no pedal set up
        int channel = 0;            // 1 to 16, or 0 = any
        juce::String device;        // the MIDI device it was learned from; empty = any
        int heel = 0, toe = 127;    // the values at each end (heel above toe = a pedal that works backwards)
        bool operator== (const Setup&) const = default;
    };

    ExpressionInput();

    /** Called on the message thread with each new position, 0 heel .. 100 toe. Set once, before use. */
    std::function<void (float)> onMove;

    /** 0..100 for a controller value. The last few values at each end count as the end itself,
        so a pedal that stops at 2 or 125 still reaches a true heel and toe. */
    static float positionFor (const Setup&, int value) noexcept;

    Setup setup() const;
    void setSetup (const Setup&);
    bool isSetUp() const                                { return setup().cc >= 0; }

    /** Raw MIDI from any device, on whichever thread it arrives. */
    void handleMidi (const juce::String& deviceName, const juce::MidiMessage&);

    /** Starts watching every controller. Rock the pedal fully both ways, then call finishLearning. */
    void startLearning();
    bool isLearning() const noexcept                    { return learning.load(); }
    /** Ends learning. Returns a line for the screen: what it found, or why it found nothing
        (the old set-up is then kept). */
    juce::String finishLearning();
    /** What learning has seen so far, for the screen while the pedal is rocked. */
    juce::String learningLine() const;

    /** Hands the newest position to onMove if one is waiting. The app does this by itself on
        the message thread; the self-test calls it directly. */
    void deliver();
    /** False in the self-test, where there is no message loop to deliver through. */
    void setDeliverByItself (bool b) noexcept           { selfDeliver = b; }

    float position() const noexcept                     { return lastPosition.load(); }
    int messages() const noexcept                       { return count.load(); }

    juce::String stateText() const;
    void loadStateText (const juce::String&);
    static juce::File stateFile();
    void saveState() const;
    static inline std::atomic<bool> memoryOnDisk { true };

    /** One line for the screen. */
    juce::String statusLine() const;
    /** The pedal's part of the report. */
    juce::String reportText() const;

private:
    struct Seen { int low = 128, high = -1, messages = 0, channel = 0; std::array<bool, 128> values {}; juce::String device; };

    mutable juce::CriticalSection lock;
    Setup now;
    std::array<Seen, 128> seen;                 // while learning, by controller number
    std::atomic<bool> learning { false }, pending { false };
    std::atomic<int> latest { 0 }, count { 0 };
    std::atomic<float> lastPosition { -1.0f };
    bool selfDeliver = true;
    juce::String lastLearned;
    std::shared_ptr<int> alive = std::make_shared<int> (0);     // so a delivery still waiting when the app closes does nothing

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExpressionInput)
};

} // namespace gigrig
