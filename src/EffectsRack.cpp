#include "EffectsRack.h"
#include "effects/Registry.h"
#include "effects/amp/NamAmp.h"

#include <juce_events/juce_events.h>

#include <random>

namespace gigrig
{

namespace
{
    constexpr double playingLevel = 0.003;      // about -50 dBFS: counts as "playing" for the level match
    constexpr double twoPi = 6.283185307179586;

    float peakOf (const float* d, int n) noexcept
    {
        float p = 0.0f;
        for (int i = 0; i < n; ++i)
            p = juce::jmax (p, std::abs (d[i]));
        return p;
    }

    void raise (std::atomic<float>& slot, float v) noexcept
    {
        if (v > slot.load (std::memory_order_relaxed))
            slot.store (v, std::memory_order_relaxed);
    }

    void addTo (std::atomic<double>& slot, double v) noexcept
    {
        slot.store (slot.load (std::memory_order_relaxed) + v, std::memory_order_relaxed);
    }

    const char* listenWord (EffectsRack::Listen l)
    {
        switch (l)
        {
            case EffectsRack::Listen::effects:   return "effects";
            case EffectsRack::Listen::dry:       return "dry";
            case EffectsRack::Listen::reference: return "reference";
        }
        return "effects";
    }

    EffectsRack::Listen listenFromWord (const juce::String& w)
    {
        if (w == "dry")         return EffectsRack::Listen::dry;
        if (w == "reference")   return EffectsRack::Listen::reference;
        return EffectsRack::Listen::effects;
    }

    std::unique_ptr<juce::AudioFormatWriter> wavWriter (const juce::File& file, double rate, int channels, juce::String& error)
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

        if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
        {
            error = "Could not write to " + file.getFullPathName();
            return {};
        }

        juce::WavAudioFormat wav;
        auto made = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                     .withSampleRate (rate)
                                                     .withNumChannels (channels)
                                                     .withBitsPerSample (24));
        if (made == nullptr)
            error = "Could not start a WAV file at " + juce::String (rate) + " Hz";

        return made;
    }
}

//==============================================================================
EffectsRack::EffectsRack()
{
    for (int c = 0; c < numChains; ++c)
    {
        auto& r = chains[(size_t) c].route;
        r.name = c == 0 ? "Drive test" : "Chain " + juce::String (c + 1);
        r.source = c == 0 ? 9 : 9 - c;
    }

    // A first chain to start from: the drives Tom asked for, the boost after them, all off but one.
    fx::ChainModel m;
    m.slots.push_back ({ "ts", true, {}, {} });
    m.slots.push_back ({ "bd", false, {}, {} });
    m.slots.push_back ({ "klon", false, {}, {} });
    m.slots.push_back ({ "marshall", false, {}, {} });
    chains[0].chain.setModel (m);

    for (int c = 0; c < numChains; ++c)
        pushRouting (c);

    for (int i = 0; i < numPedalAssigns; ++i)
    {
        assignSw[(size_t) i].store (-1);
        assignChain[(size_t) i].store (0);
        assignAction[(size_t) i].store (0);
    }

    writerThread.startThread();
}

EffectsRack::~EffectsRack()
{
    stopCapture();
    stopAmpCapture();
    writerThread.stopThread (2000);
}

EffectsRack::Routing EffectsRack::routing (int c) const
{
    return chains[(size_t) c].route;
}

void EffectsRack::pushRouting (int c)
{
    auto& s = chains[(size_t) c];
    const auto& r = s.route;
    s.source.store (r.source);
    s.outFirst.store (r.out.firstReturn);
    s.outPair.store (r.out.pair);
    s.looperHears.store (r.looperHears);
    s.reference.store (r.reference);
    s.listen.store ((int) r.listen);
    s.trim.store (juce::Decibels::decibelsToGain (r.trimDb));
    s.on.store (r.on);
}

void EffectsRack::setRouting (int c, const Routing& r)
{
    auto& s = chains[(size_t) c];
    s.route = r;
    s.route.trimDb = juce::jlimit (-36.0f, 24.0f, r.trimDb);
    pushRouting (c);
    dirty = true;
}

void EffectsRack::setListen (int c, Listen l)
{
    auto r = routing (c);
    r.listen = l;
    setRouting (c, r);
}

void EffectsRack::toggleListen (int c)
{
    auto r = routing (c);
    r.listen = r.listen == Listen::effects ? (r.reference >= 0 ? Listen::reference : Listen::dry) : Listen::effects;
    setRouting (c, r);
}

EffectsRack::PedalAssign EffectsRack::pedalAssign (int i) const noexcept
{
    if (i < 0 || i >= numPedalAssigns)
        return {};

    return { assignSw[(size_t) i].load(), assignChain[(size_t) i].load(), assignAction[(size_t) i].load() };
}

void EffectsRack::setPedalAssign (int i, PedalAssign a) noexcept
{
    if (i < 0 || i >= numPedalAssigns)
        return;

    assignSw[(size_t) i].store (a.sw);
    assignChain[(size_t) i].store (juce::jlimit (0, numChains - 1, a.chain));
    assignAction[(size_t) i].store (a.action);
    dirty = true;
}

bool EffectsRack::takePedal (int sw, bool isDown, bool stillHeld)
{
    if (! enabled.load() || sw < 0)
        return false;

    // Every row that names this switch acts: two rows on one switch swap two chains with one press
    // (Tom, 27 Nov 2026: bass on and electric off). Before 27 Nov 2026 only the first row did.
    bool taken = false;

    for (int i = 0; i < numPedalAssigns; ++i)
    {
        const auto a = pedalAssign (i);

        if (a.sw != sw)
            continue;

        // "Held for 2 seconds" is news for the looper only. The switch is still down, so a
        // held wah, whammy, throw or harmony carries on (build 88 took it for a release).
        if (stillHeld)
            return true;

        if (isDown && a.action == 2)
            tap (juce::Time::getMillisecondCounterHiRes());     // at once, so the press time is exact
        else if (a.action == 3)
            juce::MessageManager::callAsync ([this, a, isDown]
            {
                bendWhammy (a.chain, isDown);
                if (onPedalAction != nullptr)
                    onPedalAction();
            });
        else if (a.action == 6)
            juce::MessageManager::callAsync ([this, a, isDown]
            {
                sweepWah (a.chain, isDown);
                if (onPedalAction != nullptr)
                    onPedalAction();
            });
        else if (a.action == 8)
            juce::MessageManager::callAsync ([this, a, isDown]
            {
                holdHarmony (a.chain, isDown);
                if (onPedalAction != nullptr)
                    onPedalAction();
            });
        else if (a.action == actionPedalToe)
            juce::MessageManager::callAsync ([this, isDown]
            {
                moveExpressionPedal (isDown ? 100.0f : 0.0f);
                if (onPedalAction != nullptr)
                    onPedalAction();
            });
        else if (a.action == 5)
            juce::MessageManager::callAsync ([this, a, isDown]
            {
                holdThrow (a.chain, isDown);
                if (onPedalAction != nullptr)
                    onPedalAction();
            });
        else if (isDown)
            juce::MessageManager::callAsync ([this, a] { doPedalAction (a); });

        taken = true;
    }

    return taken;
}

