#include "IrTools.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace gigrig::fx::ir
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    std::uint32_t u32 (const unsigned char* p)   { return (std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8) | ((std::uint32_t) p[2] << 16) | ((std::uint32_t) p[3] << 24); }
    std::uint16_t u16 (const unsigned char* p)   { return (std::uint16_t) (p[0] | (p[1] << 8)); }

    void put32 (std::string& s, std::uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back ((char) ((v >> (8 * i)) & 0xff)); }
    void put16 (std::string& s, std::uint16_t v) { for (int i = 0; i < 2; ++i) s.push_back ((char) ((v >> (8 * i)) & 0xff)); }

    int nextPowerOfTwo (double n)
    {
        int p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }
}

//==============================================================================
bool readWav (const std::string& path, Wav& wav, std::string& error)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
    {
        error = "can't open " + path;
        return false;
    }

    std::vector<unsigned char> d ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());

    if (d.size() < 44 || std::memcmp (d.data(), "RIFF", 4) != 0 || std::memcmp (d.data() + 8, "WAVE", 4) != 0)
    {
        error = "not a WAV file";
        return false;
    }

    int format = 0, channels = 0, bits = 0;
    double rate = 0.0;
    const unsigned char* data = nullptr;
    size_t dataBytes = 0;

    for (size_t pos = 12; pos + 8 <= d.size();)
    {
        const auto size = (size_t) u32 (d.data() + pos + 4);
        const auto* body = d.data() + pos + 8;
        const auto room = d.size() - pos - 8;

        if (std::memcmp (d.data() + pos, "fmt ", 4) == 0 && size >= 16 && room >= 16)
        {
            format = u16 (body);
            channels = u16 (body + 2);
            rate = (double) u32 (body + 4);
            bits = u16 (body + 14);
            if (format == 0xfffe && size >= 26 && room >= 26)
                format = u16 (body + 24);      // the real format sits in the "extensible" part
        }
        else if (std::memcmp (d.data() + pos, "data", 4) == 0)
        {
            data = body;
            dataBytes = std::min (size, room);
            break;
        }

        pos += 8 + size + (size & 1);
    }

    const int bytes = bits / 8;
    const bool whole = format == 1 && (bits == 16 || bits == 24 || bits == 32);
    const bool floating = format == 3 && (bits == 32 || bits == 64);

    if (data == nullptr || channels < 1 || channels > 64 || rate <= 0.0 || ! (whole || floating))
    {
        error = "a kind of WAV file this can't read (it reads 16, 24 and 32-bit files)";
        return false;
    }

    const size_t frames = dataBytes / (size_t) (bytes * channels);
    wav.rate = rate;
    wav.channels.assign ((size_t) channels, std::vector<float> (frames));

    for (size_t f = 0; f < frames; ++f)
        for (int c = 0; c < channels; ++c)
        {
            const auto* p = data + (f * (size_t) channels + (size_t) c) * (size_t) bytes;
            float v = 0.0f;

            if (floating && bits == 32)         { std::memcpy (&v, p, 4); }
            else if (floating)                  { double w; std::memcpy (&w, p, 8); v = (float) w; }
            else if (bits == 16)                v = (float) (std::int16_t) u16 (p) / 32768.0f;
            else if (bits == 24)                v = (float) ((std::int32_t) ((std::uint32_t) p[0] << 8 | (std::uint32_t) p[1] << 16 | (std::uint32_t) p[2] << 24) >> 8) / 8388608.0f;
            else                                v = (float) ((double) (std::int32_t) u32 (p) / 2147483648.0);

            wav.channels[(size_t) c][f] = v;
        }

    return true;
}

bool writeWav (const std::string& path, const Wav& wav, std::string& error)
{
    const auto channels = wav.channels.size();
    const auto frames = channels > 0 ? wav.channels[0].size() : 0;

    if (channels == 0 || frames == 0)
    {
        error = "nothing to write";
        return false;
    }

    const auto dataBytes = (std::uint32_t) (frames * channels * 4);
    std::string h;
    h += "RIFF"; put32 (h, 36 + dataBytes); h += "WAVE";
    h += "fmt "; put32 (h, 16); put16 (h, 3); put16 (h, (std::uint16_t) channels);
    put32 (h, (std::uint32_t) std::lround (wav.rate)); put32 (h, (std::uint32_t) (std::lround (wav.rate) * (long) channels * 4));
    put16 (h, (std::uint16_t) (channels * 4)); put16 (h, 32);
    h += "data"; put32 (h, dataBytes);

    std::ofstream out (path, std::ios::binary);
    if (! out)
    {
        error = "can't write " + path;
        return false;
    }

    out.write (h.data(), (std::streamsize) h.size());
    for (size_t f = 0; f < frames; ++f)
        for (size_t c = 0; c < channels; ++c)
        {
            const float v = f < wav.channels[c].size() ? wav.channels[c][f] : 0.0f;
            out.write (reinterpret_cast<const char*> (&v), 4);
        }

    return (bool) out;
}

