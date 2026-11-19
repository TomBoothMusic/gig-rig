#pragma once

#include <juce_core/juce_core.h>

namespace gigrig
{

/** Checks the device-matching logic against made-up device lists, so the
    automatic builds can prove it without a CQ or RC-600 plugged in.
    Returns a log; `passed` says whether every check held. */
juce::String runSelfTest (bool& passed);

} // namespace gigrig
