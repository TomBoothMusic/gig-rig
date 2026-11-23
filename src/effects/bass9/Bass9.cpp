#include "Bass9.h"

namespace gigrig::fx
{

Bass9::Bass9()
{
    for (const auto& r : bass9::BassSettings::ranges())
        addParam (r.key, r.label, r.unit, r.min, r.max, r.def);
}

void Bass9::onPrepare (double rate, int)
{
    machine.prepare (rate);
}

void Bass9::onReset() noexcept
{
    machine.reset();
}

void Bass9::processChunk (float* const* ch, int numCh, int n) noexcept
{
    if (anyChanged())
    {
        for (int i = 0; i < (int) params().size(); ++i)
            settings.setIndex (i, value (i));

        machine.setSettings (settings);
    }

    machine.process (ch[0], ch[0], n);

    for (int c = 1; c < numCh; ++c)
        for (int i = 0; i < n; ++i)
            ch[c][i] = ch[0][i];
}

} // namespace gigrig::fx
