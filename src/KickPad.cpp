#include "KickPad.h"
#include "DeviceReport.h"
#include "MidiMonitor.h"

#include <cmath>

namespace gigrig
{

namespace
{
    constexpr double twoPi = 6.283185307179586;

    // The built-in kick, to Tom's description of the one he uses (26 Nov 2026): a real-drum
    // kind of kick, strongest around 55 Hz to stay under the bass guitar, very little in the
    // middle, and a click up high that still tells on speakers with no bass.
    // Where the drum settles (55 Hz) and how long its body rings (0.17 s) are the Pitch and Length settings.
    constexpr double sweepSeconds = 0.018;      // the pitch falls onto 55 Hz this quickly: the "thump"
    constexpr double sweepSoftHz = 45.0;        // how far above 55 Hz a soft hit starts
    constexpr double sweepHardHz = 120.0;       // and a hard one: a harder hit bends the head further
    constexpr double clickHz = 3200.0;          // the beater
    constexpr double clickSeconds = 0.0035;
    constexpr double noiseSeconds = 0.0025;
    constexpr double lengthsPlayed = 5.3;       // the whole sound lasts this many times the Length setting; the last 20 ms fade to nothing
    constexpr double tailSeconds = 0.02;
    constexpr double fadeSeconds = 0.005;       // a kick still sounding when the next one lands fades out this fast
    constexpr double splitHz = 400.0;           // a recording: what lies above this is "the click"
    constexpr float bodyLevel = 0.78f, clickLevel = 0.16f, noiseLevel = 0.10f;

    float decayFor (double seconds, double rate) noexcept    { return (float) std::exp (-1.0 / (seconds * rate)); }
    float onePole (double hz, double rate) noexcept          { return (float) (1.0 - std::exp (-twoPi * hz / rate)); }

    juce::String noteName (int note)
    {
        return juce::MidiMessage::getMidiNoteName (note, true, true, 4) + " (number " + juce::String (note) + ")";
    }
}

bool looksLikeSpdOne (const juce::String& deviceName)
{
    const auto lower = deviceName.toLowerCase();
    return lower.contains ("spd-1") || lower.contains ("spd1") || lower.contains ("spd::one") || lower.contains ("spd-one");
}

float kickGainForStrength (int strength, float touch) noexcept
{
    const auto s = (float) juce::jlimit (1, 127, strength) / 127.0f;
    return std::pow (s, juce::jlimit (0.5f, 3.0f, touch));
}

float kickClickForStrength (int strength) noexcept
{
    const auto s = (float) (juce::jlimit (1, 127, strength) - 1) / 126.0f;
    return 0.15f + 0.85f * s;
}

KickPad::KickPad()
{
    prepare (48000.0);
}

//==============================================================================
void KickPad::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    fadeSamples = juce::jmax (1, (int) std::lround (fadeSeconds * rate));
    tailSamples = juce::jmax (1, (int) std::lround (tailSeconds * rate));
    sweepDecay = decayFor (sweepSeconds, rate);
    clickDecay = decayFor (clickSeconds, rate);
    noiseDecay = decayFor (noiseSeconds, rate);
    noiseSplit = onePole (1500.0, rate);
    sampleSplit = onePole (splitHz, rate);

    for (auto& v : voices)
        v = {};
}

void KickPad::startVoice (int strength) noexcept
{
    // The kick before this one, if it is still sounding, fades out underneath.
    voices[1] = voices[0];

    if (voices[1].active && voices[1].fadeLeft < 0)
        voices[1].fadeLeft = fadeSamples;

    const auto hard = (float) (juce::jlimit (1, 127, strength) - 1) / 126.0f;

    Voice v;
    v.active = true;
    v.gain = kickGainForStrength (strength, touch.load (std::memory_order_relaxed)) * level.load (std::memory_order_relaxed);
    v.clickAmount = juce::jmin (2.0f, kickClickForStrength (strength) * click.load (std::memory_order_relaxed));
    v.clickTone = onePole (2500.0 + 7000.0 * (double) hard, rate);
    v.bodyHz = pitch.load (std::memory_order_relaxed);
    v.bodyDecay = decayFor ((double) length.load (std::memory_order_relaxed), rate);
    v.lengthSamples = (int) std::lround (lengthsPlayed * (double) length.load (std::memory_order_relaxed) * rate);
    v.sweepHz = (float) (sweepSoftHz + (sweepHardHz - sweepSoftHz) * (double) hard);
    v.sweepEnv = v.bodyEnv = v.clickEnv = v.noiseEnv = 1.0f;
    v.noiseSeed = 0x9e3779b9u;          // the same click every time: a kick should not change from hit to hit
    v.usesSample = playing != nullptr && ! playing->data.empty();
    voices[0] = v;
}