bool EffectsRack::rockPedal (int c, const char* type, bool toeDown)
{
    auto& ch = chain (juce::jlimit (0, numChains - 1, c));

    for (int k = 0; k < (int) ch.model().slots.size(); ++k)
    {
        if (ch.model().slots[(size_t) k].type != type)
            continue;

        if (auto* fx = ch.effect (k))
        {
            ch.setParam (k, fx->paramIndex ("pedal"), toeDown ? 100.0f : 0.0f);
            dirty = true;
            return true;
        }
    }

    return false;
}

int EffectsRack::moveVolumePedals (float position)
{
    int moved = 0;

    for (auto& slot : chains)
    {
        auto& ch = slot.chain;

        for (int k = 0; k < (int) ch.model().slots.size(); ++k)
            if (ch.model().slots[(size_t) k].type == "volume")
                if (auto* fx = ch.effect (k))
                {
                    ch.setParam (k, fx->paramIndex ("pedal"), juce::jlimit (0.0f, 100.0f, position));
                    ++moved;
                }
    }

    if (moved > 0)
        dirty = true;

    return moved;
}

namespace
{
    /** The first wah or whammy in a row: the effect a toe switch engages. -1 if none. */
    int expressionSlot (const fx::ChainModel& m)
    {
        for (int k = 0; k < (int) m.slots.size(); ++k)
            if (m.slots[(size_t) k].type == "wah" || m.slots[(size_t) k].type == "whammy")
                return k;

        return -1;
    }
}

void EffectsRack::moveExpressionPedal (float position)
{
    position = juce::jlimit (0.0f, 100.0f, position);
    pedalMoved = true;

    if (toeOn)
    {
        auto& ch = chain (toeChain);

        if (const int k = expressionSlot (ch.model()); k >= 0)
            if (auto* fx = ch.effect (k))
            {
                ch.setParam (k, fx->paramIndex ("pedal"), position);
                dirty = true;
                return;
            }

        toeOn = false;      // the song changed and that effect has gone: the pedal is the blend again
    }

    blendPosition = position;
    moveVolumePedals (position);
}

bool EffectsRack::toeSwitch (int c)
{
    c = juce::jlimit (0, numChains - 1, c);
    auto& ch = chain (toeOn ? toeChain : c);
    const int k = expressionSlot (ch.model());

    if (toeOn)
    {
        if (k >= 0)
            ch.setSlotOn (k, false);

        toeOn = false;
        moveVolumePedals (blendPosition);
    }
    else
    {
        // Where the blend is now, to come back to: the first volume pedal that is not reversed.
        for (auto& slot : chains)
            for (const auto& sm : slot.chain.model().slots)
                if (sm.type == "volume" && sm.param ("reverse", 0.0f) < 0.5f)
                    blendPosition = sm.param ("pedal", blendPosition);

        moveVolumePedals (100.0f);      // electric full, the reversed acoustic silent

        if (k >= 0)
            ch.setSlotOn (k, true);

        toeOn = true;
        toeChain = c;
    }

    dirty = true;
    return toeOn;
}

bool EffectsRack::bendWhammy (int c, bool toeDown)    { return rockPedal (c, "whammy", toeDown); }
bool EffectsRack::sweepWah (int c, bool toeDown)      { return rockPedal (c, "wah", toeDown); }

juce::StringArray EffectsRack::recipe (int c) const         { return chains[(size_t) juce::jlimit (0, numChains - 1, c)].recipe; }
juce::StringArray EffectsRack::ampCycle (int c) const       { return chains[(size_t) juce::jlimit (0, numChains - 1, c)].ampCycle; }

const fx::ChainModel& EffectsRack::baseline (int c) const   { return chains[(size_t) juce::jlimit (0, numChains - 1, c)].baseline; }

void EffectsRack::setBaseline (int c, const fx::ChainModel& m)
{
    chains[(size_t) juce::jlimit (0, numChains - 1, c)].baseline = m;
    dirty = true;
}

void EffectsRack::setRecipe (int c, const juce::StringArray& presets)
{
    auto& mine = chains[(size_t) juce::jlimit (0, numChains - 1, c)].recipe;

    if (mine != presets)
    {
        mine = presets;
        dirty = true;
    }
}

void EffectsRack::setTailsRingOut (bool shouldRing)
{
    tails = shouldRing;
    dirty = true;

    for (auto& s : chains)
        s.chain.setTailsRingOut (shouldRing);
}

int EffectsRack::tailsRinging() const noexcept
{
    int n = 0;
    for (const auto& s : chains)
        n += s.chain.tailRinging() ? 1 : 0;
    return n;
}

void EffectsRack::setAmpCycle (int c, const juce::StringArray& models)
{
    chains[(size_t) juce::jlimit (0, numChains - 1, c)].ampCycle = models;
}

juce::String EffectsRack::nextAmp (int c)
{
    c = juce::jlimit (0, numChains - 1, c);
    auto& ch = chain (c);
    const juce::String label = "Chain " + juce::String (c + 1) + " amp: ";

    for (int k = 0; k < (int) ch.model().slots.size(); ++k)
    {
        if (ch.model().slots[(size_t) k].type != "amp")
            continue;

        auto* fx = ch.effect (k);

        if (fx == nullptr)
            break;

        auto list = chains[(size_t) c].ampCycle;

        if (list.isEmpty())
            for (const auto& f : juce::File (juce::String (fx::NamAmp::modelFolder())).findChildFiles (juce::File::findFiles, false, "*.nam"))
                list.add (f.getFileNameWithoutExtension());

        if (chains[(size_t) c].ampCycle.isEmpty())
            list.sort (true);

        if (list.isEmpty())
            return ampNote = label + "no amp models to step through";

        const auto now = juce::String (fx->getText ("model")).upToLastOccurrenceOf (".nam", false, true);
        int at = -1;

        for (int i = 0; i < list.size(); ++i)
            if (list[i].equalsIgnoreCase (now))
                at = i;

        const auto next = list[(at + 1) % list.size()];
        ch.setText (k, "model", next.toStdString());
        dirty = true;
        return ampNote = label + next + (fx->status().empty() ? juce::String() : "  [" + juce::String (fx->status()) + "]");
    }

    return ampNote = label + "this chain has no amp";
}

bool EffectsRack::holdThrow (int c, bool held)
{
    auto& ch = chain (juce::jlimit (0, numChains - 1, c));

    for (int k = 0; k < (int) ch.model().slots.size(); ++k)
        if (auto* fx = ch.effect (k))
            if (const int p = fx->paramIndex ("throw"); p >= 0)
            {
                ch.setParam (k, p, held ? 2.0f : 1.0f);
                dirty = true;
                return true;
            }

    return false;
}

bool EffectsRack::holdHarmony (int c, bool held)
{
    auto& ch = chain (juce::jlimit (0, numChains - 1, c));

    for (int k = 0; k < (int) ch.model().slots.size(); ++k)
        if (auto* fx = ch.effect (k))
            if (const int p = fx->paramIndex ("hold"); p >= 0)
            {
                // A song saves the harmony switched off with its key set. The first press
                // switches it on; from then on it waits, silent and already following the voice.
                if (held && ! ch.model().slots[(size_t) k].on)
                    ch.setSlotOn (k, true);

                ch.setParam (k, p, held ? 2.0f : 1.0f);
                dirty = true;
                return true;
            }

    return false;
}

