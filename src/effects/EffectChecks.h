#pragma once

#include "Registry.h"

namespace gigrig::fx
{

/** Every check in CLAUDE.md "Adding an effect", run on every effect in the registry, plus
    each effect's own checks, the chain and the oversampler. The self-test calls this. */
void runEffectChecks (const Check& check);

/** Tools for effects' own checks. */
namespace test
{
    std::vector<float> sine (double hz, double seconds, double level, double rate);
    /** Strength of one frequency from sample 'from' on (Goertzel), as an amplitude. */
    double strength (const std::vector<float>& x, size_t from, double hz, double rate);
    double rms (const std::vector<float>& x, size_t from = 0);
    /** Runs a mono signal through an effect in blocks. */
    std::vector<float> run (Effect& fx, std::vector<float> x, int block = 64);
    std::string num (double v, int places = 1);
}

} // namespace gigrig::fx
