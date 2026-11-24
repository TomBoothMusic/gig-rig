#include "ResponsePlayer.h"
#include "IrTools.h"

#include <algorithm>
#include <filesystem>
#include <mutex>

namespace gigrig::fx
{

namespace
{
    std::mutex folderLock;
    std::string responseFolder;
}

void ResponsePlayer::setFolder (const std::string& f)    { std::lock_guard<std::mutex> l (folderLock); responseFolder = f; }
std::string ResponsePlayer::folder()                     { std::lock_guard<std::mutex> l (folderLock); return responseFolder; }

ResponsePlayer::ResponsePlayer()
{
    pMix   = addParam ("mix",   "Mix",   "%",  0.0f, 100.0f, 100.0f);
    pLevel = addParam ("level", "Level", "dB", -24.0f, 24.0f, 0.0f);
}

ResponsePlayer::~ResponsePlayer()
{
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
}

//==============================================================================
std::unique_ptr<ResponsePlayer::Loaded> ResponsePlayer::load (const std::string& nameOrPath, double rate, std::string& error) const
{
    namespace fs = std::filesystem;
    fs::path path (nameOrPath);

    if (! path.is_absolute())
    {
        path = fs::path (folder()) / nameOrPath;
        if (path.extension() != ".wav" && path.extension() != ".WAV")
            path += ".wav";
    }

    std::error_code ec;
    if (! fs::exists (path, ec))
    {
        error = "can't find the response file \"" + nameOrPath + "\"";
        return nullptr;
    }

    ir::Wav wav;
    std::string why;
    if (! ir::readWav (path.string(), wav, why) || wav.channels.empty() || wav.channels[0].empty())
    {
        error = "can't read \"" + nameOrPath + "\": " + (why.empty() ? "it is empty" : why);
        return nullptr;
    }

    // Only as much of the file as will be used (a long file would take a while to convert).
    auto& first = wav.channels[0];
    const auto useful = (size_t) ((double) (maxTaps + 64) * wav.rate / rate) + 64;
    const bool cut = first.size() > useful;
    if (cut) first.resize (useful);

    auto m = std::make_unique<Loaded>();
    m->taps = ir::resampleResponse (first, wav.rate, rate);
    const auto fileTaps = m->taps.size();
    if (m->taps.size() > (size_t) maxTaps)
        m->taps.resize ((size_t) maxTaps);

    if (cut || fileTaps > (size_t) maxTaps)
    {
        // Cut short: ease the end to nothing so the cut itself is not heard.
        const size_t tail = m->taps.size() / 8;
        for (size_t i = 0; i < tail; ++i)
            m->taps[m->taps.size() - 1 - i] *= (float) i / (float) tail;
    }

    for (auto v : m->taps)
        if (! std::isfinite (v))
        {
            error = "\"" + nameOrPath + "\" holds broken numbers";
            return nullptr;
        }

    // Trailing silence costs processor time for nothing.
    while (m->taps.size() > 1 && std::abs (m->taps.back()) < 1.0e-7f)
        m->taps.pop_back();

    m->name = path.stem().string();
    return m;
}

void ResponsePlayer::setText (const std::string& key, const std::string& value)
{
    if (key != "file")
        return;

    delete retired.exchange (nullptr);
    fileName = value;

    std::string error;
    auto m = value.empty() ? nullptr : load (value, innerRate, error);

    if (m == nullptr)
    {
        loadStatus = value.empty() ? "no response file chosen (a straight wire)" : error + " (a straight wire meanwhile)";
        // An empty set of points means "straight wire".
        m = std::make_unique<Loaded>();
    }
    else
    {
        loadStatus = "playing " + m->name + " (" + std::to_string ((int) std::lround ((double) m->taps.size() * 1000.0 / innerRate)) + " ms long)";
    }

    delete pending.exchange (m.release());
}

std::string ResponsePlayer::getText (const std::string& key) const     { return key == "file" ? fileName : std::string(); }
std::string ResponsePlayer::status() const                             { return loadStatus; }

//==============================================================================
void ResponsePlayer::onPrepare (double rate, int)
{
    bridge.prepare (rate);
    const double newInner = bridge.innerRate (rate);
    history.assign ((size_t) maxTaps * 2, 0.0f);

    // Audio is stopped while preparing. A file read before the rate was known (or at another
    // rate) is read again for this one.
    if (! isPrepared || std::abs (newInner - innerRate) > 1.0e-6)
    {
        innerRate = newInner;
        isPrepared = true;
        if (! fileName.empty())
            setText ("file", fileName);
    }

    if (auto* p = pending.exchange (nullptr))
        current.reset (p);

    onReset();
}

void ResponsePlayer::onReset() noexcept
{
    bridge.clear();
    std::fill (history.begin(), history.end(), 0.0f);
    writeAt = 0;
    dip = 1.0f;
    swapping = false;
}

void ResponsePlayer::runInner (float* x, int m) noexcept
{
    const float mix = value (pMix) * 0.01f;
    const float dipStep = (float) (1.0 / (0.004 * innerRate));

    for (int i = 0; i < m; ++i)
    {
        // A new file from the message thread: dip the response, swap, bring it back.
        if (! swapping && pending.load (std::memory_order_relaxed) != nullptr && retired.load (std::memory_order_relaxed) == nullptr)
            swapping = true;

        if (swapping)
        {
            dip -= dipStep;
            if (dip <= 0.0f)
            {
                dip = 0.0f;
                if (auto* p = pending.exchange (nullptr))
                {
                    retired.store (current.release());
                    current.reset (p);
                }
                swapping = false;
            }
        }
        else if (dip < 1.0f)
            dip = std::min (1.0f, dip + dipStep);

        const float in = x[i];
        writeAt = writeAt == 0 ? maxTaps - 1 : writeAt - 1;
        history[(size_t) writeAt] = in;
        history[(size_t) (writeAt + maxTaps)] = in;

        float wet = in;

        if (current != nullptr && ! current->taps.empty())
        {
            const float* h = current->taps.data();
            const float* p = history.data() + writeAt;
            const int count = (int) current->taps.size();
            // Eight running totals at once, which the compiler turns into wide (SIMD) maths.
            float acc[8] {};
            int k = 0;
            for (; k + 8 <= count; k += 8)
                for (int j = 0; j < 8; ++j)
                    acc[j] += h[k + j] * p[k + j];
            float sum = ((acc[0] + acc[1]) + (acc[2] + acc[3])) + ((acc[4] + acc[5]) + (acc[6] + acc[7]));
            for (; k < count; ++k)
                sum += h[k] * p[k];
            wet = sum;
        }

        // While dipped, the dry sound fills in, so the level never drops out.
        const float w = mix * dip;
        x[i] = in + w * (wet - in);
    }
}

void ResponsePlayer::processChunk (float* const* ch, int numCh, int n) noexcept
{
    bridge.process (ch[0], n, [this] (float* x, int m) { runInner (x, m); });

    const float level = dbToGain (value (pLevel));
    for (int i = 0; i < n; ++i)
        ch[0][i] *= level;

    for (int c = 1; c < numCh; ++c)
        std::copy (ch[0], ch[0] + n, ch[c]);
}

} // namespace gigrig::fx