float KickPad::nextSample (Voice& v) noexcept
{
    float out = 0.0f;

    if (v.usesSample)
    {
        const auto index = (size_t) v.position;

        if (playing == nullptr || index + 1 >= playing->data.size())
        {
            v.active = false;
            return 0.0f;
        }

        const auto& data = playing->data;

        const auto frac = (float) (v.position - (double) index);
        const auto x = data[index] + frac * (data[index + 1] - data[index]);
        v.position += playing->rate / rate;

        // What lies above the split is the click: a softer hit keeps less of it.
        v.splitLow += sampleSplit * (x - v.splitLow);
        out = v.splitLow + (x - v.splitLow) * v.clickAmount;
    }
    else
    {
        const auto n = (int) v.position;

        if (n >= v.lengthSamples)
        {
            v.active = false;
            return 0.0f;
        }

        // The drum: a sine that falls quickly onto 55 Hz and dies away.
        const auto body = (float) std::sin (v.bodyPhase) * v.bodyEnv;
        v.bodyPhase += twoPi * (double) (v.bodyHz + v.sweepHz * v.sweepEnv) / rate;

        if (v.bodyPhase > twoPi)
            v.bodyPhase -= twoPi;

        // The beater: a few milliseconds of a high ring and a puff of hiss, darker for a soft hit.
        v.noiseSeed = v.noiseSeed * 1664525u + 1013904223u;
        const auto white = (float) (int) (v.noiseSeed >> 8) * (1.0f / 8388608.0f) - 1.0f;
        v.noiseLow += noiseSplit * (white - v.noiseLow);
        const auto raw = clickLevel * (float) std::sin (v.clickPhase) * v.clickEnv + noiseLevel * (white - v.noiseLow) * v.noiseEnv;
        v.clickPhase += twoPi * clickHz / rate;
        v.clickLow += v.clickTone * (raw - v.clickLow);

        v.sweepEnv *= sweepDecay;
        v.bodyEnv *= v.bodyDecay;
        v.clickEnv *= clickDecay;
        v.noiseEnv *= noiseDecay;
        v.position += 1.0;

        out = bodyLevel * body + v.clickLow * v.clickAmount;

        if (const auto left = v.lengthSamples - n; left < tailSamples)
            out *= (float) left / (float) tailSamples;
    }

    out *= v.gain;

    if (v.fadeLeft >= 0)
    {
        out *= (float) v.fadeLeft / (float) fadeSamples;

        if (--v.fadeLeft < 0)
            v.active = false;
    }

    return out;
}

void KickPad::process (float* left, float* right, int numSamples, bool quiet) noexcept
{
    // A different recording was chosen: take it, and leave the old one for the other thread to free.
    if (sampleChanged.load (std::memory_order_relaxed))
    {
        const juce::SpinLock::ScopedTryLockType tl (sampleLock);

        if (tl.isLocked())
        {
            std::swap (playing, waiting);
            sampleChanged.store (false, std::memory_order_relaxed);

            for (auto& v : voices)
                v.active = false;
        }
    }

    // Hits that arrived since the last block start at its first sample.
    if (const auto ready = fifo.getNumReady(); ready > 0)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (ready, start1, size1, start2, size2);
        const auto now = juce::Time::getHighResolutionTicks();

        for (int i = 0; i < size1 + size2; ++i)
        {
            const auto& q = queue[(size_t) (i < size1 ? start1 + i : start2 + i - size1)];

            if (quiet || ! on.load (std::memory_order_relaxed))
                continue;

            if (q.ticks > 0)
            {
                const auto waited = juce::Time::highResolutionTicksToSeconds (now - q.ticks) * 1000.0;
                waitSumMs.store (waitSumMs.load (std::memory_order_relaxed) + waited, std::memory_order_relaxed);

                if (waited > waitWorstMs.load (std::memory_order_relaxed))
                    waitWorstMs.store (waited, std::memory_order_relaxed);

                startedHits.fetch_add (1, std::memory_order_relaxed);
            }

            startVoice (q.strength);
        }

        fifo.finishedRead (size1 + size2);
    }

    if (quiet)
    {
        for (auto& v : voices)
            v.active = false;

        return;
    }

    if (! voices[0].active && ! voices[1].active)
        return;     // nothing sounding: the outputs are left exactly as they were

    for (int i = 0; i < numSamples; ++i)
    {
        float s = 0.0f;

        for (auto& v : voices)
            if (v.active)
                s += nextSample (v);

        if (left != nullptr)    left[i] += s;
        if (right != nullptr)   right[i] += s;
    }
}