//==============================================================================
std::vector<float> resample (const std::vector<float>& x, double fromRate, double toRate)
{
    if (x.empty() || fromRate <= 0.0 || toRate <= 0.0 || std::abs (fromRate - toRate) < 1.0e-6)
        return x;

    // A sinc pulse 32 crossings either side, under a Kaiser window, cutting off just below
    // the lower of the two rates' top frequency.
    constexpr int half = 32;
    constexpr double beta = 9.0;
    const double ratio = toRate / fromRate;
    const double cutoff = std::min (1.0, ratio) * 0.94;
    const double i0b = besselI0 (beta);
    const auto outLength = (size_t) std::ceil ((double) x.size() * ratio);
    std::vector<float> y (outLength);
    const double reach = half / cutoff;

    for (size_t n = 0; n < outLength; ++n)
    {
        const double centre = (double) n / ratio;
        const long first = (long) std::ceil (centre - reach), last = (long) std::floor (centre + reach);
        double sum = 0.0;

        for (long k = std::max (first, 0L); k <= std::min (last, (long) x.size() - 1); ++k)
        {
            const double t = ((double) k - centre) * cutoff;
            const double w = t / half;
            const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - w * w))) / i0b;
            const double sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (pi * t) / (pi * t);
            sum += x[(size_t) k] * sinc * window * cutoff;
        }

        y[n] = (float) sum;
    }

    return y;
}

void fft (std::vector<double>& re, std::vector<double>& im, bool inverse)
{
    const size_t n = re.size();

    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = 2.0 * pi / (double) len * (inverse ? 1.0 : -1.0);
        const double wr = std::cos (angle), wi = std::sin (angle);

        for (size_t i = 0; i < n; i += len)
        {
            double cr = 1.0, ci = 0.0;
            for (size_t k = 0; k < len / 2; ++k)
            {
                const size_t a = i + k, b = a + len / 2;
                const double xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr;        im[a] += xi;
                const double t = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = t;
            }
        }
    }

    if (inverse)
        for (size_t i = 0; i < n; ++i) { re[i] /= (double) n; im[i] /= (double) n; }
}

std::vector<float> resampleResponse (const std::vector<float>& taps, double fromRate, double toRate)
{
    auto out = resample (taps, fromRate, toRate);
    const auto scale = (float) (fromRate / toRate);
    for (auto& v : out) v *= scale;
    return out;
}

double responseDb (const std::vector<float>& taps, double rate, double hz)
{
    double re = 0.0, im = 0.0;
    const double w = 2.0 * pi * hz / rate;
    for (size_t k = 0; k < taps.size(); ++k)
    {
        re += taps[k] * std::cos (w * (double) k);
        im -= taps[k] * std::sin (w * (double) k);
    }
    return 10.0 * std::log10 (std::max (re * re + im * im, 1.0e-30));
}

