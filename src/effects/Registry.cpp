#include "Registry.h"

#include "bass9/Bass9.h"
#include "bluesdriver/BluesDriver.h"
#include "boost/CleanBoost.h"
#include "fuzz/Fuzz.h"
#include "klon/Klon.h"
#include "marshall/MarshallBox.h"
#include "tubescreamer/TubeScreamer.h"

namespace gigrig::fx
{

void tubeScreamerChecks (const Check&);
void driveChecks (const std::string& id, const Check&);
void bass9Checks (const Check&);
void fuzzChecks (const Check&);

const std::vector<EffectType>& effectTypes()
{
    // One line per effect. The order is the order on screen.
    static const std::vector<EffectType> list {
        { "ts",       "Tube Screamer style", "Drive", "Mid-hump overdrive with a tight low end (TS808 / TS9).",
          [] { return std::make_unique<TubeScreamer>(); }, tubeScreamerChecks },
        { "bd",       "Blues Driver style",  "Drive", "Open, amp-like overdrive that cleans up with the guitar's volume (BD-2).",
          [] { return std::make_unique<BluesDriver>(); }, [] (const Check& c) { driveChecks ("bd", c); } },
        { "klon",     "Klon style",          "Drive", "Clean plus driven blend: a fat boost at low gain, smooth drive higher up (Centaur).",
          [] { return std::make_unique<Klon>(); }, [] (const Check& c) { driveChecks ("klon", c); } },
        { "marshall", "Marshall in a box",   "Drive", "Cranked Plexi / JCM800-style preamp with bass, middle, treble and presence.",
          [] { return std::make_unique<MarshallBox>(); }, [] (const Check& c) { driveChecks ("marshall", c); } },
        { "fuzz",     "Fuzz Face style",     "Drive", "Round, woolly two-transistor fuzz, with a bias knob for the sputtery dying-battery sound.",
          [] { return std::make_unique<Fuzz>(); }, fuzzChecks },
        { "boost",    "Clean boost",         "Drive", "Louder with a little colour, to push a drive or the amp.",
          [] { return std::make_unique<CleanBoost>(); }, {} },
        { "bass9",    "Bass9 PRECISION copy", "Bass", "Guitar in, P-bass out: the app's copy of the EHX Bass9 PRECISION patch.",
          [] { return std::make_unique<Bass9>(); }, bass9Checks },
    };

    return list;
}

const EffectType* findEffectType (const std::string& id)
{
    for (const auto& t : effectTypes())
        if (t.id == id)
            return &t;

    return nullptr;
}

std::unique_ptr<Effect> makeEffect (const std::string& id)
{
    const auto* t = findEffectType (id);
    return t != nullptr ? t->make() : nullptr;
}

} // namespace gigrig::fx
