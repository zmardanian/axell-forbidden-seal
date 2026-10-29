// Axell Forbidden Seal - DSP suite: the shipping engine (Source/Engine.h, JUCE-free), measured.
//   SealDspTests [--quick]   (quick: 48 kHz only, no CPU benchmark)
#include "../Source/Engine.h"
#include "../Source/Presets.h"

#include <axell/dsp/Filters.h>

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

using namespace afs;

static bool countAllocs = false;
static int allocCount = 0;
void* operator new (std::size_t n)
{
    if (countAllocs)
        ++allocCount;
    if (void* p = std::malloc (n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

static int checks = 0, fails = 0;
static void check (const std::string& name, double v, double lo, double hi, const char* unit)
{
    ++checks;
    const bool ok = v >= lo && v <= hi;
    fails += ok ? 0 : 1;
    std::printf ("%s  %-100s %11.4f %s  [%g .. %g]\n", ok ? "PASS" : "FAIL", name.c_str(), v, unit, lo, hi);
}
static std::string R (double fs) { char b[32]; std::snprintf (b, sizeof b, " @ %.1f kHz", fs / 1000.0); return b; }
static double db (double a) { return 20.0 * std::log10 (std::max (a, 1e-30)); }

using Buf = std::vector<std::vector<float>>;
// renders through a fresh engine, the latency removed (output aligned with the input)
static Buf render (double fs, const Params& p, Buf x, int block = 512, std::mt19937* rng = nullptr, bool count = false, int* latOut = nullptr)
{
    Engine e;
    e.setParams (p);
    e.prepare (fs, block);
    e.setParams (p);
    const int lat = e.latencySamples();
    if (latOut != nullptr)
        *latOut = lat;
    const size_t n = x[0].size();
    for (auto& c : x)
        c.resize (n + (size_t) lat, 0.0f);
    std::uniform_int_distribution<int> bl (1, 2048);
    for (size_t pos = 0; pos < n + (size_t) lat;)
    {
        const int m = (int) std::min<size_t> ((size_t) (rng ? bl (*rng) : block), n + (size_t) lat - pos);
        float* io[2] = { x[0].data() + pos, x[1].data() + pos };
        countAllocs = count;
        e.process (io, 2, m);
        countAllocs = false;
        pos += (size_t) m;
    }
    for (auto& c : x)
        c.erase (c.begin(), c.begin() + lat);
    return x;
}
static Buf sine (double fs, double f, double dbfs, double sec)
{
    Buf x (2, std::vector<float> ((size_t) (sec * fs)));
    const double a = std::pow (10.0, dbfs / 20.0);
    for (size_t i = 0; i < x[0].size(); ++i)
        x[0][i] = x[1][i] = (float) (a * std::sin (2.0 * kPi * f * (double) i / fs));
    return x;
}
// amplitude of the component at frequency f over the second half (a Blackman-windowed DFT at that bin)
static double amp (const std::vector<float>& y, double fs, double f)
{
    const size_t a = y.size() / 2, n = y.size() - a;
    std::complex<double> s = 0;
    double wsum = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.42 - 0.5 * std::cos (2.0 * kPi * i / (n - 1)) + 0.08 * std::cos (4.0 * kPi * i / (n - 1));
        wsum += w;
        s += w * (double) y[a + i] * std::polar (1.0, -2.0 * kPi * f * (double) (a + i) / fs);
    }
    return 2.0 * std::abs (s) / wsum;
}
// 7-term Blackman-Harris window: sidelobes below -180 dB, so a tone between two bins does not leak into the
// measurement (a Blackman window's -58 dB sidelobes read as "aliasing" at 44.1 kHz)
static double bh7 (size_t i, size_t n)
{
    static const double a[7] = { 0.27105140069342, 0.43329793923448, 0.21812299954311, 0.06592544638803, 0.01081174209837,
                                 0.00077658482522, 0.00001388721735 };
    double w = 0, sgn = 1;
    for (int k = 0; k < 7; ++k, sgn = -sgn)
        w += sgn * a[k] * std::cos (2.0 * kPi * k * (double) i / (double) (n - 1));
    return w;
}
// energy away from the harmonics of f0 in 20 Hz - 20 kHz re the fundamental (dB): aliasing and noise
static double strayDb (const std::vector<float>& y, double fs, double f0)
{
    // a plain DFT magnitude spectrum of the settled half (n = 2^k for speed via a radix-2 FFT)
    size_t n = 1;
    while (n * 2 <= y.size() / 2)
        n *= 2;
    std::vector<std::complex<double>> X (n);
    const size_t a = y.size() - n;
    for (size_t i = 0; i < n; ++i)
    {
        X[i] = bh7 (i, n) * (double) y[a + i];
    }
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (X[i], X[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto wl = std::polar (1.0, -2.0 * kPi / (double) len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w = 1;
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = X[i + k], v = X[i + k + len / 2] * w;
                X[i + k] = u + v;
                X[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    const double bin = fs / (double) n;
    double fund = 0, stray = 0;
    for (size_t k = 1; k < n / 2; ++k)
    {
        const double f = k * bin, p = std::norm (X[k]);
        const double h = f / f0, near = std::abs (h - std::round (h)) * f0;
        if (std::abs (f - f0) < 10 * bin)
            fund += p;
        else if (f > 20.0 && f < 20000.0 && near > 10 * bin)
            stray += p;
    }
    return 10.0 * std::log10 (stray / fund + 1e-30);
}
// a voice-like signal at -18 dBFS RMS (pulses through formants, fricative bursts, pauses)
static Buf voice (double fs, double sec)
{
    const size_t n = (size_t) (sec * fs);
    Buf x (2, std::vector<float> (n));
    std::mt19937 g (5);
    std::normal_distribution<double> nd;
    double s1[4] {}, s2[4] {}, ph = 0, gl = 0;
    const double F[4] = { 650, 1150, 2600, 6500 }, B[4] = { 110, 140, 240, 2500 };
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / fs, seg = std::fmod (t, 0.5);
        ph += (150.0 + 40.0 * std::sin (2.0 * kPi * 0.6 * t)) / fs;
        double pulse = 0;
        if (ph >= 1.0) { ph -= 1.0; pulse = 1.0; }
        gl = 0.96 * gl + pulse;
        double v = 0;
        const double env = seg < 0.32 ? std::min (1.0, std::min (seg, 0.32 - seg) / 0.02) : 0.0;
        const double fenv = seg > 0.36 && seg < 0.46 ? std::sin (kPi * (seg - 0.36) / 0.1) : 0.0;
        for (int k = 0; k < 4; ++k)
        {
            const double r = std::exp (-kPi * B[k] / fs), w = 2.0 * std::cos (2.0 * kPi * F[k] / fs) * r;
            const double in = k < 3 ? gl : nd (g);
            const double y = (1.0 - r) * in + w * s1[k] - r * r * s2[k];
            s2[k] = s1[k];
            s1[k] = y;
            v += k < 3 ? env * y * (k == 0 ? 1.0 : (k == 1 ? 0.6 : 0.3)) : 0.5 * fenv * y;
        }
        x[0][i] = x[1][i] = (float) v;
    }
    double e = 0;
    for (float v : x[0]) e += (double) v * v;
    const double gn = std::pow (10.0, -18.0 / 20.0) / std::sqrt (e / (double) n);
    for (auto& c : x)
        for (auto& v : c) v = (float) (v * gn);
    return x;
}
static double loud (const std::vector<float>& x, double fs)
{
    axell::KWeighting k;
    k.prepare (fs);
    double e = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double y = k.process (x[i], 0);
        if (i > (size_t) (0.25 * fs))
            e += y * y;
    }
    return 10.0 * std::log10 (e / (double) x.size() + 1e-30);
}
static Params style (int s, float drive, int q = 2)
{
    Params p;
    p.style = s;
    p.driveDb = drive;
    p.autoGain = false;
    p.quality = q;
    return p;
}

static void testLinearity (double fs, bool quick)
{
    for (int q : quick ? std::vector<int> { 2, 4 } : std::vector<int> { 0, 1, 2, 3, 4 })
    {
        double worst = 0;
        for (int s = 0; s < kNumStyles; ++s)
        {
            const auto y = render (fs, style (s, 0.0f, q), sine (fs, 1000.0, -50.0, 0.5));
            worst = std::max (worst, std::abs (db (amp (y[0], fs, 1000.0) / std::pow (10.0, -50.0 / 20.0))));
        }
        check ("every style, quality " + std::to_string (q) + ": small-signal gain at 1 kHz (largest deviation)" + R (fs), worst, 0.0, 0.15, "dB");
    }
}

static void testExactness (double fs)
{
    const auto x = voice (fs, 2.0);
    for (int q = 0; q < 5; ++q)
    {
        Params p = style (kTriode, 18.0f, q);
        p.mix = 0.0f;
        int lat = 0;
        const auto y = render (fs, p, x, 512, nullptr, false, &lat);
        double e = 0;
        for (size_t i = 0; i < x[0].size(); ++i)
            e = std::max (e, (double) std::abs (y[0][i] - x[0][i]));
        check ("MIX 0 %, quality " + std::to_string (q) + ": the input delayed by the reported latency (" + std::to_string (lat) + "), max error" + R (fs), e, 0.0, 0.0, "");
        if (q == 4)
            check ("LIVE: latency (samples)" + R (fs), lat, 1, 8, "");
    }
    // host bypass: after 10 ms the input delayed by the latency, bit for bit
    {
        Engine e;
        Params p = style (kPentode, 20.0f);
        e.setParams (p);
        e.prepare (fs, 256);
        e.setParams (p);
        e.setBypass (true);
        const int lat = e.latencySamples();
        Buf y = x;
        for (size_t pos = 0; pos < y[0].size(); pos += 256)
        {
            float* io[2] = { y[0].data() + pos, y[1].data() + pos };
            e.process (io, 2, (int) std::min<size_t> (256, y[0].size() - pos));
        }
        double err = 0;
        for (size_t i = (size_t) (0.02 * fs) + (size_t) lat; i < y[0].size(); ++i)
            err = std::max (err, (double) std::abs (y[0][i] - x[0][i - (size_t) lat]));
        check ("host bypass: the input delayed by the latency, bit for bit (after 10 ms)" + R (fs), err, 0.0, 0.0, "");
    }
}

static void testAutoGain (double fs)
{
    const auto x = voice (fs, 3.0);
    const double l0 = loud (x[0], fs);
    double worst = 0;
    int ws = 0, wd = 0;
    for (int s = 0; s < kNumStyles; ++s)
        for (float d : { 0.0f, 10.0f, 20.0f, 30.0f })
        {
            Params p = style (s, d);
            p.autoGain = true;
            const double l = loud (render (fs, p, x)[0], fs) - l0;
            if (std::abs (l) > std::abs (worst)) { worst = l; ws = s; wd = (int) d; }
        }
    check (std::string ("AUTO: loudness re the input, every style, DRIVE 0 - 30 dB (largest: ") + styleName (ws) + " +" + std::to_string (wd) + " dB)" + R (fs),
           worst, -0.75, 0.75, "dB");
}

static void testAliasing (double fs)
{
    // a brutal test: a 7.5 kHz tone 18 dB over full scale into the circuit. TAPE has memory (hysteresis: no
    // antiderivative anti-aliasing), so at Best it is held to -55 dB here (realistic material: below -120 dB, next test)
    for (int s = 0; s < kNumStyles; ++s)
    {
        const auto y = render (fs, style (s, 24.0f, 2), sine (fs, 7500.0, -6.0, 0.6));
        check (std::string (styleName (s)) + ", Best (8x): stray energy (aliasing), 7.5 kHz at -6 dBFS +24 dB" + R (fs), strayDb (y[0], fs, 7500.0),
               -400.0, s == kTape ? -55.0 : -60.0, "dB");
        const auto z = render (fs, style (s, 24.0f, 3), sine (fs, 7500.0, -6.0, 0.6));
        check (std::string (styleName (s)) + ", Extreme (16x): the same" + R (fs), strayDb (z[0], fs, 7500.0), -400.0, -90.0, "dB");
    }
    // a realistic loud vocal-range tone, default quality: nothing audible
    double worst = -400;
    for (int s = 0; s < kNumStyles; ++s)
        worst = std::max (worst, strayDb (render (fs, style (s, 18.0f, 2), sine (fs, 3000.0, -12.0, 0.6))[0], fs, 3000.0));
    check ("every style, Best (8x): stray energy, 3 kHz at -12 dBFS +18 dB (the worst style)" + R (fs), worst, -400.0, -80.0, "dB");
}

static void testHarmonics (double fs)
{
    auto h = [&] (int s, float d, float ch, int k) {
        Params p = style (s, d);
        p.character = ch;
        const auto y = render (fs, p, sine (fs, 1000.0, -18.0, 0.5));
        return db (amp (y[0], fs, 1000.0 * k) / amp (y[0], fs, 1000.0));
    };
    for (int s : { (int) kTape, (int) kPentode, (int) kIron })
        check (std::string (styleName (s)) + ": symmetric - 2nd harmonic re fundamental at +24 dB (odd only)" + R (fs), h (s, 24.0f, 50.0f, 2), -400.0, -80.0, "dB");
    check ("FOLD, OFFSET centred: symmetric - 2nd harmonic at +18 dB" + R (fs), h (kFold, 18.0f, 50.0f, 2), -400.0, -80.0, "dB");
    for (int s : { (int) kTriode, (int) kConsole, (int) kGermanium })
        check (std::string (styleName (s)) + ": even-rich - 2nd re 3rd harmonic at +6 dB" + R (fs), h (s, 6.0f, 50.0f, 2) - h (s, 6.0f, 50.0f, 3), 3.0, 400.0, "dB");
    check ("DIODE: ASYMMETRY 0 -> 100 raises the 2nd harmonic (+12 dB)" + R (fs), h (kDiode, 12.0f, 100.0f, 2) - h (kDiode, 12.0f, 0.0f, 2), 10.0, 400.0, "dB");
    check ("FOLD: OFFSET 50 -> 100 raises the 2nd harmonic (+12 dB)" + R (fs), h (kFold, 12.0f, 100.0f, 2) - h (kFold, 12.0f, 50.0f, 2), 40.0, 400.0, "dB");
    // THD grows with DRIVE
    int monotone = 0;
    for (int s = 0; s < kNumStyles; ++s)
    {
        double prev = -400;
        bool ok = true;
        for (float d : { 0.0f, 8.0f, 16.0f, 24.0f })
        {
            Params p = style (s, d);
            const auto y = render (fs, p, sine (fs, 1000.0, -18.0, 0.4));
            double hsum = 0;
            for (int k = 2; k <= 9; ++k)
                hsum += std::pow (amp (y[0], fs, 1000.0 * k), 2.0);
            const double thd = 10.0 * std::log10 (hsum + 1e-30) - db (amp (y[0], fs, 1000.0));
            ok = ok && thd > prev - 1.5;   // class AB (PENTODE): crossover distortion is largest at low levels
            prev = thd;
        }
        monotone += ok ? 1 : 0;
    }
    check ("every style: distortion grows with DRIVE (styles that do)" + R (fs), monotone, kNumStyles, kNumStyles, "");
}

static void testSafety (double fs)
{
    const auto x = voice (fs, 2.0);
    std::mt19937 rng (3);
    int silentBad = 0, nonFinite = 0;
    double blockDiff = 0;
    for (const auto& pr : factoryPresets())
    {
        Buf z (2, std::vector<float> ((size_t) (0.5 * fs), 0.0f));
        for (auto& c : render (fs, pr.p, z))
            for (float v : c)
                silentBad += v != 0.0f ? 1 : 0;
        Buf bad = x;
        for (size_t i = 1000; i < bad[0].size(); i += 4801)
            bad[0][i] = (i / 4801) % 2 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
        for (auto& c : render (fs, pr.p, bad))
            for (float v : c)
                nonFinite += std::isfinite (v) ? 0 : 1;
        const auto a = render (fs, pr.p, x, 512), b = render (fs, pr.p, x, 512, &rng);
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < x[0].size(); ++i)
                blockDiff = std::max (blockDiff, (double) std::abs (a[c][i] - b[c][i]));
    }
    check ("every preset: silence in -> silence out (non-zero samples)" + R (fs), silentBad, 0, 0, "");
    check ("every preset: NaN / Inf in -> finite output (non-finite samples)" + R (fs), nonFinite, 0, 0, "");
    check ("every preset: blocks of 512 vs random 1-2048 (max difference)" + R (fs), blockDiff, 0.0, 1.0e-6, "");
    // STYLE switched while playing: no click
    {
        Engine e;
        Params p = style (kTriode, 12.0f);
        e.setParams (p);
        e.prepare (fs, 256);
        e.setParams (p);
        Buf y = sine (fs, 220.0, -12.0, 1.0);
        double stepOut = 0;
        for (size_t pos = 0; pos < y[0].size(); pos += 256)
        {
            if (pos == (size_t) (0.5 * fs) / 256 * 256)
            {
                p.style = kFold;
                e.setParams (p);
            }
            float* io[2] = { y[0].data() + pos, y[1].data() + pos };
            e.process (io, 2, (int) std::min<size_t> (256, y[0].size() - pos));
        }
        for (size_t i = (size_t) (0.45 * fs); i < (size_t) (0.6 * fs); ++i)
            stepOut = std::max (stepOut, (double) std::abs (y[0][i] - y[0][i - 1]));
        const double sineStep = 2.0 * kPi * 220.0 / fs * std::pow (10.0, -12.0 / 20.0) * 4.0;   // generous: harmonics steepen it
        check ("STYLE switched while playing: largest sample step re 4x the sine's" + R (fs), stepOut / sineStep, 0.0, 1.0, "x");
    }
}

// 2.0: BANDS, ESS GUARD, DYNAMICS
static void testBands (double fs)
{
    // linear (DRIVE 0, a tiny level): the crossovers alone - (wet / dry) with BANDS equals (wet / dry) with one band,
    // in level and phase, at tones around the crossovers (the circuit's own response cancels out of the comparison)
    for (int nb : { 2, 3 })
    {
        double worstDb = 0, worstDeg = 0;
        for (double f : { 80.0, 200.0, 600.0, 1500.0, 3000.0, 5000.0 })
        {
            auto ratio = [&] (int bands) {
                Params p = style (kTriode, 0.0f);
                p.bands = bands;
                p.xLow = 200.0f;
                p.xHigh = 3000.0f;
                Params d = p;
                d.mix = 0.0f;
                const auto x = sine (fs, f, -60.0, 0.6);
                const auto w = render (fs, p, x), y = render (fs, d, x);
                auto ph = [&] (const std::vector<float>& v) {
                    const size_t a = v.size() / 2, n = v.size() - a;
                    std::complex<double> acc = 0;
                    for (size_t i = 0; i < n; ++i) acc += (double) v[a + i] * std::polar (1.0, -2.0 * kPi * f * (double) (a + i) / fs);
                    return acc;
                };
                return ph (w[0]) / ph (y[0]);
            };
            const auto r = ratio (nb) / ratio (1);
            worstDb = std::max (worstDb, std::abs (db (std::abs (r))));
            worstDeg = std::max (worstDeg, std::abs (std::arg (r)) * 180.0 / kPi);
        }
        check (std::to_string (nb) + " BANDS, linear: the crossovers re one band, wet re its dry, 80 Hz - 5 kHz (largest level difference)" + R (fs), worstDb, 0.0, 0.05, "dB");
        check (std::to_string (nb) + " BANDS, linear: the same, largest phase difference" + R (fs), worstDeg, 0.0, 1.0, "deg");
    }
    // a band at MIX 0 passes its range untouched: 3 bands, the high band clean - an 8 kHz tone keeps no harmonics
    // while the low band distorts a 100 Hz tone
    {
        Params p = style (kDiode, 24.0f);
        p.bands = 3;
        p.xLow = 300.0f;
        p.xHigh = 3000.0f;
        p.bandMix = { 100.0f, 100.0f, 0.0f };
        const auto hi = render (fs, p, sine (fs, 8000.0, -12.0, 0.5));
        const auto lo = render (fs, p, sine (fs, 100.0, -12.0, 0.5));
        check ("3 BANDS, high band MIX 0: an 8 kHz tone gets no 2nd / 3rd harmonic (worst)" + R (fs),
               std::max (db (amp (hi[0], fs, 16000.0) / amp (hi[0], fs, 8000.0)), fs > 50000 ? db (amp (hi[0], fs, 24000.0) / amp (hi[0], fs, 8000.0)) : -400.0), -400.0, -80.0, "dB");
        check ("3 BANDS, low band driven: a 100 Hz tone distorts (3rd harmonic)" + R (fs), db (amp (lo[0], fs, 300.0) / amp (lo[0], fs, 100.0)), -40.0, 0.0, "dB");
    }
    // per-band STYLE: the low band TAPE (odd), the mid band TRIODE (even) - 2nd harmonic of a 1 kHz tone rises
    {
        Params p = style (kTape, 12.0f);
        p.bands = 3;
        p.xLow = 400.0f;
        p.xHigh = 5000.0f;
        Params q = p;
        q.bandStyle = { -1, kTriode, -1 };
        const auto a = render (fs, p, sine (fs, 1000.0, -18.0, 0.5)), b = render (fs, q, sine (fs, 1000.0, -18.0, 0.5));
        check ("3 BANDS: mid band TRIODE instead of the main TAPE raises a 1 kHz tone's 2nd harmonic" + R (fs),
               db (amp (b[0], fs, 2000.0)) - db (amp (a[0], fs, 2000.0)), 40.0, 400.0, "dB");
    }
}

// a vowel / ess voice: 0.25 s vowels (harmonic, 160 Hz) and 0.12 s esses (4-10 kHz noise), with pauses
static Buf voiceEss (double fs, double sec, std::vector<int>& kind)
{
    Buf x (2, std::vector<float> ((size_t) (sec * fs), 0.0f));
    kind.assign (x[0].size(), 0);
    std::mt19937 g (7);
    std::normal_distribution<float> nd;
    double ph = 0;
    float b2 = 0;
    for (size_t i = 0; i < x[0].size(); ++i)
    {
        const double t = (double) i / fs, c = std::fmod (t, 0.6);
        float v = 0.0f;
        if (c < 0.25)
        {
            ph += 2.0 * kPi * 160.0 / fs;
            for (int h = 1; h <= 25; ++h)
                v += (float) (std::sin (h * ph) / h);
            v *= 0.12f;
            kind[i] = 1;
        }
        else if (c >= 0.3 && c < 0.42)
        {
            const float n = nd (g);
            v = 0.07f * (n - 0.9f * b2);
            b2 = n;
            kind[i] = 2;
        }
        x[0][i] = x[1][i] = v;
    }
    return x;
}

static void testEssGuard (double fs)
{
    std::vector<int> kind;
    const auto x = voiceEss (fs, 4.0, kind);
    Params p = style (kTriode, 24.0f);
    p.autoGain = false;
    Params g = p;
    g.essGuard = 100.0f;
    // the latency grows by the ess look-ahead
    int l0 = 0, l1 = 0;
    {
        Engine e0, e1;
        e0.setParams (p); e0.prepare (fs, 512);
        e1.setParams (g); e1.prepare (fs, 512);
        l0 = e0.latencySamples();
        l1 = e1.latencySamples();
        check ("ESS GUARD on: latency + the ess look-ahead (" + std::to_string (e1.essLookAhead()) + " samples)" + R (fs), l1 - l0 - e1.essLookAhead(), 0, 0, "");
    }
    // the decision forced on the esses (the network itself is tested in axell-core and on real voices)
    auto run = [&] (const Params& q, bool force) {
        Engine e;
        e.setParams (q);
        e.prepare (fs, 64);
        e.setParams (q);
        const int lat = e.latencySamples(), la = e.essLookAhead();
        Buf y = x;
        for (auto& c : y) c.resize (y[0].size() + (size_t) lat, 0.0f);
        for (size_t pos = 0; pos < y[0].size(); pos += 64)
        {
            if (force)
            {
                const long at = (long) pos + 32 - (q.essGuard > 0.0f ? (long) e.essLookAhead() : 0);   // the sample leaving the look-ahead
                e.forceEssDecision (at >= 0 && (size_t) at < kind.size() && kind[(size_t) at] == 2 ? 1 : 0);
            }
            float* io[2] = { y[0].data() + pos, y[1].data() + pos };
            e.process (io, 2, (int) std::min<size_t> (64, y[0].size() - pos));
        }
        for (auto& c : y) c.erase (c.begin(), c.begin() + lat);
        (void) la;
        return y;
    };
    const auto off = run (p, false), on = run (g, true);
    // the esses' balance to the vowels: as in the input with the guard (saturation without it pushes the esses up -
    // the harshness the guard is for)
    auto segEnergy = [&] (const Buf& y, int k) {
        double e = 0;
        for (size_t i = (size_t) (1.2 * fs); i < y[0].size(); ++i)
            if (kind[i] == k) e += (double) y[0][i] * y[0][i];
        return 10.0 * std::log10 (e + 1e-30);
    };
    const double balIn = segEnergy (x, 2) - segEnergy (x, 1);
    check ("ESS GUARD 100 %: esses re vowels as in the input (balance change)" + R (fs), segEnergy (on, 2) - segEnergy (on, 1) - balIn, -1.0, 1.0, "dB");
    check ("ESS GUARD: the vowels untouched (re off)" + R (fs), segEnergy (on, 1) - segEnergy (off, 1), -0.2, 0.2, "dB");
    // the esses' distortion: what is left after the best level match to the input, re the input
    auto distortion = [&] (const Buf& y) {
        double num = 0, den = 0, cross = 0;
        for (size_t i = (size_t) (1.2 * fs); i < y[0].size(); ++i)
            if (kind[i] == 2) { cross += (double) y[0][i] * x[0][i]; den += (double) x[0][i] * x[0][i]; }
        const double gm = cross / den;
        for (size_t i = (size_t) (1.2 * fs); i < y[0].size(); ++i)
            if (kind[i] == 2) { const double e = y[0][i] - gm * x[0][i]; num += e * e; }
        return 10.0 * std::log10 (num / den / (gm * gm) + 1e-30);
    };
    check ("ESS GUARD 100 %: the esses' distortion (residual re the input) re ESS GUARD off" + R (fs), distortion (on) - distortion (off), -400.0, -15.0, "dB");
}

static void testDynamics (double fs)
{
    // a tone stepping between -30 and -6 dBFS: positive DYNAMICS distorts the loud part more, negative the quiet part
    Buf x (2, std::vector<float> ((size_t) (2.0 * fs)));
    for (size_t i = 0; i < x[0].size(); ++i)
    {
        const double t = (double) i / fs, a = std::fmod (t, 1.0) < 0.5 ? std::pow (10.0, -30.0 / 20.0) : std::pow (10.0, -6.0 / 20.0);
        x[0][i] = x[1][i] = (float) (a * std::sin (2.0 * kPi * 500.0 * t));
    }
    auto thd = [&] (const Params& p, double t0) {
        const auto y = render (fs, p, x);
        std::vector<float> seg (y[0].begin() + (long) (t0 * fs), y[0].begin() + (long) ((t0 + 0.4) * fs));
        return db (amp (seg, fs, 1500.0) / amp (seg, fs, 500.0));
    };
    Params p = style (kTape, 12.0f);
    Params up = p, dn = p;
    up.dynamics = 100.0f;
    dn.dynamics = -100.0f;
    check ("DYNAMICS +100: the loud part distorts more (3rd harmonic, re 0)" + R (fs), thd (up, 1.55) - thd (p, 1.55), 3.0, 400.0, "dB");
    check ("DYNAMICS -100: the quiet part distorts more (3rd harmonic, re 0)" + R (fs), thd (dn, 1.05) - thd (p, 1.05), 3.0, 400.0, "dB");
}

static void testAllocAndCpu (bool quick)
{
    const double fs = 48000.0;
    const auto x = voice (fs, 2.0);
    std::mt19937 rng (5);
    allocCount = 0;
    for (const auto& pr : factoryPresets())
        render (fs, pr.p, x, 512, &rng, true);
    check ("no memory allocation in process() (every preset, random blocks)", allocCount, 0, 0, "allocs");
    if (quick)
        return;
    const auto y = voice (fs, 10.0);
    for (int s = 0; s < kNumStyles; ++s)
    {
        const auto a = std::chrono::steady_clock::now();
        render (fs, style (s, 12.0f, 2), y, 128);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count();
        check (std::string ("CPU, stereo @ 48 kHz, Best (8x), 128-sample blocks, ") + styleName (s) + " (% of one core)", 100.0 * sec / 10.0, 0.0, 25.0, "%");
    }
    for (const auto& pr : factoryPresets())
        if (std::string (pr.name) == "Vocal: Split Warmth")
        {
            const auto a0 = std::chrono::steady_clock::now();
            render (fs, pr.p, y, 128);
            const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - a0).count();
            check ("CPU, 3 BANDS + ESS GUARD (Vocal: Split Warmth), Best (8x), 128-sample blocks (% of one core)", 100.0 * sec / 10.0, 0.0, 40.0, "%");
        }
}

int main (int argc, char** argv)
{
    const bool quick = argc > 1 && std::strcmp (argv[1], "--quick") == 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (double fs : quick ? std::vector<double> { 48000.0 } : std::vector<double> { 44100.0, 48000.0, 96000.0 })
    {
        testLinearity (fs, quick);
        testExactness (fs);
        testAutoGain (fs);
        testAliasing (fs);
        testHarmonics (fs);
        testSafety (fs);
        testBands (fs);
        testEssGuard (fs);
        testDynamics (fs);
    }
    testAllocAndCpu (quick);
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    if (fails == 0)
        std::printf ("ALL PASS: %d of %d checks passed (%.1f s)\n", checks, checks, sec);
    else
        std::printf ("FAILED: %d of %d checks (%.1f s)\n", fails, checks, sec);
    return fails;
}