void EffectsRack::doPedalAction (const PedalAssign& a)
{
    const int c = juce::jlimit (0, numChains - 1, a.chain);

    if (a.action == 2)
        tap (juce::Time::getMillisecondCounterHiRes());
    else if (a.action == 4)
        takeNote = takeStep();
    else if (a.action == 7)
        nextAmp (c);
    else if (a.action == 9)
        toeSwitch (c);
    else if (a.action == 0)
        toggleListen (c);
    else if (a.action == 1)
    {
        auto r = routing (c);
        r.on = ! r.on;
        setRouting (c, r);
    }
    else if (a.action >= 10 && a.action != actionPedalToe)
    {
        const int k = a.action - 10;
        auto& ch = chain (c);

        if (k < (int) ch.model().slots.size())
            ch.setSlotOn (k, ! ch.model().slots[(size_t) k].on);

        dirty = true;
    }

    if (onPedalAction != nullptr)
        onPedalAction();
}

juce::String EffectsRack::pedalAssignText (const PedalAssign& a, bool brief) const
{
    const auto& m = chains[(size_t) a.chain].chain.model();
    if (a.action == actionPedalToe)
        return brief ? "pedal toe down while held" : "expression pedal toe down while held, heel down when let go (stands in for the pedal until there is one)";

    juce::String what = a.action == 2 ? "tap tempo" : a.action == 3 ? "whammy bend while held"
                      : a.action == 4 ? (brief ? "test take" : "test take: record, then play round and round, then stop")
                      : a.action == 5 ? "delay throw while held" : a.action == 6 ? "wah sweep while held" : a.action == 8 ? "harmony while held"
                      : a.action == 9 ? (brief ? (toeOn ? "toe switch (ON)" : "toe switch (off)")
                                               : (toeOn ? "toe switch (ON: electric full, acoustic off, the pedal works the wah or whammy)" : "toe switch (off: the pedal is the acoustic and electric blend)"))
                      : a.action == 7 ? "next amp sound" : a.action == 0 ? "A/B" : a.action == 1 ? "chain on/off"
                      : "slot " + juce::String (a.action - 9) + (a.action - 10 < (int) m.slots.size()
                            ? " (" + juce::String (fx::findEffectType (m.slots[(size_t) (a.action - 10)].type)->name) + ")" : juce::String()) + " on/off";

    return (a.action == 2 || a.action == 4 ? juce::String() : "chain " + juce::String (a.chain + 1) + " ") + what;
}

juce::String EffectsRack::pedalText() const
{
    juce::String t;

    for (int i = 0; i < numPedalAssigns; ++i)
    {
        const auto a = pedalAssign (i);

        if (a.sw < 0)
            continue;

        t << "Pedal switch " << (a.sw < 9 ? juce::String (a.sw + 1) : a.sw == 9 ? juce::String ("CTL3") : juce::String ("CTL4"))
          << ": " << pedalAssignText (a, false) << " (the looper no longer hears this switch)\n";
    }

    return t;
}

juce::String EffectsRack::pedalUse (int sw) const
{
    // The same test as takePedal: every row that names the switch acts, in row order.
    if (! enabled.load() || sw < 0)
        return {};

    juce::StringArray uses;

    for (int i = 0; i < numPedalAssigns; ++i)
        if (const auto a = pedalAssign (i); a.sw == sw)
            uses.add (pedalAssignText (a, true));

    return uses.joinIntoString (" + ");
}

EffectsRack::Levels EffectsRack::takeLevels (int c) noexcept
{
    auto& s = chains[(size_t) c];
    Levels l;
    const auto n = s.played.exchange (0);
    const auto e = s.effSum.exchange (0.0), k = s.cmpSum.exchange (0.0);

    if (n > 0)
    {
        l.effectsDb = juce::Decibels::gainToDecibels (std::sqrt (e / (double) n), -100.0);
        l.compareDb = juce::Decibels::gainToDecibels (std::sqrt (k / (double) n), -100.0);
        l.playedSeconds = (double) n / rate;
    }

    return l;
}

void EffectsRack::tick()
{
    for (auto& s : chains)
        s.chain.collectGarbage();

    if (ampDone.load() && ampPlaying.load())
        stopAmpCapture();

    for (auto& s : chains)
    {
        s.loadRing[(size_t) s.loadRingAt] = s.loadPeak.exchange (0.0f);
        s.loadRingAt = (s.loadRingAt + 1) % (int) s.loadRing.size();
        if (! s.on.load() || ! enabled.load())
            s.loadAverage.store (0.0f);
    }

    // A take that ran to its full length ended itself on the audio thread: tidy it up here.
    if (take != nullptr && takeMode.load() == 0 && take->length == 0 && takeRecorded.load() > 0)
        stopTake();

    // Save a second after the last change, not on every slider movement.
    if (dirty)
    {
        dirty = false;
        ticksSinceChange = 1;
    }
    else if (ticksSinceChange > 0 && ++ticksSinceChange > 10)
    {
        ticksSinceChange = 0;
        saveState();
    }
}

//==============================================================================
juce::File EffectsRack::stateFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("GigRig").getChildFile ("effects-rack.txt");
}

juce::String EffectsRack::stateText() const
{
    juce::String t;
    t << "# GigRig effects rack: the app's own memory of the Effects tab. Safe to delete (it starts afresh).\n";
    t << "rack.on=" << (enabled.load() ? 1 : 0) << "\n";
    t << "rack.tails=" << (tails ? 1 : 0) << "\n";

    for (int i = 0; i < numPedalAssigns; ++i)
    {
        const auto a = pedalAssign (i);
        t << "pedal" << (i + 1) << "=" << a.sw << "," << a.chain << "," << a.action << "\n";
    }

    for (int c = 0; c < numChains; ++c)
    {
        const auto& r = chains[(size_t) c].route;
        t << "chain=" << (c + 1) << "\n";
        t << "chain.name=" << r.name.replaceCharacters ("\r\n", "  ") << "\n";
        t << "chain.on=" << (r.on ? 1 : 0) << "\n";
        t << "chain.source=" << (r.source + 1) << "\n";
        t << "chain.out=" << (r.out.firstReturn + 1) << (r.out.pair ? "+" : "") << "\n";
        t << "chain.looper=" << (r.looperHears < 0 ? juce::String (r.looperHears == -2 ? "same" : "none") : juce::String (r.looperHears + 1)) << "\n";
        t << "chain.listen=" << listenWord (r.listen) << "\n";
        t << "chain.reference=" << (r.reference + 1) << "\n";
        t << "chain.trim=" << r.trimDb << "\n";
        if (! chains[(size_t) c].recipe.isEmpty())
            t << "chain.recipe=" << chains[(size_t) c].recipe.joinIntoString ("|") << "\n";
        t << chains[(size_t) c].chain.model().toText();

        // The chain as last loaded, for "Update its presets" (see baseline()).
        if (! chains[(size_t) c].recipe.isEmpty())
            for (const auto& line : juce::StringArray::fromLines (juce::String (chains[(size_t) c].baseline.toText())))
                if (line.isNotEmpty() && ! line.startsWithChar ('#'))
                    t << "base:" << line << "\n";

        t << "end=chain\n";
    }

    return t;
}

