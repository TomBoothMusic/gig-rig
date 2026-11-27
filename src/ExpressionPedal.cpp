#include "ExpressionPedal.h"

namespace gigrig
{

namespace
{
    constexpr int endSlack = 2;             // controller steps at each end that count as the end
    constexpr int fewestValues = 8;         // a real pedal sweeps through many values; a switch sends two
    constexpr int smallestSweep = 40;       // and covers a good part of the range
}

ExpressionInput::ExpressionInput() = default;

juce::File ExpressionInput::stateFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GigRig").getChildFile ("expression-pedal.txt");
}

float ExpressionInput::positionFor (const Setup& s, int value) noexcept
{
    if (s.heel == s.toe)
        return 0.0f;

    const bool backwards = s.heel > s.toe;
    const int low = juce::jmin (s.heel, s.toe) + endSlack, high = juce::jmax (s.heel, s.toe) - endSlack;

    if (high <= low)
        return (value >= (s.heel + s.toe) / 2) != backwards ? 100.0f : 0.0f;

    const float along = juce::jlimit (0.0f, 1.0f, (float) (value - low) / (float) (high - low));
    return 100.0f * (backwards ? 1.0f - along : along);
}

ExpressionInput::Setup ExpressionInput::setup() const
{
    const juce::ScopedLock l (lock);
    return now;
}

void ExpressionInput::setSetup (const Setup& s)
{
    const juce::ScopedLock l (lock);
    now = s;
    now.cc = juce::jlimit (-1, 127, s.cc);
    now.channel = juce::jlimit (0, 16, s.channel);
    now.heel = juce::jlimit (0, 127, s.heel);
    now.toe = juce::jlimit (0, 127, s.toe);
}

void ExpressionInput::handleMidi (const juce::String& deviceName, const juce::MidiMessage& message)
{
    if (! message.isController())
        return;

    const int cc = message.getControllerNumber(), value = message.getControllerValue();

    {
        const juce::ScopedLock l (lock);

        if (learning.load())
        {
            auto& s = seen[(size_t) cc];
            s.low = juce::jmin (s.low, value);
            s.high = juce::jmax (s.high, value);
            s.values[(size_t) value] = true;
            s.channel = message.getChannel();
            s.device = deviceName;
            ++s.messages;
            return;
        }

        if (cc != now.cc || (now.channel != 0 && message.getChannel() != now.channel) || (now.device.isNotEmpty() && deviceName != now.device))
            return;
    }

    latest.store (value);
    count.fetch_add (1);

    if (! pending.exchange (true) && selfDeliver)
        juce::MessageManager::callAsync ([this, stillHere = std::weak_ptr<int> (alive)]
        {
            if (! stillHere.expired())
                deliver();
        });
}

void ExpressionInput::deliver()
{
    if (! pending.exchange (false))
        return;

    const auto position = positionFor (setup(), latest.load());
    lastPosition.store (position);

    if (onMove != nullptr)
        onMove (position);
}

void ExpressionInput::startLearning()
{
    const juce::ScopedLock l (lock);

    for (auto& s : seen)
        s = {};

    learning.store (true);
}

juce::String ExpressionInput::learningLine() const
{
    const juce::ScopedLock l (lock);
    int best = -1;

    for (int cc = 0; cc < 128; ++cc)
        if (seen[(size_t) cc].messages > 0 && (best < 0 || seen[(size_t) cc].high - seen[(size_t) cc].low > seen[(size_t) best].high - seen[(size_t) best].low))
            best = cc;

    if (best < 0)
        return "Listening: rock the pedal all the way back and all the way forward. Nothing has arrived yet.";

    const auto& s = seen[(size_t) best];
    return "Listening: controller " + juce::String (best) + " has gone from " + juce::String (s.low) + " to " + juce::String (s.high) + ". Rock it fully both ways, then press Done.";
}

