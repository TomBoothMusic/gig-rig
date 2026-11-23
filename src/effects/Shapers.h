#pragma once

// Clipping curves shared by the drives. Inputs and outputs in "volts": the drives scale the
// digital signal so 0.5 is a hot guitar pickup, as the real pedals see it.

#include <cmath>

namespace gigrig::fx::shape
{

/** Two diodes back to back (silicon about 0.55 V): a firm but rounded knee. */
inline float diodePair (float u, float vf, float knee = 2.5f) noexcept
{
    const float a = std::abs (u) / vf;
    return u / std::pow (1.0f + std::pow (a, knee), 1.0f / knee);
}

/** Soft, symmetrical (germanium diodes to ground, or a transistor running out of room). */
inline float soft (float u, float vf) noexcept
{
    return vf * std::tanh (u / vf);
}

/** Uneven clipping: the positive half flattens earlier than the negative half, which adds the
    even harmonics that make a drive sound like an amp. Passes through 0 at 0. */
inline float uneven (float u, float vPos, float vNeg) noexcept
{
    return u >= 0.0f ? vPos * std::tanh (u / vPos) : vNeg * std::tanh (u / vNeg);
}

/** A 12AX7 triode stage, much simplified: grid current squashes the positive side hard,
    the negative side runs into cut-off softly. Output inverted back to the input's polarity. */
inline float triode (float u) noexcept
{
    return u >= 0.0f ? u / (1.0f + u) * 1.0f            // grid conduction: hard knee near +1
                     : std::tanh (u * 0.8f) / 0.8f * 1.0f; // cut-off: gentler
}

} // namespace gigrig::fx::shape