void EffectsRack::loadStateText (const juce::String& text)
{
    auto lines = juce::StringArray::fromLines (text);
    int c = -1;
    juce::String chainText, baseText;

    const auto finishChain = [&]
    {
        if (c >= 0 && c < numChains)
        {
            chains[(size_t) c].chain.setModel (fx::ChainModel::fromText (chainText.toStdString()));
            chains[(size_t) c].baseline = fx::ChainModel::fromText (baseText.toStdString());
        }
        chainText.clear();
        baseText.clear();
    };

    for (auto line : lines)
    {
        line = line.trim();

        if (line.startsWith ("base:"))
        {
            baseText << line.substring (5) << "\n";
            continue;
        }

        const auto key = line.upToFirstOccurrenceOf ("=", false, false);
        const auto value = line.fromFirstOccurrenceOf ("=", false, false);

        if (key == "rack.on")       { enabled.store (value == "1"); continue; }
        if (key == "rack.tails")    { setTailsRingOut (value != "0"); continue; }

        if (key.startsWith ("pedal") && c < 0)
        {
            const auto parts = juce::StringArray::fromTokens (value, ",", "");
            if (parts.size() == 3)
                setPedalAssign (key.getTrailingIntValue() - 1, { parts[0].getIntValue(), parts[1].getIntValue(), parts[2].getIntValue() });
            continue;
        }
        if (key == "chain")         { finishChain(); c = value.getIntValue() - 1; continue; }
        if (key == "end")           { finishChain(); if (c >= 0 && c < numChains) pushRouting (c); c = -1; continue; }

        if (c < 0 || c >= numChains)
            continue;

        auto& r = chains[(size_t) c].route;

        if (key == "chain.name")            r.name = value;
        else if (key == "chain.on")         r.on = value == "1";
        else if (key == "chain.source")     r.source = juce::jlimit (0, 31, value.getIntValue() - 1);
        else if (key == "chain.out")        { r.out.pair = value.endsWith ("+"); r.out.firstReturn = juce::jlimit (0, 7, value.getIntValue() - 1); }
        else if (key == "chain.looper")     r.looperHears = value == "same" ? -2 : value == "none" ? -1 : juce::jlimit (0, 31, value.getIntValue() - 1);
        else if (key == "chain.listen")     r.listen = listenFromWord (value);
        else if (key == "chain.reference")  r.reference = juce::jlimit (-1, 31, value.getIntValue() - 1);
        else if (key == "chain.trim")       r.trimDb = juce::jlimit (-36.0f, 24.0f, value.getFloatValue());
        else if (key == "chain.recipe")     chains[(size_t) c].recipe = juce::StringArray::fromTokens (value, "|", "");
        else                                chainText << line << "\n";
    }

    finishChain();

    for (int i = 0; i < numChains; ++i)
        pushRouting (i);
}

void EffectsRack::saveState()
{
    auto f = stateFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (stateText());
}

//==============================================================================
double EffectsRack::captureSeconds() const noexcept
{
    return (double) capturedSamples.load() / rate;
}

juce::String EffectsRack::startCapture (int c, const juce::File& folder)
{
    stopCapture();

    if (! folder.createDirectory())
        return "Could not make the folder " + folder.getFullPathName();

    const auto& r = chains[(size_t) c].route;
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    auto file = folder.getNonexistentChildFile (r.name + " " + stamp, ".wav", false);
    juce::String error;
    auto made = wavWriter (file, rate, 3, error);

    if (made == nullptr)
        return error;

    file.withFileExtension ("txt").replaceWithText (
        "# GigRig effects capture. WAV channels: 1 = dry (input socket " + juce::String (r.source + 1) + "), "
        "2 = reference (input socket " + juce::String (r.reference + 1) + "), 3 = the app's effects.\n"
        "# Build " + juce::String (GIGRIG_BUILD_LABEL) + ", " + juce::String (rate) + " Hz. The chain while recording:\n"
        + juce::String (chains[(size_t) c].chain.model().toText()));

    capturedSamples.store (0);
    captureChain.store (c);
    auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (made.release(), writerThread, 1 << 17);

    {
        const juce::SpinLock::ScopedLockType hold (writerLock);
        writer = std::move (threaded);
    }

    lastCapture = file;
    capturing.store (true);
    return {};
}

void EffectsRack::stopCapture()
{
    capturing.store (false);
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;

    {
        const juce::SpinLock::ScopedLockType hold (writerLock);
        old = std::move (writer);
    }

    old.reset();    // writes out the rest and closes the file
}

//==============================================================================
juce::File EffectsRack::takeFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GigRig").getChildFile ("Test takes");
}

EffectsRack::ChainLoad EffectsRack::chainLoad (int c) const noexcept
{
    const auto& s = chains[(size_t) c];
    ChainLoad l;
    l.average = s.loadAverage.load();
    l.busiest = juce::jmax (s.loadPeak.load(), *std::max_element (s.loadRing.begin(), s.loadRing.end()));
    return l;
}

EffectsRack::TakeState EffectsRack::takeState() const noexcept
{
    const int mode = takeMode.load();
    if (mode == 1) return TakeState::recording;
    if (mode == 2) return TakeState::playing;
    return take != nullptr && take->length > 0 ? TakeState::ready : TakeState::none;
}

double EffectsRack::takeSeconds() const noexcept
{
    if (takeMode.load() == 1)
        return (double) takeRecorded.load() / rate;

    return take != nullptr ? (double) take->length / rate : 0.0;
}

bool EffectsRack::takeHolds (int c) const noexcept
{
    return take != nullptr && take->has[(size_t) c];
}

juce::String EffectsRack::startTake()
{
    if (! enabled.load())
        return "Tick 'Effects on' first.";

    auto fresh = std::make_unique<Take>();
    fresh->rate = rate;
    int count = 0;

    for (int c = 0; c < numChains; ++c)
        if (chains[(size_t) c].route.on)
        {
            fresh->has[(size_t) c] = true;
            ++count;
        }

    if (count == 0)
        return "Switch on the chain (or chains) to record first: a take records what goes into every chain that is on.";

    // Every chain gets the room (so the audio thread never has to check which), but only the
    // ones that are on are filled. A minute at 96 kHz is 23 MB a chain.
    const auto room = (size_t) (maxTakeSeconds * rate);
    for (int c = 0; c < numChains; ++c)
        fresh->data[(size_t) c].assign (fresh->has[(size_t) c] ? room : (size_t) 1, 0.0f);
    // The audio thread reads data[0].size() for the room left.
    if (! fresh->has[0])
        fresh->data[0].assign (room, 0.0f);

    takeMode.store (0);

    {
        const juce::SpinLock::ScopedLockType hold (takeLock);
        take.swap (fresh);
        takeRecorded.store (0);
        takePos.store (0);
    }

    takeLabel = "Take " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    takeMode.store (1);
    return {};
}