//==============================================================================
void KickPad::setSample (std::vector<float> samples, double sampleRate, const juce::String& name)
{
    auto fresh = std::make_shared<Sample>();
    fresh->data = std::move (samples);
    fresh->rate = sampleRate > 0.0 ? sampleRate : 48000.0;

    std::shared_ptr<const Sample> old;

    {
        const juce::SpinLock::ScopedLockType sl (sampleLock);
        old = std::move (waiting);
        waiting = fresh->data.empty() ? nullptr : std::move (fresh);
        sampleChanged.store (true);
    }

    const juce::ScopedLock sl (lock);
    sampleLabel = name;
}

juce::String KickPad::sampleName() const
{
    const juce::ScopedLock sl (lock);
    return sampleLabel;
}

void KickPad::setPad (const juce::String& deviceName, int padNoteNumber)
{
    const juce::ScopedLock sl (lock);
    device = deviceName;
    note = padNoteNumber;
    learned = deviceName.isNotEmpty();
    foundByName = false;
}

juce::String KickPad::padDevice() const
{
    const juce::ScopedLock sl (lock);
    return device;
}

int KickPad::padNote() const
{
    const juce::ScopedLock sl (lock);
    return note;
}

void KickPad::handleMidi (const juce::String& deviceName, const juce::MidiMessage& m)
{
    if (m.isMidiClock() || m.isActiveSense())
        return;

    const juce::ScopedLock sl (lock);

    if (! m.isNoteOn())
    {
        // Anything else the pad sends is worth knowing about: counted, and each kind named once.
        if (device.isNotEmpty() && deviceName == device)
        {
            if (m.isNoteOff())
            {
                ++noteOffs;
            }
            else
            {
                ++otherMessages;
                const auto kind = MidiMonitor::describe (m);

                if (otherKinds.size() < 8 && ! otherKinds.contains (kind))
                    otherKinds.add (kind);
            }
        }

        return;
    }

    if (learning.exchange (false))
    {
        device = deviceName;
        note = m.getNoteNumber();
        learned = true;
        foundByName = false;
    }
    else if (device.isEmpty() && looksLikeSpdOne (deviceName))
    {
        // Tom's pad, known by its name. Every note it sends counts, so a change of sound on the
        // pad that changes the note shows up in the report instead of going quiet.
        device = deviceName;
        note = -1;
        foundByName = true;
    }

    if (device.isEmpty() || deviceName != device || (note >= 0 && m.getNoteNumber() != note))
    {
        ++otherNotes;
        return;
    }

    queueHit (m.getVelocity(), false, deviceName, m.getNoteNumber(), m.getChannel());
}

void KickPad::screenHit (int strength)
{
    const juce::ScopedLock sl (lock);
    queueHit (strength, true, {}, -1, 0);
}

void KickPad::queueHit (int strength, bool fromScreen, const juce::String&, int noteNumber, int channelNumber)
{
    // Called with the lock held, so two threads never write to the queue at once.
    strength = juce::jlimit (1, 127, strength);

    int start1, size1, start2, size2;
    fifo.prepareToWrite (1, start1, size1, start2, size2);

    if (size1 + size2 > 0)
    {
        queue[(size_t) (size1 > 0 ? start1 : start2)] = { strength, juce::Time::getHighResolutionTicks() };
        fifo.finishedWrite (1);
    }

    Hit hit;
    hit.timeMs = juce::Time::currentTimeMillis();
    hit.strength = strength;
    hit.fromScreen = fromScreen;

    if (fromScreen)
    {
        ++screenHits;
    }
    else
    {
        channel = channelNumber;

        if (const auto name = noteName (noteNumber); notesSeen.size() < 8 && ! notesSeen.contains (name))
            notesSeen.add (name);

        if (hits > 0)
        {
            hit.gapMs = (int) juce::jmin ((juce::int64) 999999, hit.timeMs - lastHitMs);
            closestGapMs = closestGapMs < 0 ? hit.gapMs : juce::jmin (closestGapMs, hit.gapMs);

            if (hit.gapMs < doubleHitMs)
                ++doubles;
        }

        softest = hits == 0 ? strength : juce::jmin (softest, strength);
        hardest = juce::jmax (hardest, strength);
        ++strengthCounts[(size_t) strength];
        ++hits;
        lastHitMs = hit.timeMs;
    }

    recent.push_back (hit);

    if ((int) recent.size() > maxRecent)
        recent.erase (recent.begin());
}

std::vector<KickPad::Hit> KickPad::recentHits() const
{
    const juce::ScopedLock sl (lock);
    return recent;
}

int KickPad::hitCount() const
{
    const juce::ScopedLock sl (lock);
    return hits;
}

void KickPad::resetCounts()
{
    const juce::ScopedLock sl (lock);
    recent.clear();
    hits = screenHits = softest = hardest = doubles = noteOffs = otherMessages = otherNotes = 0;
    closestGapMs = -1;
    strengthCounts.fill (0);
    otherKinds.clear();
    notesSeen.clear();
    startedHits.store (0);
    waitSumMs.store (0.0);
    waitWorstMs.store (0.0);
}