//==============================================================================
Response makeResponse (const std::vector<float>& sent, const std::vector<float>& back, double rate, double outRate, int outTaps)
{
    Response r;
    r.rate = outRate;
    const size_t length = std::min (sent.size(), back.size());

    if (length < (size_t) (2.0 * rate))
    {
        r.error = "the recording is too short (it needs at least 2 seconds)";
        return r;
    }

    double sentPower = 0.0, backPower = 0.0;
    for (size_t i = 0; i < length; ++i) { sentPower += (double) sent[i] * sent[i]; backPower += (double) back[i] * back[i]; }

    if (sentPower <= 0.0 || backPower < sentPower * 1.0e-8)
    {
        r.error = "nothing came back (the return is silent)";
        return r;
    }

    // ---- 1. How late did it come back? Line the first part of both up against each other. ----
    size_t delay = 0;
    {
        const size_t part = std::min (length, (size_t) (12.0 * rate));
        const auto n = (size_t) nextPowerOfTwo ((double) part + rate);
        std::vector<double> ar (n, 0.0), ai (n, 0.0), br (n, 0.0), bi (n, 0.0);
        for (size_t i = 0; i < part; ++i) { ar[i] = sent[i]; br[i] = back[i]; }
        fft (ar, ai, false);
        fft (br, bi, false);
        for (size_t i = 0; i < n; ++i)
        {
            const double re = ar[i] * br[i] + ai[i] * bi[i], im = ar[i] * bi[i] - ai[i] * br[i];   // conj(A) * B
            ar[i] = re; ai[i] = im;
        }
        fft (ar, ai, true);
        double best = 0.0;
        for (size_t lag = 0; lag < (size_t) (0.5 * rate); ++lag)
            if (std::abs (ar[lag]) > best) { best = std::abs (ar[lag]); delay = lag; }
    }

    r.delayMs = (double) delay * 1000.0 / rate;

    // A little is kept in front, in case the loudest moment of the response is not its start.
    const size_t lead = (size_t) (0.002 * rate);
    const size_t shift = delay > lead ? delay - lead : 0;

    // ---- 2. The response at each frequency, averaged over the whole recording. ----
    const auto n = (size_t) nextPowerOfTwo (0.6 * rate);
    std::vector<double> window (n);
    for (size_t i = 0; i < n; ++i) window[i] = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) n);

    std::vector<double> sxx (n, 0.0), syy (n, 0.0), sxyRe (n, 0.0), sxyIm (n, 0.0), xr (n), xi (n), yr (n), yi (n);

    for (size_t start = 0; start + n + shift <= length; start += n / 2)
    {
        for (size_t i = 0; i < n; ++i)
        {
            xr[i] = sent[start + i] * window[i];           xi[i] = 0.0;
            yr[i] = back[start + shift + i] * window[i];   yi[i] = 0.0;
        }
        fft (xr, xi, false);
        fft (yr, yi, false);
        for (size_t i = 0; i < n; ++i)
        {
            sxx[i] += xr[i] * xr[i] + xi[i] * xi[i];
            syy[i] += yr[i] * yr[i] + yi[i] * yi[i];
            sxyRe[i] += xr[i] * yr[i] + xi[i] * yi[i];
            sxyIm[i] += xr[i] * yi[i] - xi[i] * yr[i];
        }
    }

    const auto bin = [&] (double hz) { return (size_t) std::clamp (hz * (double) n / rate, 1.0, (double) n / 2.0 - 1.0); };

    // Where the test sound had next to nothing (the very bottom, the very top) the answer
    // would be noise: there the response is eased to nothing instead.
    double inBand = 0.0;
    for (size_t i = bin (100.0); i < bin (5000.0); ++i) inBand += sxx[i];
    inBand /= (double) (bin (5000.0) - bin (100.0));
    const double floor = inBand * 1.0e-6;

    std::vector<double> hr (n, 0.0), hi (n, 0.0);
    double explained = 0.0, total = 0.0;
    const double topHz = std::min (20000.0, rate * 0.45);

    for (size_t i = 1; i < n / 2; ++i)
    {
        const double hz = (double) i * rate / (double) n;
        double fade = 1.0;
        if (hz < 30.0)              fade = hz <= 15.0 ? 0.0 : 0.5 - 0.5 * std::cos (pi * (hz - 15.0) / 15.0);
        else if (hz > topHz * 0.85) fade = hz >= topHz ? 0.0 : 0.5 + 0.5 * std::cos (pi * (hz - topHz * 0.85) / (topHz * 0.15));

        const double den = sxx[i] + floor;
        hr[i] = fade * sxyRe[i] / den;
        hi[i] = fade * sxyIm[i] / den;
        hr[n - i] = hr[i];
        hi[n - i] = -hi[i];

        if (hz > 60.0 && hz < 12000.0 && sxx[i] > floor * 100.0)
        {
            explained += (sxyRe[i] * sxyRe[i] + sxyIm[i] * sxyIm[i]) / sxx[i];
            total += syy[i];
        }
    }

    // 100 minus the part of what came back that no response file can explain (as a share of
    // its level, not its power: a tenth left over reads 90, which is about where it is heard).
    r.matchPercent = total > 0.0 ? 100.0 * (1.0 - std::sqrt (std::clamp (1.0 - explained / total, 0.0, 1.0))) : 0.0;

    // ---- 3. Back to a short sound: the response itself. ----
    fft (hr, hi, true);

    // Start just before the first moment it reaches a tenth of its loudest.
    double top = 0.0;
    for (size_t i = 0; i < n / 2; ++i) top = std::max (top, std::abs (hr[i]));
    size_t onset = 0;
    while (onset < n / 2 && std::abs (hr[onset]) < top * 0.1) ++onset;
    const size_t from = onset > 3 ? onset - 3 : 0;

    const auto rawLength = std::min (n / 2 - from, (size_t) std::ceil ((double) outTaps * rate / outRate));
    std::vector<float> raw (rawLength);
    for (size_t i = 0; i < rawLength; ++i) raw[i] = (float) hr[from + i];

    auto taps = resampleResponse (raw, rate, outRate);
    if ((int) taps.size() > outTaps) taps.resize ((size_t) outTaps);

    // The end eased to nothing, so stopping there makes no click of its own.
    const size_t tail = taps.size() / 8;
    for (size_t i = 0; i < tail; ++i)
        taps[taps.size() - 1 - i] *= (float) (0.5 - 0.5 * std::cos (pi * (double) i / (double) tail));

    // ---- 4. Level-neutral over the range a guitar or a voice lives in (100 Hz to 4 kHz). ----
    double power = 0.0;
    int points = 0;
    for (double hz = 100.0; hz <= 4000.0; hz *= std::pow (2.0, 1.0 / 6.0), ++points)
        power += std::pow (10.0, responseDb (taps, outRate, hz) / 10.0);
    power /= std::max (1, points);

    if (! (power > 1.0e-12) || ! std::isfinite (power))
    {
        r.error = "could not work out a response from this recording";
        return r;
    }

    r.levelDb = 10.0 * std::log10 (power);
    const auto scale = (float) (1.0 / std::sqrt (power));
    for (auto& t : taps) t *= scale;

    r.taps = std::move (taps);
    return r;
}

} // namespace gigrig::fx::ir
