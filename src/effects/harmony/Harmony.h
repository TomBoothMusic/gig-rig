#pragma once

#include "../Effect.h"
#include "../HalfRate.h"

#include <array>
#include <atomic>
#include <vector>

namespace gigrig::fx
{

/** "harmony": a vocal harmoniser (first version, 25 Nov 2026). It adds one harmony voice
    that follows the note being sung and stays inside the song's key, like the RC-600's
    Harmonist in its "manual" (key) mode: sing a C in the key of C with "3rd above" and the
    harmony sings E; sing a D and it sings F.

    Settings: Key (the twelve major keys, each shown with its relative minor, which uses the
    same notes), Voice (a 3rd, 4th, 5th or 6th above or below, an octave, or a unison double),
    and Level (how loud the harmony is; the lead voice always passes untouched and undelayed).
    A second harmony voice (26 Nov 2026) has its own Voice and Level and follows the same key
    and the same pitch tracker; it is Off unless chosen. Two harmony effects in a row would not
    do this: the second would hear the lead and the first harmony mixed.

    How it works, in three parts (all in this folder, nothing else needed):
      1. A pitch tracker listens to a low-passed copy of the voice and finds how long one
         cycle of the note is (the "period"), about 190 times a second.
      2. The voice is cut into grains two periods long, each centred on a pulse of the vocal
         cords. Playing the grains back closer together raises the pitch, further apart
         lowers it. Because the grains themselves are not sped up or slowed down, the
         character of the voice (its "formants") stays put: no chipmunk, no giant.
         This is the classic PSOLA / Lent method.
      3. The note sung is matched to the key, and the distance to the harmony note (3 or 4
         semitones for a 3rd, and so on) sets how much closer or further the grains go.
    Breaths and consonants (no note to follow) pass into the harmony quietly, unshifted.

    The harmony voice comes out about 24 ms after the lead (it needs two cycles of the
    lowest note it can follow, 85 Hz, before it can sing). The lead voice has no delay. */
class Harmony final : public Effect
{
public:
    Harmony();
    std::string typeId() const override                 { return "harmony"; }
    double cpuBudgetPercent() const noexcept override   { return 10.0; }
    std::string status() const override;

    /** Semitones from the note sung to the harmony note. midiNote: 60 = middle C. */
    static int interval (int key, int voice, int midiNote) noexcept;
    static const std::vector<std::string>& keyNames();
    static const std::vector<std::string>& voiceNames();
    static const std::vector<std::string>& secondVoiceNames();      // "Off", then the same list
    /** How late the harmony voice is, in seconds. */
    static double harmonyDelaySeconds() noexcept        { return 2.0 / lowestHz + 0.0002; }

    static constexpr double lowestHz = 85.0, highestHz = 1000.0;

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;
    void runInner (float* x, int m) noexcept;
    void trackerFeed (float x) noexcept;
    void trackerFinish() noexcept;
    void placeMarks() noexcept;
    void writeGrain (std::vector<float>& into, double centre, double source, double period, float gain) noexcept;

    int pKey, pVoice, pLevel, pVoice2, pLevel2;
    HalfRate bridge;
    double inner = 48000.0;

    // ---- the pitch tracker ----
    static constexpr int window = 512;
    Biquad low1, low2, high;
    int decimate = 4, decimateCount = 0;
    std::array<float, window> ring {}, snap {};
    int ringPos = 0, sinceUpdate = 0;
    int lagMin = 12, lagMax = 141, lagCursor = 0;      // lagCursor 0 = idle, else the lag being worked out
    std::vector<float> nsdf;
    double snapPower = 0.0;
    double period = 240.0;          // one cycle of the note, in samples at the inner rate
    bool voiced = false;
    int note = 60;                  // the note the voice is taken to be on
    double pendingHz = 0.0;         // a big jump waits for a second opinion
    int unvoicedRuns = 0;

    // ---- grains ----
    static constexpr int bufferSize = 16384, mask = bufferSize - 1;
    struct Mark { double pos = 0.0; double period = 240.0; bool voiced = false; };
    static constexpr int numMarks = 128;

    /** One harmony voice: its own grains, everything else (tracker, marks, the voice heard) is shared. */
    struct Part
    {
        std::vector<float> ola;
        double nextGrain = 0.0;
        float ratioNow = 1.0f, grainGain = 0.0f;
    };
    static constexpr int numParts = 2;
    std::array<Part, numParts> parts;

    std::vector<float> in, energy;
    std::array<Mark, numMarks> marks {};
    int markCount = 0;              // marks ever made (the newest is marks[(markCount - 1) % numMarks])
    long long now = 0;              // inner samples since the start
    double nextMark = 0.0;
    int delay = 1152;               // how far behind the lead the harmony is read, in inner samples
    double maxPeriod = 565.0, restPeriod = 240.0;
    OnePole energyHigh;
    double energyNow = 0.0, energyCoeff = 0.03;

    // ---- for the screen and the report ----
    std::atomic<int> shownNote { -1 }, shownInterval { 0 }, shownInterval2 { 0 };
    std::atomic<bool> shownSecond { false };
};

} // namespace gigrig::fx
