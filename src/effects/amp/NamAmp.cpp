#include "NamAmp.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

#include <filesystem>

namespace gigrig::fx
{

namespace
{
    std::mutex folderLock;
    std::string folder, defaultModel;

    constexpr int maxInner = 16;     // most samples the model is given at once (chunks are 16)
}

struct NamAmp::Model
{
    std::unique_ptr<nam::DSP> dsp;
    std::string name;
    double trainedRate = 48000.0;
    float silence = 0.0f;           // what the model puts out for silence: taken off, so silence stays silent
};

void NamAmp::setModelFolder (const std::string& f)       { std::lock_guard<std::mutex> l (folderLock); folder = f; }
std::string NamAmp::modelFolder()                        { std::lock_guard<std::mutex> l (folderLock); return folder; }
void NamAmp::setDefaultModel (const std::string& m)      { std::lock_guard<std::mutex> l (folderLock); defaultModel = m; }

NamAmp::NamAmp()
{
    pInput  = addParam ("input",  "Input",  "dB", -24.0f, 24.0f, 0.0f);
    pOutput = addParam ("output", "Output", "dB", -24.0f, 12.0f, 0.0f);

    std::string def;
    {
        std::lock_guard<std::mutex> l (folderLock);
        def = defaultModel;
    }

    if (! def.empty())
        setText ("model", def);
}

NamAmp::~NamAmp()
{
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
    delete fadingOut;
}

//==============================================================================
std::unique_ptr<NamAmp::Model> NamAmp::load (const std::string& nameOrPath, std::string& error) const
{
    namespace fs = std::filesystem;
    fs::path path (nameOrPath);

    if (! path.is_absolute())
    {
        path = fs::path (modelFolder()) / nameOrPath;
        if (path.extension() != ".nam")
            path += ".nam";
    }

    try
    {
        if (! fs::exists (path))
        {
            error = "can't find the model \"" + nameOrPath + "\"";
            return nullptr;
        }

        auto m = std::make_unique<Model>();
        m->dsp = nam::get_dsp (path);

        if (m->dsp == nullptr)
        {
            error = "can't read the model \"" + nameOrPath + "\"";
            return nullptr;
        }

        m->name = path.stem().string();
        m->trainedRate = m->dsp->GetExpectedSampleRate() > 0.0 ? m->dsp->GetExpectedSampleRate() : 48000.0;
        m->dsp->ResetAndPrewarm (m->trainedRate, maxInner);

        // Let it settle on silence, and remember what it puts out then.
        std::vector<float> zeros (maxInner, 0.0f), out (maxInner, 0.0f);
        for (int i = 0; i < 8192; i += maxInner)
            m->dsp->process (zeros.data(), out.data(), maxInner);
        m->silence = out[maxInner - 1];
        return m;
    }
    catch (const std::exception& e)
    {
        error = "can't load the model \"" + nameOrPath + "\": " + e.what();
        return nullptr;
    }
}

void NamAmp::setText (const std::string& key, const std::string& value)
{
    if (key != "model")
        return;

    takeOutTheBin();
    std::string error;
    auto m = value.empty() ? nullptr : load (value, error);
    modelName = value;

    if (m == nullptr)
    {
        loadStatus = value.empty() ? "no model chosen" : error;
        return;
    }

    loadStatus = "playing " + m->name + " (trained at " + std::to_string ((int) (m->trainedRate / 1000.0 + 0.5)) + " kHz)";
    delete pending.exchange (m.release());
}

std::string NamAmp::getText (const std::string& key) const     { return key == "model" ? modelName : std::string(); }
std::string NamAmp::status() const                             { return loadStatus; }

void NamAmp::takeOutTheBin()
{
    delete retired.exchange (nullptr);
}

//==============================================================================
void NamAmp::onPrepare (double rate, int /*maxBlock*/)
{
    factor = rate >= 88000.0 ? 2 : 1;
    const auto c = HalfBand::design (0.04);
    down.setCoefs (c);
    up.setCoefs (c);

    inner.assign (maxInner, 0.0f);
    innerOut.assign (maxInner, 0.0f);
    oldOut.assign (maxInner, 0.0f);
    here.assign ((size_t) (2 * maxInner + 4), 0.0f);
    dcBlock.highPass (rate, 5.0);
    fadeLength = std::max (1, (int) (0.02 * rate));

    // Audio is stopped while preparing: take any model waiting, and settle the one in use.
    if (auto* p = pending.exchange (nullptr))
        current.reset (p);

    // The delay of the trip down and back up, measured with a click through a straight wire.
    auto saved = std::move (current);
    onReset();
    std::vector<float> y (64, 0.0f);
    y[0] = 1.0f;
    for (int i = 0; i < 64; i += chunk)
    {
        float* c1[1] { y.data() + i };
        processChunk (c1, 1, chunk);
    }
    int peakAt = 0;
    for (int i = 1; i < 64; ++i)
        if (std::abs (y[(size_t) i]) > std::abs (y[(size_t) peakAt]))
            peakAt = i;
    latency = peakAt;
    current = std::move (saved);
    onReset();
}

void NamAmp::onReset() noexcept
{
    down.clear();
    up.clear();
    carry = 0.0f;
    haveCarry = false;
    dcBlock.clear();
    std::fill (here.begin(), here.end(), 0.0f);
    hereCount = factor == 2 ? 1 : 0;    // one sample of slack, so an odd-sized chunk always has one to give
}

void NamAmp::runModel (Model& m, const float* in, float* out, int n) noexcept
{
    m.dsp->process (const_cast<float*> (in), out, n);
    for (int i = 0; i < n; ++i)
        out[i] -= m.silence;
}

void NamAmp::processChunk (float* const* ch, int numCh, int n) noexcept
{
    if (n <= 0)
        return;

    // A new model from the message thread: fade across to it.
    if (fadingOut == nullptr)
    {
        if (auto* p = pending.exchange (nullptr))
        {
            if (current != nullptr)
            {
                fadingOut = current.release();
                fadeLeft = fadeLength;
            }
            current.reset (p);
        }
    }

    float* x = ch[0];
    const float inGain = dbToGain (value (pInput));
    const float outGain = dbToGain (value (pOutput));

    // Down to the model's rate (with no model yet, it is a straight wire with the same delay).
    int m = 0;

    if (factor == 2)
    {
        // Pairs in, one out. A sample left without its pair waits for the next chunk.
        for (int i = 0; i < n; ++i)
        {
            const float s = x[i] * inGain;
            if (! haveCarry) { carry = s; haveCarry = true; }
            else             { inner[(size_t) m++] = (float) down.down (carry, s); haveCarry = false; }
        }
    }
    else
    {
        for (int i = 0; i < n; ++i)
            inner[(size_t) m++] = x[i] * inGain;
    }

    // The model (and the old one too, while fading across).
    if (current != nullptr)
        runModel (*current, inner.data(), innerOut.data(), m);
    else
        std::copy (inner.begin(), inner.begin() + m, innerOut.begin());

    if (fadingOut != nullptr)
    {
        runModel (*fadingOut, inner.data(), oldOut.data(), m);
        const int step = factor;

        for (int i = 0; i < m; ++i)
        {
            const float t = (float) std::max (0, fadeLeft) / (float) fadeLength;   // 1 -> 0
            innerOut[(size_t) i] = innerOut[(size_t) i] * (1.0f - t) + oldOut[(size_t) i] * t;
            fadeLeft -= step;
        }

        if (fadeLeft <= 0 && retired.load() == nullptr)
        {
            retired.store (fadingOut);
            fadingOut = nullptr;
        }
    }

    // Back up to the app's rate.
    if (factor == 2)
    {
        for (int i = 0; i < m; ++i)
        {
            double a, b;
            up.up (innerOut[(size_t) i], a, b);
            here[(size_t) hereCount++] = (float) a;
            here[(size_t) hereCount++] = (float) b;
        }

        const int give = std::min (n, hereCount);
        std::copy (here.begin(), here.begin() + give, x);
        std::fill (x + give, x + n, 0.0f);
        std::copy (here.begin() + give, here.begin() + hereCount, here.begin());
        hereCount -= give;
    }
    else
    {
        std::copy (innerOut.begin(), innerOut.begin() + m, x);
    }

    for (int i = 0; i < n; ++i)
        x[i] = dcBlock.run (x[i], 0) * outGain;

    for (int c = 1; c < numCh; ++c)
        std::copy (x, x + n, ch[c]);
}

} // namespace gigrig::fx
