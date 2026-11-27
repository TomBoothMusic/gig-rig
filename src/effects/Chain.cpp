#include "Chain.h"
#include "Registry.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace gigrig::fx
{

//==============================================================================
float SlotModel::param (const std::string& id, float fallback) const
{
    for (const auto& [k, v] : params)
        if (k == id)
            return v;

    return fallback;
}

void SlotModel::setParam (const std::string& id, float value)
{
    for (auto& [k, v] : params)
        if (k == id) { v = value; return; }

    params.emplace_back (id, value);
}

std::string SlotModel::text (const std::string& key) const
{
    for (const auto& [k, v] : texts)
        if (k == key)
            return v;

    return {};
}

void SlotModel::setText (const std::string& key, const std::string& value)
{
    for (auto& [k, v] : texts)
        if (k == key) { v = value; return; }

    texts.emplace_back (key, value);
}

std::string ChainModel::toText() const
{
    std::ostringstream s;
    s << "# GigRig effect chain. One effect per 'slot=' line, its settings below it.\n";

    for (const auto& slot : slots)
    {
        s << "slot=" << slot.type << "\n";
        s << "on=" << (slot.on ? 1 : 0) << "\n";

        for (const auto& [k, v] : slot.params)
            s << k << "=" << v << "\n";

        for (const auto& [k, v] : slot.texts)
            s << "text." << k << "=" << v << "\n";
    }

    return s.str();
}

size_t ChainModel::stackPoint() const
{
    auto at = slots.size();
    for (size_t k = 0; k < slots.size(); ++k)
        if (slots[k].type == "amp")
        {
            at = k;
            break;
        }

    // A volume pedal sitting right in front of the amp stays there: on Tom's board the drives
    // push the pedal and the pedal pushes the amp, so stacked drives go in front of the pedal.
    while (at > 0 && at < slots.size() && slots[at - 1].type == "volume")
        --at;

    return at;
}

int ChainModel::addFrom (const ChainModel& other, int maxSlots)
{
    const auto at = stackPoint();

    int added = 0;
    for (const auto& s : other.slots)
    {
        if ((int) slots.size() >= maxSlots)
            break;
        slots.insert (slots.begin() + (std::ptrdiff_t) (at + (size_t) added), s);
        ++added;
    }
    return added;
}

ChainModel ChainModel::fromText (const std::string& text, std::vector<std::string>* skipped)
{
    ChainModel m;
    std::istringstream lines (text);
    std::string line;
    bool inUnknown = false;

    while (std::getline (lines, line))
    {
        while (! line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();

        if (line.empty() || line[0] == '#')
            continue;

        const auto eq = line.find ('=');

        if (eq == std::string::npos)
            continue;

        const auto key = line.substr (0, eq), value = line.substr (eq + 1);

        if (key == "slot")
        {
            inUnknown = findEffectType (value) == nullptr;

            if (inUnknown)
            {
                if (skipped != nullptr) skipped->push_back (value);
                continue;
            }

            if ((int) m.slots.size() < Chain::maxSlots)
                m.slots.push_back ({ value, true, {}, {} });
            else
                inUnknown = true;

            continue;
        }

        if (inUnknown || m.slots.empty())
            continue;

        auto& slot = m.slots.back();

        if (key == "on")
            slot.on = value != "0";
        else if (key.rfind ("text.", 0) == 0)
            slot.setText (key.substr (5), value);
        else
        {
            char* end = nullptr;
            const float v = std::strtof (value.c_str(), &end);

            if (end != value.c_str() && std::isfinite (v))
                slot.setParam (key, v);
        }
    }

    return m;
}

//==============================================================================
Chain::Chain() = default;

Chain::~Chain()
{
    freeAll();
}

void Chain::freeAll()
{
    std::vector<Built*> all { pending.exchange (nullptr), retired.exchange (nullptr), live, fading };
    std::sort (all.begin(), all.end());
    all.erase (std::unique (all.begin(), all.end()), all.end());

    for (auto* b : all)
        delete b;

    live = fading = latest = nullptr;
}

Chain::Built* Chain::build (const ChainModel& m) const
{
    auto* b = new Built();

    for (const auto& sm : m.slots)
    {
        auto fx = makeEffect (sm.type);

        if (fx == nullptr)
            continue;

        for (const auto& [k, v] : sm.params)
            fx->setParam (k, v);

        for (const auto& [k, v] : sm.texts)
            fx->setText (k, v);

        fx->prepare (rate, maxBlock);

        auto slot = std::make_unique<Slot>();
        slot->fx = std::move (fx);
        slot->on.store (sm.on);
        slot->mix = sm.on ? 1.0f : 0.0f;
        b->slots.push_back (std::move (slot));
    }

    return b;
}

void Chain::prepare (double sampleRate, int maxBlockSize)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock = std::max (16, maxBlockSize);
    fadeLength = std::max (1, (int) std::lround (rate * 0.010));
    wet.assign ((size_t) maxBlock, 0.0f);
    old.assign ((size_t) maxBlock, 0.0f);

    freeAll();
    live = latest = build (current);
    fadePos = 0;
}

void Chain::submit (Built* b)
{
    latest = b;

    // A set the audio thread never took is simply replaced.
    if (auto* untaken = pending.exchange (b))
        delete untaken;
}

void Chain::setModel (const ChainModel& m)
{
    current = m;

    if ((int) current.slots.size() > maxSlots)
        current.slots.resize ((size_t) maxSlots);

    // Not prepared yet (no audio running): build at once so the screen can show the settings;
    // prepare() builds again at the real rate.
    if (live == nullptr && latest == nullptr)
    {
        live = latest = build (current);
        return;
    }

    submit (build (current));
}

bool Chain::setModelGently (const ChainModel& m)
{
    bool same = latest != nullptr && m.slots.size() == current.slots.size() && latest->slots.size() == m.slots.size();

    for (size_t k = 0; same && k < m.slots.size(); ++k)
        same = m.slots[k].type == current.slots[k].type;

    if (! same)
    {
        setModel (m);
        return false;
    }

    for (size_t k = 0; k < m.slots.size(); ++k)
    {
        const auto& want = m.slots[k];
        auto* fx = latest->slots[k]->fx.get();
        setSlotOn ((int) k, want.on);

        for (int p = 0; p < (int) fx->params().size(); ++p)
        {
            const auto& info = fx->params()[(size_t) p];
            const float v = want.param (info.id, info.def);

            if (v != fx->getParam (p))
                fx->setParam (p, v);
        }

        for (const auto& key : fx->textKeys())
            if (const auto v = want.text (key); v != fx->getText (key) && ! (v.empty() && want.texts.empty()))
                fx->setText (key, v);
    }

    current = m;
    return true;
}

void Chain::setSlotOn (int slot, bool on)
{
    if (slot < 0 || slot >= (int) current.slots.size())
        return;

    current.slots[(size_t) slot].on = on;

    if (latest != nullptr && slot < (int) latest->slots.size())
        latest->slots[(size_t) slot]->on.store (on);
}

void Chain::setParam (int slot, int index, float value)
{
    auto* fx = effect (slot);

    if (fx == nullptr || index < 0 || index >= (int) fx->params().size())
        return;

    fx->setParam (index, value);
    current.slots[(size_t) slot].setParam (fx->params()[(size_t) index].id, fx->getParam (index));
}

void Chain::setText (int slot, const std::string& key, const std::string& value)
{
    auto* fx = effect (slot);

    if (fx == nullptr)
        return;

    fx->setText (key, value);
    current.slots[(size_t) slot].setText (key, value);
}

Effect* Chain::effect (int slot) const
{
    if (latest == nullptr || slot < 0 || slot >= (int) latest->slots.size())
        return nullptr;

    return latest->slots[(size_t) slot]->fx.get();
}

void Chain::collectGarbage()
{
    delete retired.exchange (nullptr);
}

int Chain::latencySamples() const
{
    int total = 0;

    if (latest != nullptr)
        for (const auto& s : latest->slots)
            if (s->on.load())
                total += s->fx->latencySamples();

    return total;
}

//==============================================================================
void Chain::runBuilt (Built& b, float* buffer, int n, int from) noexcept
{
    const float step = 1.0f / (float) fadeLength;

    for (size_t k = (size_t) std::max (0, from); k < b.slots.size(); ++k)
    {
        auto& s = *b.slots[k];
        s.fx->setTempo (tempo);
        const float target = s.on.load (std::memory_order_relaxed) ? 1.0f : 0.0f;

        // An effect that is off keeps running unheard, so switching it on never starts it from
        // cold (a cold start can thump) and the processor load is the same either way.
        if (s.mix == 1.0f && target == 1.0f)
        {
            s.fx->process (&buffer, 1, n);
            continue;
        }

        // Off, or fading in or out: run the effect on a copy and blend.
        std::memcpy (wet.data(), buffer, sizeof (float) * (size_t) n);
        float* w = wet.data();
        s.fx->process (&w, 1, n);

        if (s.mix == 0.0f && target == 0.0f)
            continue;

        for (int i = 0; i < n; ++i)
        {
            s.mix = target > s.mix ? std::min (target, s.mix + step) : std::max (target, s.mix - step);
            buffer[i] += (w[i] - buffer[i]) * s.mix;
        }
    }
}

void Chain::retireFading() noexcept
{
    retired.store (fading);
    fading = nullptr;
    tailFrom = -1;
    ringing.store (false, std::memory_order_relaxed);
}

void Chain::process (float* buffer, int n) noexcept
{
    if (n <= 0 || n > maxBlock)
        return;

    // Start a change-over only once the last one's old set has been collected.
    if (fading == nullptr && retired.load() == nullptr)
    {
        if (auto* next = pending.exchange (nullptr))
        {
            fading = live;
            live = next;
            fadePos = 0;
            tailFrom = -1;

            // Does the old set have a delay or reverb sounding? Then it rings out.
            if (fading != nullptr && tailsOn.load (std::memory_order_relaxed))
                for (size_t k = 0; k < fading->slots.size() && tailFrom < 0; ++k)
                    if (fading->slots[k]->mix > 0.0f && fading->slots[k]->fx->hasTail())
                        tailFrom = (int) k;

            tailPos = 0;
            tailQuiet = 0;
            tailGain = 1.0f;
            tailStep = 0.0f;
            ringing.store (tailFrom >= 0, std::memory_order_relaxed);
        }
    }

    if (live == nullptr)
        return;

    if (fading == nullptr)
    {
        runBuilt (*live, buffer, n);
        return;
    }

    if (tailFrom < 0)
    {
        // Change-over: old set and new set side by side, fading from one to the other.
        std::memcpy (old.data(), buffer, sizeof (float) * (size_t) n);
        runBuilt (*fading, old.data(), n);
        runBuilt (*live, buffer, n);

        for (int i = 0; i < n; ++i)
        {
            const float t = std::min (1.0f, (float) fadePos++ / (float) fadeLength);
            buffer[i] = old[(size_t) i] + (buffer[i] - old[(size_t) i]) * t;
        }

        if (fadePos >= fadeLength)
            retireFading();

        return;
    }

    // Ringing out. For the first 10 ms the playing fades out of the old set just in front of
    // its first delay or reverb (so nothing is cut short inside it) while the new set fades in.
    // After that the old set hears silence and only what is still sounding inside it comes out.
    const bool crossing = fadePos < fadeLength;
    int pos = fadePos;

    if (crossing)
    {
        std::memcpy (old.data(), buffer, sizeof (float) * (size_t) n);

        for (size_t k = 0; k < (size_t) tailFrom; ++k)      // what sits in front of the tail, as it was
        {
            auto& s = *fading->slots[k];
            s.fx->setTempo (tempo);

            if (s.mix > 0.0f)
            {
                std::memcpy (wet.data(), old.data(), sizeof (float) * (size_t) n);
                float* w = wet.data();
                s.fx->process (&w, 1, n);

                for (int i = 0; i < n; ++i)
                    old[(size_t) i] += (w[i] - old[(size_t) i]) * s.mix;
            }
        }

        for (int i = 0; i < n; ++i)
            old[(size_t) i] *= 1.0f - std::min (1.0f, (float) pos++ / (float) fadeLength);
    }
    else
    {
        std::memset (old.data(), 0, sizeof (float) * (size_t) n);
    }

    runBuilt (*fading, old.data(), n, tailFrom);
    runBuilt (*live, buffer, n);

    // Ends: by itself once it has been quiet for 0.3 s; faded over the last second of its
    // longest time; or faded in 10 ms because another change is waiting for its turn.
    if (tailStep == 0.0f)
    {
        if (pending.load (std::memory_order_relaxed) != nullptr)
            tailStep = 1.0f / (float) fadeLength;
        else if ((double) tailPos > (maxTailSeconds - 1.0) * rate)
            tailStep = (float) (1.0 / rate);
    }

    float loudest = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float t = std::min (1.0f, (float) fadePos++ / (float) fadeLength);
        tailGain = std::max (0.0f, tailGain - tailStep);
        const float o = old[(size_t) i] * tailGain;
        loudest = std::max (loudest, std::abs (o));
        buffer[i] = buffer[i] * t + o;
    }

    fadePos = std::min (fadePos, fadeLength);
    tailPos += n;
    tailQuiet = loudest < 1.0e-4f ? tailQuiet + n : 0;

    if (! crossing && (tailGain <= 0.0f || (double) tailQuiet > 0.3 * rate))
        retireFading();
}

void Chain::reset() noexcept
{
    if (live != nullptr)
        for (auto& s : live->slots)
            s->fx->reset();
}

} // namespace gigrig::fx
