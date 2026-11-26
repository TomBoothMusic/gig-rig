#pragma once

// THE LIST OF EFFECTS. Adding an effect = its folder under src/effects/<id>/, one line in
// Registry.cpp, its .cpp files in CMakeLists.txt. See docs/effects.md.

#include "Effect.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gigrig::fx
{

/** Called by an effect's own checks: (passed, what was checked). */
using Check = std::function<void (bool, const std::string&)>;

struct EffectType
{
    std::string id;             // stable: presets and songs use it, never rename
    std::string name;           // shown on screen
    std::string group;          // "Drive", "Bass"...
    std::string description;    // one plain sentence for the screen
    std::function<std::unique_ptr<Effect>()> make;
    std::function<void (const Check&)> extraChecks;     // the effect's own self-test checks, may be empty
};

const std::vector<EffectType>& effectTypes();
/** The mixer's own blocks (EQ, compressor, notches, limiter, line delay, wet filter): found by
    findEffectType and makeEffect, checked like every effect, but not offered on the Effects tab. */
const std::vector<EffectType>& mixerEffectTypes();
const EffectType* findEffectType (const std::string& id);
std::unique_ptr<Effect> makeEffect (const std::string& id);

} // namespace gigrig::fx