void EffectsRack::stopTake()
{
    const bool wasRecording = takeMode.exchange (0) == 1 || (take != nullptr && take->length == 0 && takeRecorded.load() > 0);

    if (! wasRecording || take == nullptr)
        return;

    // Wait out the block the audio thread may be in the middle of, then nobody else touches the take.
    { const juce::SpinLock::ScopedLockType hold (takeLock); }

    // Trim the silence before the first note and after the last (keeping a breath at each end).
    const juce::int64 recorded = takeRecorded.load();
    juce::int64 first = recorded, last = -1;

    for (int c = 0; c < numChains; ++c)
        if (take->has[(size_t) c])
        {
            const auto& d = take->data[(size_t) c];
            juce::int64 a = 0, b = recorded - 1;
            while (a < recorded && std::abs (d[(size_t) a]) < (float) playingLevel) ++a;
            while (b > a && std::abs (d[(size_t) b]) < (float) playingLevel) --b;
            if (a < recorded) { first = juce::jmin (first, a); last = juce::jmax (last, b); }
        }

    juce::int64 start = 0, length = 0;

    if (last >= first)
    {
        start = juce::jmax ((juce::int64) 0, first - (juce::int64) (0.1 * rate));
        length = juce::jmin (recorded, last + (juce::int64) (0.6 * rate)) - start;
    }

    const juce::SpinLock::ScopedLockType hold (takeLock);

    if (length < (juce::int64) (0.5 * rate))
    {
        take.reset();           // nothing was played
        takeLabel = {};
        takeRecorded.store (0);
    }
    else
    {
        take->start = start;
        take->length = length;
    }
}

juce::String EffectsRack::playTake()
{
    if (takeMode.load() == 1)
        stopTake();

    if (take == nullptr || take->length <= 0)
        return "There is no take yet: press Record a take, play, and press it again.";

    if (! enabled.load())
        return "Tick 'Effects on' first.";

    takePos.store (0);
    takeMode.store (2);
    return {};
}

juce::String EffectsRack::finishTake()
{
    stopTake();

    if (take == nullptr)
        return "Nothing was heard, so no take was kept. Check the chain's Input socket and its In meter.";

    const auto file = takeFolder().getNonexistentChildFile (takeLabel, ".wav", false);
    const auto error = saveTake (file);
    return error.isEmpty() ? "Take kept (" + juce::String (takeSeconds(), 1) + " s) and saved as " + file.getFileName() + "." : "Take kept, but not saved: " + error;
}

juce::String EffectsRack::takeStep()
{
    switch (takeState())
    {
        case TakeState::recording:
        {
            auto note = finishTake();
            if (take != nullptr)
                playTake();
            return note;
        }
        case TakeState::playing:
            stopTake();
            return "Take stopped. The chains hear their sockets again.";
        case TakeState::none:
        case TakeState::ready:
            break;
    }

    const auto error = startTake();
    return error.isEmpty() ? juce::String ("Recording a take: play (and sing), then press again.") : error;
}

juce::String EffectsRack::saveTake (const juce::File& file)
{
    if (take == nullptr || take->length <= 0 || takeMode.load() == 1)
        return "There is no take to save.";

    if (! file.getParentDirectory().createDirectory())
        return "Could not make the folder " + file.getParentDirectory().getFullPathName();

    file.deleteFile();
    juce::String error;
    auto made = wavWriter (file, take->rate, numChains, error);

    if (made == nullptr)
        return error;

    // Nothing writes to a take once it has been recorded, so it can be read here without the lock.
    juce::AudioBuffer<float> block (numChains, 8192);

    for (juce::int64 at = 0; at < take->length; at += block.getNumSamples())
    {
        const int count = (int) juce::jmin ((juce::int64) block.getNumSamples(), take->length - at);
        block.clear();
        for (int c = 0; c < numChains; ++c)
            if (take->has[(size_t) c])
                block.copyFrom (c, 0, take->data[(size_t) c].data() + take->start + at, count);

        if (! made->writeFromAudioSampleBuffer (block, 0, count))
            return "Could not write all of " + file.getFullPathName();
    }

    takeLabel = file.getFileNameWithoutExtension();
    return {};
}

juce::String EffectsRack::loadTake (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return "Could not read " + file.getFileName();

    if (std::abs (reader->sampleRate - rate) > 0.5)
        return file.getFileName() + " was recorded at " + juce::String (reader->sampleRate / 1000.0, 1) + " kHz and the app is running at "
               + juce::String (rate / 1000.0, 1) + " kHz, so it cannot be played now.";

    const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (maxTakeSeconds * rate));
    juce::AudioBuffer<float> b ((int) juce::jmax ((unsigned int) numChains, reader->numChannels), length);
    b.clear();
    reader->read (&b, 0, length, 0, true, true);

    auto fresh = std::make_unique<Take>();
    fresh->rate = rate;
    fresh->length = length;

    for (int c = 0; c < numChains; ++c)
    {
        // A file with fewer channels than chains (a recording made elsewhere) feeds the first chains.
        const bool there = c < (int) reader->numChannels && b.getMagnitude (c, 0, length) > 0.0f;
        fresh->has[(size_t) c] = there;
        if (there)
            fresh->data[(size_t) c].assign (b.getReadPointer (c), b.getReadPointer (c) + length);
        else
            fresh->data[(size_t) c].assign ((size_t) (c == 0 ? length : 1), 0.0f);
    }

    takeMode.store (0);

    {
        const juce::SpinLock::ScopedLockType hold (takeLock);
        take.swap (fresh);
        takeRecorded.store (length);
        takePos.store (0);
    }

    takeLabel = file.getFileNameWithoutExtension();
    return {};
}

juce::String EffectsRack::takeText() const
{
    const auto state = takeState();

    if (state == TakeState::none)
        return "Test take: none";

    juce::StringArray held;
    for (int c = 0; c < numChains; ++c)
        if (takeHolds (c))
            held.add (juce::String (c + 1));

    return "Test take \"" + takeLabel + "\": " + juce::String (takeSeconds(), 1) + " s of chain" + (held.size() == 1 ? " " : "s ") + held.joinIntoString (" and ")
           + (state == TakeState::recording ? ", recording now" : state == TakeState::playing ? ", playing round and round in place of the sockets" : ", not playing");
}