juce::String ExpressionInput::finishLearning()
{
    const juce::ScopedLock l (lock);
    learning.store (false);
    int best = -1, switches = 0, anything = 0;

    for (int cc = 0; cc < 128; ++cc)
    {
        const auto& s = seen[(size_t) cc];

        if (s.messages == 0)
            continue;

        ++anything;
        int different = 0;

        for (bool v : s.values)
            different += v ? 1 : 0;

        if (different < fewestValues || s.high - s.low < smallestSweep)
        {
            ++switches;
            continue;
        }

        if (best < 0 || s.high - s.low > seen[(size_t) best].high - seen[(size_t) best].low)
            best = cc;
    }

    if (best < 0)
        return lastLearned = anything == 0 ? "Nothing arrived, so nothing was changed. Check the pedal is plugged in and its device shows on the Device check tab."
                           : switches > 0 ? "Only footswitches moved (a switch sends two values, a pedal sweeps through many), so nothing was changed."
                                          : juce::String ("Nothing swept far enough to be a pedal, so nothing was changed.");

    const auto& s = seen[(size_t) best];
    now.cc = best;
    now.channel = s.channel;
    now.device = s.device;
    now.heel = s.low;
    now.toe = s.high;
    return lastLearned = "Learned: controller " + juce::String (best) + " on channel " + juce::String (s.channel) + " from '" + s.device
                       + "', heel at " + juce::String (s.low) + " and toe at " + juce::String (s.high) + ". If it works backwards, press Swap heel and toe.";
}

juce::String ExpressionInput::stateText() const
{
    const auto s = setup();
    juce::String t ("# GigRig expression pedal: which MIDI controller is the pedal. Safe to delete (press Learn again).\n");
    t << "[expression pedal]\n";
    t << "cc=" << s.cc << "\n";
    t << "channel=" << s.channel << "\n";
    t << "device=" << s.device << "\n";
    t << "heel=" << s.heel << "\n";
    t << "toe=" << s.toe << "\n";
    return t;
}

void ExpressionInput::loadStateText (const juce::String& text)
{
    Setup s;

    for (const auto& raw : juce::StringArray::fromLines (text))
    {
        const auto line = raw.trim();
        const auto key = line.upToFirstOccurrenceOf ("=", false, false).trim(), value = line.fromFirstOccurrenceOf ("=", false, false).trim();

        if (line.startsWithChar ('#') || ! line.containsChar ('='))
            continue;

        if (key == "cc")            s.cc = value.getIntValue();
        else if (key == "channel")  s.channel = value.getIntValue();
        else if (key == "device")   s.device = value;
        else if (key == "heel")     s.heel = value.getIntValue();
        else if (key == "toe")      s.toe = value.getIntValue();
    }

    setSetup (s);
}

void ExpressionInput::saveState() const
{
    if (! memoryOnDisk.load())
        return;

    auto f = stateFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (stateText());
}

juce::String ExpressionInput::statusLine() const
{
    if (learning.load())
        return learningLine();

    const auto s = setup();

    if (s.cc < 0)
        return "No expression pedal is set up. Plug one in, press Learn pedal and rock it.";

    juce::String t ("Controller " + juce::String (s.cc) + (s.channel > 0 ? ", channel " + juce::String (s.channel) : juce::String())
                    + (s.device.isNotEmpty() ? " from '" + s.device + "'" : juce::String()) + ", heel " + juce::String (s.heel) + ", toe " + juce::String (s.toe) + ". ");
    const auto p = position();
    t << (p < 0.0f ? juce::String ("Not moved yet.") : "Now at " + juce::String (juce::roundToInt (p)) + " (0 heel, 100 toe), " + juce::String (messages()) + " moves seen.");
    return t;
}

juce::String ExpressionInput::reportText() const
{
    juce::String t ("Expression pedal\n  " + statusLine() + "\n");

    if (lastLearned.isNotEmpty())
        t << "  Last Learn: " << lastLearned << "\n";

    return t;
}

} // namespace gigrig
