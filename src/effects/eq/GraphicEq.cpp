#include "GraphicEq.h"

namespace gigrig::fx
{

GraphicEq::GraphicEq()
{
    // Ids never change: they are saved in presets and songs.
    static const char* ids[numBands]    { "b100", "b200", "b400", "b800", "b1k6", "b3k2", "b6k4" };
    static const char* labels[numBands] { "100 Hz", "200 Hz", "400 Hz", "800 Hz", "1.6 kHz", "3.2 kHz", "6.4 kHz" };

    for (int b = 0; b < numBands; ++b)
        pBand[(size_t) b] = addParam (ids[b], labels[b], "dB", -15.0f, 15.0f, 0.0f);

    pLevel = addParam ("level", "Level", "dB", -15.0f, 15.0f, 0.0f);
}

void GraphicEq::onPrepare (double rate, int)
{
    // Each slider is a fixed band-pass filter whose output is added to (boost) or taken from
    // (cut) the guitar, as in the GE-7's own circuit. The filters never change, only how
    // much of each is mixed in, so moving a slider can never click.
    for (int b = 0; b < numBands; ++b)
        bands[(size_t) b].bandPass (rate, bandHz[b], 1.4);
    onReset();
}

void GraphicEq::onReset() noexcept
{
    for (auto& b : bands)
        b.clear();
}

void GraphicEq::processChunk (float* const* ch, int numCh, int n) noexcept
{
    std::array<float, numBands> mix {};
    for (int b = 0; b < numBands; ++b)
    {
        mix[(size_t) b] = dbToGain (value (pBand[(size_t) b])) - 1.0f;
    }

    const float level = dbToGain (value (pLevel));

    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < n; ++i)
        {
            float x = ch[c][i];
            for (int b = 0; b < numBands; ++b)
            {
                // Run even when flat, so the filter is ready the moment a slider moves.
                const float band = bands[(size_t) b].run (x, c);
                x += mix[(size_t) b] * band;
            }
            ch[c][i] = x * level;
        }
}

} // namespace gigrig::fx
