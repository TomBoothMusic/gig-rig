#include "DeviceReport.h"

namespace gigrig
{

namespace
{
    // The CQ12T shows 16 channels each way over USB.
    constexpr int expectedCQChannels = 16;

    /** True if `word` appears in `text` with no letter or digit stuck on the front
        and no letter straight after, so "CQ12T" and "A&H CQ" match, but a word
        merely containing "cq" doesn't, and nor does the monitor "MP341CQ". */
    bool containsWordStart (const juce::String& text, const juce::String& word)
    {
        const auto lower = text.toLowerCase();
        const auto needle = word.toLowerCase();

        for (int from = 0;;)
        {
            const auto at = lower.indexOf (from, needle);

            if (at < 0)
                return false;

            if (at == 0 || ! juce::CharacterFunctions::isLetterOrDigit (lower[at - 1]))
            {
                const auto after = lower[at + needle.length()];

                // "cq" then a digit, space, dash or the end; never another letter
                if (after == 0 || ! juce::CharacterFunctions::isLetter (after))
                    return true;
            }

            from = at + 1;
        }
    }

    juce::String joinNumbered (const juce::StringArray& names)
    {
        juce::StringArray parts;

        for (int i = 0; i < names.size(); ++i)
            parts.add (juce::String (i + 1) + ": " + names[i]);

        return parts.joinIntoString (", ");
    }

    template <typename Number>
    juce::String joinNumbers (const juce::Array<Number>& values)
    {
        juce::StringArray parts;

        for (auto v : values)
            parts.add (juce::String (v));

        return parts.joinIntoString (", ");
    }

    AudioDeviceEntry describeDevice (juce::AudioIODeviceType& type,
                                     const juce::String& outputName,
                                     const juce::String& inputName)
    {
        AudioDeviceEntry entry;
        entry.typeName = type.getTypeName();
        entry.name = outputName.isNotEmpty() ? outputName : inputName;

        std::unique_ptr<juce::AudioIODevice> device (type.createDevice (outputName, inputName));

        if (device == nullptr)
        {
            entry.problem = "Listed by the driver, but it would not open. "
                            "It may be unplugged, switched off or in use by another program.";
            return entry;
        }

        entry.inputChannels  = device->getInputChannelNames();
        entry.outputChannels = device->getOutputChannelNames();
        entry.sampleRates    = device->getAvailableSampleRates();
        entry.bufferSizes    = device->getAvailableBufferSizes();

        if (entry.inputChannels.isEmpty() && entry.outputChannels.isEmpty())
        {
            const auto error = device->getLastError();
            entry.problem = "Opened, but reported no channels."
                            + (error.isNotEmpty() ? " Driver said: " + error : juce::String());
        }

        return entry;
    }

