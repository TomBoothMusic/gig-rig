#pragma once

#include "../DelayLine.h"
#include "../Effect.h"

namespace gigrig::fx
{

/** "digitaldelay": a clean digital delay with modulated repeats, in the spirit of a Boss DD
    series or TC Flashback with a touch of chorus on the echoes. Follow tempo: the time is a
    note value (1/4, dotted 1/8 ...) at the song's tempo, which comes from the looper or a
    tap; with it off, or before any tempo is known, the Time knob sets it in milliseconds.

    A change of time crossfades from the old echo to the new one in 40 ms, so the repeats
    never bend in pitch (that is the tape and analog delays' job).

    Throw: for a delay that only catches a phrase ("...after love, after love, after love").
    Off = the delay hears everything, as always. Ready = nothing goes in, so the sound stays
    clean. Catching = the delay hears what is played or sung. A held pedal switch moves it
    from Ready to Catching and back (EffectsRack::holdThrow); what was caught rings out on
    its own after the switch is let go. The way in opens and shuts over a few thousandths of
    a second, so it never clicks. Any effect with a choice setting called "throw" laid out
    the same way gets the same switch. */
class DigitalDelay final : public Effect
{
public:
    DigitalDelay();
    std::string typeId() const override         { return "digitaldelay"; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;
    double targetSamples() const noexcept;

    int pFollow, pDivision, pTime, pFeedback, pMix, pTone, pMod, pModRate, pThrow;
    DelayLine line;
    Lfo lfo;
    OnePole fbLow, fbHigh;
    double tapA = 0.0, tapB = 0.0;   // samples: the echo now, and the one being faded to
    int fadeLeft = 0, fadeLength = 1;
    float lastTone = -1.0f;
    float send = 1.0f;               // how far the way into the delay is open (the throw)
};

} // namespace gigrig::fx
