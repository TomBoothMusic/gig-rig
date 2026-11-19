#include "SelfTest.h"
#include "DeviceReport.h"
#include "MidiMonitor.h"

namespace gigrig
{

namespace
{
    juce::StringArray channels (int count)
    {
        juce::StringArray names;

        for (int i = 1; i <= count; ++i)
            names.add ("Ch " + juce::String (i));

        return names;
    }

    AudioDeviceEntry device (const juce::String& type, const juce::String& name, int ins, int outs)
    {
        AudioDeviceEntry e;
        e.typeName = type;
        e.name = name;
        e.inputChannels = channels (ins);
        e.outputChannels = channels (outs);
        return e;
    }

    juce::MidiDeviceInfo midi (const juce::String& name)
    {
        return { name, name };
    }
}

juce::String runSelfTest (bool& passed)
{
    juce::StringArray log;
    int failures = 0;

    const auto check = [&] (bool condition, const juce::String& what)
    {
        log.add ((condition ? "ok    " : "FAIL  ") + what);

        if (! condition)
            ++failures;
    };

    using State = Verdict::State;

    // --- Name matching -------------------------------------------------------
    check (looksLikeCQ ("CQ12T"),                          "CQ12T is a CQ");
    check (looksLikeCQ ("Allen & Heath CQ ASIO Driver"),   "A&H CQ ASIO driver is a CQ");
    check (looksLikeCQ ("Line (CQ-12T)"),                  "Windows-style 'Line (CQ-12T)' is a CQ");
    check (looksLikeCQ ("Allen&Heath USB Audio"),          "Allen&Heath without the model is a CQ");
    check (! looksLikeCQ ("Realtek High Definition Audio"), "Realtek is not a CQ");
    check (! looksLikeCQ ("WING"),                         "Wing is not a CQ");
    check (! looksLikeCQ ("Acquire Capture"),              "a word containing 'cq' is not a CQ");
    check (looksLikeRC600 ("RC-600"),                      "RC-600 is an RC-600");
    check (looksLikeRC600 ("MIDIIN2 (RC-600)"),            "second RC-600 port is an RC-600");
    check (! looksLikeRC600 ("RC-505mkII"),                "RC-505 is not an RC-600");

    // --- Nothing plugged in --------------------------------------------------
    {
        DeviceReport r;
        r.audioDevices.add (device ("Windows Audio", "Speakers (Realtek Audio)", 0, 2));
        judge (r);
        check (r.mixer.state == State::missing,  "no CQ -> mixer not found");
        check (r.looper.state == State::missing, "no RC-600 -> looper not found");
    }

    // --- Driver installed: ASIO shows all 16 ---------------------------------
    {
        DeviceReport r;
        r.audioDevices.add (device ("Windows Audio", "Line (CQ12T)", 16, 0));
        r.audioDevices.add (device ("Windows Audio", "Speakers (CQ12T)", 0, 16));
        r.audioDevices.add (device ("ASIO", "CQ ASIO Driver", 16, 16));
        r.midiInputs.add (midi ("RC-600"));
        r.midiOutputs.add (midi ("RC-600"));
        judge (r);
        check (r.mixer.state == State::found,           "CQ 16x16 -> mixer found");
        check (r.mixer.headline.contains ("ASIO"),      "ASIO preferred when channel counts tie");
        check (r.mixer.headline.contains ("16 inputs"), "headline gives the input count");
        check (r.looper.state == State::found,          "RC-600 input -> looper found");
    }

    // --- No driver: Windows only shows a stereo pair --------------------------
    {
        DeviceReport r;
        r.audioDevices.add (device ("Windows Audio", "Line (CQ12T)", 2, 0));
        r.audioDevices.add (device ("Windows Audio", "Speakers (CQ12T)", 0, 2));
        r.midiOutputs.add (midi ("RC-600"));
        judge (r);
        check (r.mixer.state == State::partial,  "CQ with 2 channels -> partly");
        check (r.looper.state == State::partial, "RC-600 output only -> partly");
    }

    // --- Separate in and out entries still add up to a full device ------------
    {
        DeviceReport r;
        r.audioDevices.add (device ("ALSA", "CQ12T, USB Audio; Direct hardware device without any conversions", 16, 16));
        judge (r);
        check (r.mixer.state == State::found, "Linux ALSA CQ 16x16 -> mixer found");
    }

    // --- Report text and MIDI wording ----------------------------------------
    {
        DeviceReport r;
        r.audioTypeNames.add ("ASIO");
        r.audioDevices.add (device ("ASIO", "CQ ASIO Driver", 16, 16));
        judge (r);
        const auto text = r.toText();
        check (text.contains ("[FOUND]"),            "report shows a FOUND tag");
        check (text.contains ("16: Ch 16"),          "report numbers every channel");
        check (text.contains ("[NOT FOUND] RC-600"), "report shows the RC-600 as not found");
    }

    check (MidiMonitor::describe (juce::MidiMessage::controllerEvent (1, 80, 127)) == "CC 80 = 127 (channel 1)",
           "CC message reads plainly");
    check (MidiMonitor::describe (juce::MidiMessage::programChange (1, 10)) == "Program change 11 (channel 1)",
           "program change is shown counting from 1");

    passed = failures == 0;
    log.add ({});
    log.add (passed ? "Self-test: passed (" + juce::String (log.size() - 1) + " checks)"
                    : "Self-test: FAILED (" + juce::String (failures) + " checks)");

    return log.joinIntoString ("\n") + "\n";
}

} // namespace gigrig
