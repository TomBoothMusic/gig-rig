#pragma once

// Tools for response files ("impulse responses", IRs): reading and writing WAV files without
// JUCE, changing a recording's sample rate, and working out a pedal's response from a
// recording of a test sound going in and what came back out.
//
// A response file is a short WAV that holds everything a tone-only box does to the sound
// (an EQ, a body-resonance pedal like the BodyRez, a speaker cabinet, a microphone in front
// of a guitar). Playing a guitar "through" the file gives the same tone. It cannot hold
// anything that changes with how hard you play (distortion, compression) or over time
// (chorus, reverb longer than the file).
//
// Message thread or a background thread only: everything here allocates.

#include <string>
#include <vector>

namespace gigrig::fx::ir
{

struct Wav
{
    std::vector<std::vector<float>> channels;
    double rate = 48000.0;
};

/** Reads a WAV file (16, 24 or 32-bit whole numbers, or 32/64-bit floating point). */
bool readWav (const std::string& path, Wav& wav, std::string& error);
/** Writes a 32-bit floating point WAV. */
bool writeWav (const std::string& path, const Wav& wav, std::string& error);

/** A recording at another sample rate (a windowed-sinc resampler: slow, exact, not for the audio thread). */
std::vector<float> resample (const std::vector<float>& x, double fromRate, double toRate);

/** The same for a response file. A response's points are worth more the further apart they
    are, so they are scaled by fromRate / toRate: the response then does the same thing to the
    sound at either rate. */
std::vector<float> resampleResponse (const std::vector<float>& taps, double fromRate, double toRate);

/** In-place FFT of a power-of-two length (re and im the same size). inverse = true also divides by the length. */
void fft (std::vector<double>& re, std::vector<double>& im, bool inverse);

struct Response
{
    std::vector<float> taps;        // empty if it failed
    double rate = 48000.0;
    double delayMs = 0.0;           // how late the box's sound came back (leads, converters, the box itself): taken out
    double levelDb = 0.0;           // how much louder or quieter the box was (taken out, so the file is level-neutral)
    double matchPercent = 0.0;      // how much of what came back a response file can explain (under about 90: the box also squashes or distorts)
    std::string error;
};

/** Works out a box's response from what was sent into it and what came back (both at 'rate',
    as in an Amp capture WAV: channel 1 sent, channel 2 back). The result is at outRate, at
    most outTaps long, and level-neutral over the range a guitar or voice lives in. */
Response makeResponse (const std::vector<float>& sent, const std::vector<float>& back, double rate,
                       double outRate = 48000.0, int outTaps = 4096);

/** The response's gain in dB at a frequency (for checks and for the report). */
double responseDb (const std::vector<float>& taps, double rate, double hz);

} // namespace gigrig::fx::ir