//==============================================================================
std::vector<float> EffectsRack::makeTestSound (double r)
{
    // Everything here is made from fixed seeds, so the same rate always gives the same sound.
    std::vector<float> x;
    x.reserve ((size_t) (200.0 * r));
    std::mt19937 random (20261123);
    std::uniform_real_distribution<double> u (0.0, 1.0);

    const auto silence = [&] (double seconds) { x.insert (x.end(), (size_t) (seconds * r), 0.0f); };

    // 1. Silence, then three clicks one second apart, so the recording can be lined up to the sample.
    silence (0.5);
    for (int k = 0; k < 3; ++k)
    {
        x.push_back (1.0f);
        x.push_back (-1.0f);
        silence (1.0);
    }

    // 2. Slow sweeps, 30 Hz to 18 kHz, at five levels.
    for (double db : { -36.0, -24.0, -15.0, -8.0, -2.0 })
    {
        const double g = std::pow (10.0, db / 20.0), seconds = 5.0;
        double phase = 0.0;
        const auto n = (size_t) (seconds * r);
        for (size_t i = 0; i < n; ++i)
        {
            const double hz = 30.0 * std::pow (600.0, (double) i / (double) n);
            phase += twoPi * hz / r;
            const double fade = std::min ({ 1.0, (double) i / (0.02 * r), (double) (n - i) / (0.02 * r) });
            x.push_back ((float) (g * fade * std::sin (phase)));
        }
        silence (0.3);
    }

    // 3. Plucked notes and chords, the shape of real guitar playing, at every level.
    const auto pluckSection = [&] (double seconds, double minDb, double maxDb)
    {
        const auto start = x.size();
        x.insert (x.end(), (size_t) (seconds * r), 0.0f);
        double t = 0.0;

        while (t < seconds - 1.0)
        {
            const int notes = 1 + (int) (u (random) * 5.0);
            const double db = minDb + (maxDb - minDb) * u (random);
            const double g = std::pow (10.0, db / 20.0) / std::sqrt ((double) notes);
            const double length = 0.15 + 1.6 * u (random);

            for (int k = 0; k < notes; ++k)
            {
                const double hz = 82.4 * std::pow (2.0, (std::floor (u (random) * 36.0)) / 12.0);
                const double strum = k * 0.008 * u (random);
                const double decay = 1.5 + 4.0 * u (random);
                const auto from = start + (size_t) ((t + strum) * r);
                const auto len = (size_t) (std::min (length + 0.6, seconds - t - strum) * r);

                for (int h = 1; h <= 14 && h * hz < 9000.0; ++h)
                {
                    const double a = g * std::abs (std::sin (3.14159265 * h * (0.12 + 0.1 * u (random)))) / h;
                    const double w = twoPi * h * hz / r;
                    for (size_t i = 0; i < len && from + i < x.size(); ++i)
                    {
                        const double tt = (double) i / r;
                        x[from + i] += (float) (a * std::exp (-tt * decay * (1.0 + 0.4 * h)) * std::sin (w * (double) i));
                    }
                }
            }

            t += length;
        }
    };

    pluckSection (30.0, -30.0, -12.0);
    pluckSection (30.0, -18.0, -3.0);

    // 4. Noise shaped like a guitar (most of it below 3 kHz), swelling and dying away, at many levels.
    {
        const auto n = (size_t) (40.0 * r);
        double lp = 0.0, hp = 0.0, prev = 0.0;
        const double a = 1.0 - std::exp (-twoPi * 3000.0 / r), b = std::exp (-twoPi * 60.0 / r);
        std::normal_distribution<double> gauss (0.0, 1.0);

        for (size_t i = 0; i < n; ++i)
        {
            lp += a * (gauss (random) - lp);
            hp = b * (hp + lp - prev);
            prev = lp;
            const double tt = (double) i / r;
            const double swell = 0.5 - 0.5 * std::cos (twoPi * tt / (0.7 + 0.3 * std::sin (tt * 0.37)));
            const double db = -40.0 + 37.0 * (0.5 + 0.5 * std::sin (twoPi * tt / 13.0));
            x.push_back ((float) (std::pow (10.0, db / 20.0) * swell * hp * 0.6));
        }
        silence (0.5);
    }

    // 5. The same plucked section again, for checking a model on playing it never learned from.
    pluckSection (20.0, -24.0, -3.0);
    silence (1.0);

    // Loudest moment just under full scale (the capture then scales it to the level chosen).
    float top = 0.0f;
    for (auto v : x) top = juce::jmax (top, std::abs (v));
    if (top > 0.0f)
        for (auto& v : x) v *= 0.98f / top;

    return x;
}

juce::String EffectsRack::startAmpCapture (const AmpCaptureSetup& setup, const juce::File& folder, double latencyMs, const juce::String& latencySource)
{
    stopAmpCapture();

    if (! folder.createDirectory())
        return "Could not make the folder " + folder.getFullPathName();

    std::vector<float> sound;
    juce::String soundName = "the app's own test sound";

    if (setup.soundFile.existsAsFile())
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (setup.soundFile));

        if (reader == nullptr || reader->lengthInSamples <= 0)
            return "Could not read " + setup.soundFile.getFileName();

        juce::AudioBuffer<float> b (1, (int) reader->lengthInSamples);
        reader->read (&b, 0, b.getNumSamples(), 0, true, false);
        const double ratio = reader->sampleRate / rate;

        if (std::abs (ratio - 1.0) < 1.0e-6)
            sound.assign (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
        else
        {
            sound.assign ((size_t) (b.getNumSamples() / ratio), 0.0f);
            juce::LagrangeInterpolator li;
            li.process (ratio, b.getReadPointer (0), sound.data(), (int) sound.size());
        }

        soundName = setup.soundFile.getFileName();
    }
    else
    {
        sound = makeTestSound (rate);
    }

    // Scale so the loudest moment of the sound sits at the chosen peak.
    float top = 0.0f;
    for (auto v : sound) top = juce::jmax (top, std::abs (v));
    const float gain = top > 0.0f ? juce::Decibels::decibelsToGain (setup.levelDb) / top : 0.0f;

    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    const auto base = (setup.name.trim().isNotEmpty() ? juce::File::createLegalFileName (setup.name.trim()) : juce::String ("Amp capture")) + " " + stamp;
    auto file = folder.getNonexistentChildFile (base, ".wav", false);
    juce::String error;
    auto made = wavWriter (file, rate, 2, error);

    if (made == nullptr)
        return error;

    ampSetupText = "# GigRig amp capture. WAV channel 1 = the test sound as sent, channel 2 = what came back.\n"
                   "name=" + setup.name + "\n"
                   "build=" + juce::String (GIGRIG_BUILD_LABEL) + "\n"
                   "rate=" + juce::String (rate) + "\n"
                   "sound=" + soundName + "\n"
                   "out=return " + juce::String (setup.out.firstReturn + 1) + (setup.out.pair ? " and " + juce::String (setup.out.firstReturn + 2) : juce::String()) + "\n"
                   "in=input socket " + juce::String (setup.input + 1) + "\n"
                   "peak_dbfs=" + juce::String (setup.levelDb, 1) + "\n"
                   "round_trip_ms=" + juce::String (latencyMs, 2) + " (" + latencySource + ")\n";
    file.withFileExtension ("txt").replaceWithText (ampSetupText);

    ampGain.store (gain);
    ampOutFirst.store (setup.out.firstReturn);
    ampOutPair.store (setup.out.pair);
    ampInput.store (setup.input);
    ampPosition.store (0);
    ampDone.store (false);

    auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (made.release(), writerThread, 1 << 18);

    {
        const juce::SpinLock::ScopedLockType hold (ampWriterLock);
        ampWriter = std::move (threaded);
        testSound = std::move (sound);
    }

    lastAmpCapture = file;
    ampPlaying.store (true);
    return {};
}

void EffectsRack::stopAmpCapture()
{
    ampPlaying.store (false);
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;

    {
        const juce::SpinLock::ScopedLockType hold (ampWriterLock);
        old = std::move (ampWriter);
    }

    old.reset();
}

double EffectsRack::ampCaptureProgress() const noexcept
{
    return testSound.empty() ? 0.0 : juce::jlimit (0.0, 1.0, (double) ampPosition.load() / (double) testSound.size());
}

double EffectsRack::ampCaptureSeconds() const noexcept
{
    return (double) ampPosition.load() / rate;
}

