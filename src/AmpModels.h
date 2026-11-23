#pragma once

#include <juce_core/juce_core.h>

// The amp models (.nam files) the app can play: where they live, and the ones built into
// the app (trained from Tom's Dream '65 captures), which are copied there on start.

namespace gigrig::ampmodels
{

/** Documents/GigRig/Amp models. */
juce::File folder();

/** Copies the built-in models into the folder (adding missing ones, refreshing changed ones),
    then tells the amp effect where to look and which model a new amp starts with.
    Returns how many files it wrote. */
int setUp (const juce::File& folder);

/** The models in the folder, by name (no ".nam"), sorted. */
juce::StringArray names (const juce::File& folder);

/** The name a new amp starts with. */
juce::String defaultName();

} // namespace gigrig::ampmodels
