#pragma once

#include "../Effect.h"
#include "BassMachine.h"

namespace gigrig::fx
{

/** "bass9": the app's copy of the EHX Bass9's PRECISION patch (guitar in, P-bass out).
    The sound itself is in BassMachine (first built 20 Nov 2026 in PR #14); this wraps it in
    the common effect interface. Setting ids are BassSettings' keys and must not change.
    Mono: on a stereo chain both sides get the bass made from the left. */
class Bass9 final : public Effect
{
public:
    Bass9();
    std::string typeId() const override                 { return "bass9"; }
    double cpuBudgetPercent() const noexcept override   { return 20.0; }

private:
    void onPrepare (double rate, int maxBlock) override;
    void onReset() noexcept override;
    void processChunk (float* const* ch, int numCh, int n) noexcept override;

    bass9::BassMachine machine;
    bass9::BassSettings settings;
};

} // namespace gigrig::fx