    juce::String stateTag (Verdict::State state)
    {
        switch (state)
        {
            case Verdict::State::found:   return "[FOUND]    ";
            case Verdict::State::partial: return "[PARTLY]   ";
            case Verdict::State::missing: return "[NOT FOUND]";
        }

        return {};
    }
}

bool looksLikeCQ (const juce::String& deviceName)
{
    const auto lower = deviceName.toLowerCase();

    return containsWordStart (deviceName, "cq")
        || (lower.contains ("allen") && lower.contains ("heath"));
}

bool looksLikeRC600 (const juce::String& deviceName)
{
    const auto lower = deviceName.toLowerCase();
    return lower.contains ("rc-600") || lower.contains ("rc600");
}

void judge (DeviceReport& report)
{
    // ---- Mixer: best channel count any one driver type shows for the CQ ----
    struct PerType { juce::String type; int ins = 0, outs = 0; };
    juce::Array<PerType> perType;

    for (const auto& device : report.audioDevices)
    {
        if (! looksLikeCQ (device.name))
            continue;

        PerType* slot = nullptr;

        for (auto& p : perType)
            if (p.type == device.typeName)
                slot = &p;

        if (slot == nullptr)
        {
            perType.add ({ device.typeName, 0, 0 });
            slot = &perType.getReference (perType.size() - 1);
        }

        slot->ins  = juce::jmax (slot->ins,  device.inputChannels.size());
        slot->outs = juce::jmax (slot->outs, device.outputChannels.size());
    }

    const auto score = [] (const PerType& p)
    {
        // Most usable channels first; ASIO wins a tie because it is the low-latency route.
        return juce::jmin (p.ins, p.outs) * 2 + (p.type == "ASIO" ? 1 : 0);
    };

    const PerType* best = nullptr;

    for (const auto& p : perType)
        if (best == nullptr || score (p) > score (*best))
            best = &p;

    if (best == nullptr)
    {
        report.mixer = { Verdict::State::missing,
                         "CQ mixer: not found",
                         "Check the USB cable runs from the CQ's USB-B socket to this computer, the mixer is "
                         "switched on, and (on Windows) the Allen & Heath CQ driver is installed. Then press Scan again." };
    }
    else if (best->ins >= expectedCQChannels && best->outs >= expectedCQChannels)
    {
        report.mixer = { Verdict::State::found,
                         "CQ mixer: found, " + juce::String (best->ins) + " inputs and "
                             + juce::String (best->outs) + " outputs through " + best->type,
                         best->type == "ASIO" || best->type == "ALSA" || best->type == "JACK"
                             ? "This is the low-latency route the rig will use."
                             : "All channels are visible, but not through ASIO. The Allen & Heath driver "
                               "should add an ASIO entry; it is worth checking it installed properly." };
    }
    else
    {
        report.mixer = { Verdict::State::partial,
                         "CQ mixer: found, but only " + juce::String (best->ins) + " inputs and "
                             + juce::String (best->outs) + " outputs through " + best->type,
                         "The rig needs all " + juce::String (expectedCQChannels) + " each way. On Windows this "
                         "usually means the Allen & Heath CQ driver is not installed yet (no ASIO entry in the list below)." };
    }

    // ---- Looper: the RC-600 as a MIDI input (its switches) ------------------
    juce::String inName, outName;

    for (const auto& d : report.midiInputs)
        if (inName.isEmpty() && looksLikeRC600 (d.name))
            inName = d.name;

    for (const auto& d : report.midiOutputs)
        if (outName.isEmpty() && looksLikeRC600 (d.name))
            outName = d.name;

    if (inName.isNotEmpty())
    {
        report.looper = { Verdict::State::found,
                          "RC-600: found as a MIDI device (\"" + inName + "\")",
                          "Select the MIDI CTRL memory (slot 65) on the RC-600, then press a switch: "
                          "each press shows in the live MIDI monitor as CC 20 to 30." };
    }
    else if (outName.isNotEmpty())
    {
        report.looper = { Verdict::State::partial,
                          "RC-600: found for sending MIDI to it, but not for receiving from it",
                          "The rig needs to receive from the RC-600. Try another USB socket, then press Scan again." };
    }
    else
    {
        report.looper = { Verdict::State::missing,
                          "RC-600: not found",
                          "Check its USB cable and that it is switched on. If its screen is showing a storage or "
                          "backup mode, leave that mode first. On Windows it may need the Boss RC-600 driver. "
                          "Then press Scan again." };
    }
}

DeviceReport scanDevices()
{
    DeviceReport report;

    juce::AudioDeviceManager manager;
    juce::OwnedArray<juce::AudioIODeviceType> types;
    manager.createAudioDeviceTypes (types);

    for (auto* type : types)
    {
        report.audioTypeNames.add (type->getTypeName());
        type->scanForDevices();

        const auto inputs  = type->getDeviceNames (true);
        const auto outputs = type->getDeviceNames (false);

        if (! type->hasSeparateInputsAndOutputs())
        {
            // ASIO-style: one name is the whole device, both directions.
            for (const auto& name : outputs)
                report.audioDevices.add (describeDevice (*type, name, name));

            continue;
        }

        // A name in both lists is one device with both directions; otherwise
        // (Windows Audio) inputs and outputs are separate entries.
        for (const auto& name : outputs)
            report.audioDevices.add (describeDevice (*type, name, inputs.contains (name) ? name : juce::String()));

        for (const auto& name : inputs)
            if (! outputs.contains (name))
                report.audioDevices.add (describeDevice (*type, {}, name));
    }

    report.midiInputs  = juce::MidiInput::getAvailableDevices();
    report.midiOutputs = juce::MidiOutput::getAvailableDevices();

    judge (report);
    return report;
}

juce::String DeviceReport::toText() const
{
    juce::StringArray lines;
    const auto add = [&lines] (const juce::String& line = {}) { lines.add (line); };

    add ("GigRig report");
    add ("Build:    " + juce::String (GIGRIG_BUILD_LABEL) + " (version " + JUCE_APPLICATION_VERSION_STRING + ")");
    add ("System:   " + juce::SystemStats::getOperatingSystemName()
            + (juce::SystemStats::isOperatingSystem64Bit() ? ", 64-bit" : ", 32-bit"));
    add ("CPU:      " + juce::SystemStats::getCpuModel() + ", "
            + juce::String (juce::SystemStats::getNumPhysicalCpus()) + " cores / "
            + juce::String (juce::SystemStats::getNumCpus()) + " threads, "
            + juce::String (juce::SystemStats::getMemorySizeInMegabytes() / 1024) + " GB memory");
    add ("Scanned:  " + juce::Time::getCurrentTime().toString (true, true, false, true));
    add();

    add ("RESULT");
    add ("------");

    for (const auto* verdict : { &mixer, &looper })
    {
        add (stateTag (verdict->state) + " " + verdict->headline);
        add ("            " + verdict->detail);
    }

    add();
    add ("AUDIO DEVICES");
    add ("-------------");
    add ("Driver types on this computer: "
            + (audioTypeNames.isEmpty() ? juce::String ("none") : audioTypeNames.joinIntoString (", ")));

    for (const auto& typeName : audioTypeNames)
    {
        add();
        add ("== " + typeName + " ==");

        int count = 0;

        for (const auto& device : audioDevices)
        {
            if (device.typeName != typeName)
                continue;

            ++count;
            add ("  " + device.name);

            if (device.problem.isNotEmpty())
                add ("      Problem: " + device.problem);

            if (! device.inputChannels.isEmpty())
                add ("      Inputs (" + juce::String (device.inputChannels.size()) + "):  "
                        + joinNumbered (device.inputChannels));

            if (! device.outputChannels.isEmpty())
                add ("      Outputs (" + juce::String (device.outputChannels.size()) + "): "
                        + joinNumbered (device.outputChannels));

            if (! device.sampleRates.isEmpty())
                add ("      Sample rates: " + joinNumbers (device.sampleRates));

            if (! device.bufferSizes.isEmpty())
                add ("      Buffer sizes: " + joinNumbers (device.bufferSizes));
        }

        if (count == 0)
            add ("  (no devices)");
    }

    const auto addMidi = [&add] (const juce::String& title, const juce::Array<juce::MidiDeviceInfo>& devices)
    {
        add();
        add (title);
        add (juce::String::repeatedString ("-", title.length()));

        if (devices.isEmpty())
            add ("  (none)");

        for (const auto& d : devices)
            add ("  " + d.name);
    };

    addMidi ("MIDI INPUTS (things this computer can receive MIDI from)", midiInputs);
    addMidi ("MIDI OUTPUTS (things this computer can send MIDI to)", midiOutputs);

    return lines.joinIntoString ("\n") + "\n";
}

} // namespace gigrig