//==============================================================================
void EffectsRack::prepare (double sampleRate, int maxBlock, int numIns)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    blockCapacity = juce::jmax (16, maxBlock);
    envCoeff = 1.0 - std::exp (-1.0 / (0.05 * rate));
    silence.assign ((size_t) blockCapacity, 0.0f);
    returnMix.assign ((size_t) blockCapacity * 8, 0.0f);
    feed.assign ((size_t) juce::jmax (numIns, 1), nullptr);

    for (auto& s : chains)
    {
        s.chain.prepare (rate, blockCapacity);
        s.wet.assign ((size_t) blockCapacity, 0.0f);
        s.heard.assign ((size_t) blockCapacity, 0.0f);
        s.takeDry.assign ((size_t) blockCapacity, 0.0f);
        s.trimNow = s.trim.load();
        s.hot = 0;
        s.wasOn = false;
    }

    // A take made at another rate would play at the wrong speed: it is dropped (audio is stopped here).
    takeMode.store (0);
    if (take != nullptr && std::abs (take->rate - rate) > 0.5)
    {
        take.reset();
        takeLabel = {};
    }
}

void EffectsRack::bypassBlock (const float* const* inputs, int numIns, int) noexcept
{
    for (int i = 0; i < juce::jmin (numIns, (int) feed.size()); ++i)
        feed[(size_t) i] = inputs[i];

    running = false;
}

void EffectsRack::process (const float* const* inputs, int numIns, int n) noexcept
{
    numIns = juce::jmin (numIns, (int) feed.size());

    for (int i = 0; i < numIns; ++i)
        feed[(size_t) i] = inputs[i];

    running = false;

    if (n <= 0 || n > blockCapacity)
        return;

    for (int i = 0; i < juce::jmin (numIns, 16); ++i)
        if (inputs[i] != nullptr)
            raise (socketMax[(size_t) i], peakOf (inputs[i], n));

    const auto start = juce::Time::getHighResolutionTicks();
    const bool rackOn = enabled.load();
    const bool amp = ampPlaying.load();
    returnUsed.fill (false);

    const auto pick = [&] (int index) -> const float*
    {
        return index >= 0 && index < numIns && inputs[index] != nullptr ? inputs[index] : silence.data();
    };

    const auto addToReturns = [&] (int first, bool pair, const float* data)
    {
        for (int r = first; r <= (pair ? first + 1 : first); ++r)
        {
            if (r < 0 || r >= 8)
                continue;

            auto* mixPtr = returnMix.data() + (size_t) r * (size_t) blockCapacity;

            if (! returnUsed[(size_t) r])
            {
                juce::FloatVectorOperations::copy (mixPtr, data, n);
                returnUsed[(size_t) r] = true;
            }
            else
                juce::FloatVectorOperations::add (mixPtr, data, n);
        }
    };

    // ---- the amp capture's test sound: out on its returns, and recorded with what comes back ----
    if (amp)
    {
        // The lock is only ever held by the message thread for a moment while it swaps the
        // sound or the file; if it is busy, this block simply skips the test sound.
        const juce::SpinLock::ScopedTryLockType hold (ampWriterLock);

        if (hold.isLocked())
        {
            auto pos = ampPosition.load();
            const auto total = (juce::int64) testSound.size();
            auto& out = chains[0].wet;   // borrowed as scratch: chain 0 refills it below
            const float g = ampGain.load();

            for (int i = 0; i < n; ++i, ++pos)
                out[(size_t) i] = pos < total ? testSound[(size_t) pos] * g : 0.0f;

            const float* back = pick (ampInput.load());
            raise (ampReturnPeak, peakOf (back, n));
            addToReturns (ampOutFirst.load(), ampOutPair.load(), out.data());

            if (ampWriter != nullptr)
            {
                const float* chans[2] { out.data(), back };
                ampWriter->write (chans, n);
            }

            // A little past the end, so the amp's own tail is in the recording too.
            ampPosition.store (pos);
            if (pos >= total + (juce::int64) (0.5 * rate))
                ampDone.store (true);
        }
    }

    if (rackOn)
    {
        const float fadeStep = 1.0f / (float) juce::jmax (1.0, 0.010 * rate);

        // The test take. The lock is only held by the message thread for a moment while it
        // swaps takes; if it is busy this block carries on live.
        const juce::SpinLock::ScopedTryLockType takeHold (takeLock);
        Take* const tk = takeHold.isLocked() ? take.get() : nullptr;
        const int takeNow = tk != nullptr ? takeMode.load() : 0;
        const juce::int64 takeFade = (juce::int64) (0.005 * rate);
        const bool takeRec = takeNow == 1, takePlay = takeNow == 2 && tk->length > takeFade * 4;
        const juce::int64 recAt = takeRecorded.load(), playAt = takePos.load();
        const juce::int64 recRoom = takeRec ? (juce::int64) tk->data[0].size() - recAt : 0;

        for (int c = 0; c < numChains; ++c)
        {
            auto& s = chains[(size_t) c];

            if (! s.on.load())
            {
                s.wasOn = false;
                continue;
            }

            const float* dry = pick (s.source.load());
            const float* ref = pick (s.reference.load());
            const auto chainStart = juce::Time::getHighResolutionTicks();

            if (takeRec && tk->has[(size_t) c] && recRoom > 0)
            {
                std::copy (dry, dry + juce::jmin ((juce::int64) n, recRoom), tk->data[(size_t) c].begin() + recAt);
            }
            else if (takePlay && tk->has[(size_t) c])
            {
                // This chain's part of the take in place of its socket, eased in and out at the ends of the loop.
                const float* from = tk->data[(size_t) c].data() + tk->start;
                juce::int64 p = playAt;
                for (int i = 0; i < n; ++i)
                {
                    const juce::int64 edge = juce::jmin (p, tk->length - 1 - p);
                    s.takeDry[(size_t) i] = from[p] * (edge >= takeFade ? 1.0f : (float) edge / (float) takeFade);
                    if (++p >= tk->length) p = 0;
                }
                dry = s.takeDry.data();
            }

            raise (s.inPeak, peakOf (dry, n));
            raise (s.refPeak, peakOf (ref, n));

            // The effects.
            juce::FloatVectorOperations::copy (s.wet.data(), dry, n);
            s.chain.process (s.wet.data(), n);

            // The chain's level, gliding.
            const float trimTarget = s.trim.load();
            const float trimStep = (trimTarget - s.trimNow) / (float) n;

            // What is heard: effects, dry or the reference, faded across in 10 ms.
            const auto mode = (Listen) s.listen.load();
            const float te = mode == Listen::effects ? 1.0f : 0.0f, td = mode == Listen::dry ? 1.0f : 0.0f, tr = mode == Listen::reference ? 1.0f : 0.0f;

            if (! s.wasOn)
            {
                s.mixEffects = te; s.mixDry = td; s.mixRef = tr;
                s.trimNow = trimTarget;
                s.wasOn = true;
            }

            const auto glide = [fadeStep] (float& v, float target)
            {
                v = target > v ? juce::jmin (target, v + fadeStep) : juce::jmax (target, v - fadeStep);
            };

            double effSq = 0.0, cmpSq = 0.0;
            juce::int64 played = 0;
            const bool compareRef = s.reference.load() >= 0;

            for (int i = 0; i < n; ++i)
            {
                s.trimNow += trimStep;
                const float e = s.wet[(size_t) i] * s.trimNow;
                s.wet[(size_t) i] = e;
                glide (s.mixEffects, te); glide (s.mixDry, td); glide (s.mixRef, tr);
                s.heard[(size_t) i] = e * s.mixEffects + dry[i] * s.mixDry + ref[i] * s.mixRef;

                s.env += envCoeff * (std::abs (dry[i]) - s.env);
                if (s.env > playingLevel)
                {
                    const float k = compareRef ? ref[i] : dry[i];
                    effSq += (double) e * e;
                    cmpSq += (double) k * k;
                    ++played;
                }
            }
            s.trimNow = trimTarget;

            if (played > 0)
            {
                addTo (s.effSum, effSq);
                addTo (s.cmpSum, cmpSq);
                s.played.fetch_add (played);
            }

            const float outPeak = peakOf (s.heard.data(), n);
            raise (s.outPeak, outPeak);

            // Stuck at full level for a quarter of a second is a howl round the mixer, not music.
            s.hot = outPeak >= 0.99f ? s.hot + n : 0;
            if (s.hot > rate * 0.25)
            {
                s.on.store (false);
                s.guard.store (true);
                s.hot = 0;
                continue;
            }

            // While the amp capture uses these returns, the chain stays quiet on them.
            const int first = s.outFirst.load();
            const bool pair = s.outPair.load();
            const bool clash = amp && (first == ampOutFirst.load() || (pair && first + 1 == ampOutFirst.load())
                                       || (ampOutPair.load() && first == ampOutFirst.load() + 1));
            if (! clash)
                addToReturns (first, pair, s.heard.data());

            // The looper records what is heard, in place of a socket.
            const int hears = s.looperHears.load();
            const int socket = hears == -2 ? s.source.load() : hears;
            if (socket >= 0 && socket < numIns)
                feed[(size_t) socket] = s.heard.data();

            // Capture.
            if (capturing.load() && captureChain.load() == c && capturedSamples.load() < (juce::int64) (maxCaptureSeconds * rate))
            {
                const juce::SpinLock::ScopedTryLockType hold (writerLock);

                if (hold.isLocked() && writer != nullptr)
                {
                    const float* chans[3] { dry, ref, s.wet.data() };
                    if (writer->write (chans, n))
                        capturedSamples.fetch_add (n);
                }
            }

            // How much of the block this chain took (its effects and the little around them).
            const auto chainLoadNow = (float) (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - chainStart) * rate / (double) n);
            s.loadAverage.store (s.loadAverage.load (std::memory_order_relaxed) * 0.999f + chainLoadNow * 0.001f, std::memory_order_relaxed);
            raise (s.loadPeak, chainLoadNow);
        }

        if (takeRec)
        {
            takeRecorded.store (recAt + juce::jmin ((juce::int64) n, recRoom));
            if (recRoom <= n)
                takeMode.store (0);     // full: the message thread finishes it off on its next tick
        }
        else if (takePlay)
        {
            takePos.store ((playAt + n) % tk->length);
        }
    }

    running = true;

    const double period = (double) n / rate;
    const double load = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start) / period;
    if (load > peakLoad.load (std::memory_order_relaxed))
        peakLoad.store (load, std::memory_order_relaxed);
}

