#pragma once

#include <juce_core/juce_core.h>

namespace gigrig::responses
{

/** Where response files live: Documents/GigRig/Responses. */
juce::File folder();

/** Makes the folder and tells the "ir" effect to look there. Call once at start. */
void setUp();

struct Made
{
    bool ok = false;
    juce::String text;      // what happened, in words for the screen and the report
    juce::File file;
};

/** Makes a response file from an Amp capture WAV of a tone-only pedal (channel 1 = the test
    sound as sent, channel 2 = what came back): Tom's BodyRez at the setting he uses, an EQ
    pedal, a preamp set clean. Slow (a few seconds): call it from a background thread.
    A .txt with the same name records what it found. */
Made makeFromCapture (const juce::File& capture, const juce::File& target);

} // namespace gigrig::responses

namespace gigrig::starterpresets
{

/** The presets built into the app ("Starter - Acoustic" and so on), as name and text. */
std::vector<std::pair<juce::String, juce::String>> all();

/** Copies each one into the presets folder unless a preset of that name is already there
    (Tom's own version is never overwritten). Returns how many were written. */
int setUp (const juce::File& presetFolder);

} // namespace gigrig::starterpresets