void KickPad::setBlockInfo (double newBlockMs, double newOutputDelayMs)
{
    const juce::ScopedLock sl (lock);
    blockMs = newBlockMs;
    outputDelayMs = newOutputDelayMs;
}

juce::String KickPad::reportText() const
{
    const juce::ScopedLock sl (lock);
    juce::StringArray lines;

    lines.add ("Kick pad test");
    lines.add ("  Kick pad: " + juce::String (on.load() ? "on" : "OFF")
                 + ". Sound: " + (sampleLabel.isEmpty() ? juce::String ("built-in kick") : "recording \"" + sampleLabel + "\"")
                 + ". Level " + juce::String (level.load(), 2) + ", touch " + juce::String (touch.load(), 2) + ", click " + juce::String (click.load(), 2)
                 + ", pitch " + juce::String (pitch.load(), 0) + " Hz, length " + juce::String (length.load(), 2) + " s"
                 + ". Plays on returns " + juce::String (returnPair.load() * 2 + 1) + " and " + juce::String (returnPair.load() * 2 + 2) + ".");

    if (device.isEmpty())
        lines.add ("  Pad: none seen yet. (The app looks for a MIDI device with SPD-1 in its name; Learn pad takes any other.)");
    else
        lines.add ("  Pad: \"" + device + "\" " + (foundByName ? "(found by its name)" : "(learned)")
                     + (note >= 0 ? ", note " + noteName (note) + " only" : juce::String (", any note"))
                     + (channel > 0 ? ", MIDI channel " + juce::String (channel) : juce::String()) + ".");

    if (hits == 0)
    {
        lines.add ("  Hits from the pad: none yet.");
    }
    else
    {
        int different = 0;

        for (auto c : strengthCounts)
            different += c > 0 ? 1 : 0;

        lines.add ("  Hits from the pad: " + juce::String (hits) + ". Softest strength " + juce::String (softest) + ", hardest " + juce::String (hardest)
                     + " (the scale is 1 to 127). " + juce::String (different) + " different strengths seen.");

        if (softest == hardest && hits >= 5)
            lines.add ("  EVERY HIT HAD THE SAME STRENGTH: the pad is not sending how hard it was hit.");

        juce::String bands = "  How many hits at each strength:";

        for (int band = 0; band < 8; ++band)
        {
            int count = 0;

            for (int s = band * 16; s < band * 16 + 16; ++s)
                count += strengthCounts[(size_t) s];

            bands << "  " << juce::jmax (1, band * 16) << "-" << (band * 16 + 15) << ": " << count;
        }

        lines.add (bands);
        lines.add ("  Notes the pad sent: " + notesSeen.joinIntoString (", ") + ".");

        if (closestGapMs >= 0)
            lines.add ("  Closest two hits: " + juce::String (closestGapMs) + " ms apart. Hits under " + juce::String (doubleHitMs)
                         + " ms apart (one stomp counted twice?): " + juce::String (doubles) + ".");
    }

    if (const auto started = startedHits.load(); started > 0)
        lines.add ("  From a hit arriving to the kick starting: typical " + juce::String (waitSumMs.load() / started, 2) + " ms, longest "
                     + juce::String (waitWorstMs.load(), 2) + " ms"
                     + (blockMs > 0.0 ? " (one block of audio is " + juce::String (blockMs, 2) + " ms; the driver's own delay, in and out together, is "
                                          + juce::String (outputDelayMs, 2) + " ms)" : juce::String()) + ".");

    lines.add ("  Other things the pad sent: " + juce::String (noteOffs) + " note-off, " + juce::String (otherMessages) + " other"
                 + (otherKinds.isEmpty() ? juce::String() : " (" + otherKinds.joinIntoString ("; ") + ")")
                 + ". Notes ignored (another device or another note): " + juce::String (otherNotes) + ".");
    lines.add ("  Hits from the on-screen buttons: " + juce::String (screenHits) + ".");

    if (! recent.empty())
    {
        lines.add ("  The last hits, oldest first (strength, then the time since the hit before):");
        juce::String row = "   ";
        int inRow = 0;

        for (const auto& h : recent)
        {
            row << " " << juce::String (h.strength).paddedLeft (' ', 3)
                << (h.fromScreen ? " (screen)" : h.gapMs >= 0 ? " +" + juce::String (h.gapMs) + "ms" : juce::String (" first")) << "  |";

            if (++inRow == 6)
            {
                lines.add (row);
                row = "   ";
                inRow = 0;
            }
        }

        if (inRow > 0)
            lines.add (row);
    }

    return lines.joinIntoString ("\n") + "\n";
}

} // namespace gigrig