//==============================================================================
void EffectsRack::tap (double ms)
{
    const juce::SpinLock::ScopedLockType sl (tapLock);
    const double gap = ms - lastTapMs;

    if (gap < 200.0)                        // a switch bounce: not a tap
        return;

    lastTapMs = ms;

    if (gap > 2000.0)                       // a fresh start
    {
        tapGaps.clear();
        return;
    }

    tapGaps.push_back (gap);
    if (tapGaps.size() > 3)
        tapGaps.erase (tapGaps.begin());

    double sum = 0.0;
    for (auto g : tapGaps)
        sum += g;
    tappedBpm.store (60000.0 / (sum / (double) tapGaps.size()));
}

juce::String EffectsRack::tempoText() const
{
    const double bpm = tempoNow.load();
    if (bpm <= 0.0)
        return "not known yet (no loop and no taps), so synced delays use their own Time";
    return juce::String (bpm, 1) + " bpm, " + (tempoFromLoop.load() ? "from the looper" : "tapped");
}

juce::String EffectsRack::reportText() const
{
    juce::String t;
    t << "--- Effects ---\n";
    t << "Effects tab: " << (enabled.load() ? "ON" : "off") << "\n";
    t << "Tempo for synced delays: " << tempoText() << "\n";
    t << "When a chain's effects change: delays and reverbs " << (tails ? "ring out" : "are cut in 10 ms")
      << (tailsRinging() > 0 ? " (" + juce::String (tailsRinging()) + " ringing out now)" : juce::String()) << "\n";

    for (int c = 0; c < numChains; ++c)
    {
        const auto& s = chains[(size_t) c];
        const auto& r = s.route;

        if (! r.on && s.chain.model().slots.empty())
            continue;

        t << "Chain " << (c + 1) << " \"" << r.name << "\": " << (r.on ? "on" : "off")
          << ", socket " << (r.source + 1) << " -> return " << (r.out.firstReturn + 1) << (r.out.pair ? "+" + juce::String (r.out.firstReturn + 2) : juce::String())
          << ", looper hears it as " << (r.looperHears == -1 ? juce::String ("nothing") : "socket " + juce::String ((r.looperHears == -2 ? r.source : r.looperHears) + 1))
          << ", listening to " << listenWord (r.listen) << (r.reference >= 0 ? " (reference socket " + juce::String (r.reference + 1) + ")" : juce::String())
          << ", chain level " << juce::String (r.trimDb, 1) << " dB"
          << ", delay " << juce::String (s.chain.latencySamples() * 1000.0 / rate, 2) << " ms"
          << (s.guard.load() ? ", FEEDBACK GUARD switched it off" : "") << "\n";

        if (r.on && enabled.load())
        {
            const auto load = chainLoad (c);
            t << "  Processor: this chain uses " << juce::String (load.average * 100.0, 1) << "% of each block on average, "
              << juce::String (load.busiest * 100.0, 1) << "% at its busiest in the last 10 seconds\n";
        }

        const auto& m = s.chain.model();

        for (size_t i = 0; i < m.slots.size(); ++i)
        {
            const auto& slot = m.slots[i];
            const auto* type = fx::findEffectType (slot.type);
            t << "  " << (i + 1) << ". " << (type != nullptr ? type->name : slot.type) << (slot.on ? "" : " (off)") << ":";

            if (auto* e = s.chain.effect ((int) i))
            {
                for (int p = 0; p < (int) e->params().size(); ++p)
                    t << " " << e->params()[(size_t) p].id << "=" << juce::String (e->getParam (p), 2);

                if (e->status().size() > 0)
                    t << "  [" << e->status() << "]";
            }

            t << "\n";
        }
    }

    t << pedalText();
    t << takeText() << "\n";

    if (panelNote.isNotEmpty())
        t << "Effects tab last said: " << panelNote << "\n";

    if (lastCapture != juce::File())
        t << "Last capture: " << lastCapture.getFullPathName() << "\n";

    if (lastAmpCapture != juce::File())
        t << "Last amp capture: " << lastAmpCapture.getFullPathName() << (ampPlaying.load() ? " (recording now)" : "") << "\n";

    return t;
}

} // namespace gigrig
