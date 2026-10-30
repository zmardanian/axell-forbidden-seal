#pragma once
// Axell Forbidden Seal - the engine (framework independent, no JUCE).
//
// 4.0 - DECOUPLED SATURATION. Every circuit gives only what it ADDS (its output minus its own small-signal response):
// the voice itself never passes a circuit, so its highs and mids stay exactly as recorded, and where the added colour
// lands is chosen on purpose:
//   pre  = LOW CUT (12 dB/oct) + THUMP -> TONE (tilt)                       (what drives the circuit)
//   c    = [ oversampled: STYLE circuit's colour of pre x DRIVE ] / DRIVE    (per band; ESS GUARD fades it on esses)
//   c    = squash + harmonics: the part of c that is a gain of the voice (its local least-squares projection on the
//          band's signal, 5 ms - Bussgang: for a memoryless circuit one gain for every frequency) and the rest
//   c'   = SQUASH x squash (flat: the voice's tone kept) + FOCUS ( harmonics ): the new harmonics kept out of the
//          vowels' range (~1 - 1.5 kHz: the box) and softened above 7 kHz (fizz); FOCUS 0 = the classic circuit
//   wet  = VOICING ( pre + c ) -> HIGH CUT -> AIR -> AUTO;   out = MIX (dry / wet) -> OUTPUT
// At FOCUS 0 and VOICING off this is exactly the classic circuit (minus its linear filters); at MIX 0 the dry.
//
// Level reference: u = 1 inside a model is 0 dBFS at DRIVE 0 dB (a vocal at -18 dBFS RMS meets the curve gently).
// Every model has unity gain for small signals (it is a pure filter below its knee), so DRIVE pushes the signal into
// the nonlinearity and AUTO takes the level back (a measured loudness table, AutoGain.h).
//
// The models are our own designs from circuit physics (references in THIRD_PARTY_NOTICES.md):
//   TAPE      Jiles-Atherton magnetic hysteresis between record pre-emphasis and playback de-emphasis; CHARACTER =
//             tape speed 7.5 .. 30 ips (emphasis time constant, head bump, gap loss).
//   TRIODE    12AX7-type common-cathode stage (Koren's triode equations on the load line), grid conduction and the
//             bias shift it causes; CHARACTER = cathode bias, cold (more even harmonics, earlier break-up) to hot.
//   PENTODE   push-pull pentode output stage (Koren's pentode equations, two tubes in antiphase: odd harmonics) with
//             supply sag; CHARACTER = bias from class A to cold class B (crossover grit).
//   CONSOLE   input transformer (low, loud notes saturate first) into a single-ended class-A transistor stage;
//             CHARACTER = transformer size.
//   GERMANIUM a germanium common-emitter stage with low headroom whose bias starves on loud passages (sputter);
//             CHARACTER = starve.
//   IRON      transformer core saturation alone: weight and low-end compression; CHARACTER = core size.
//   DIODE     a diode clipper (series resistor, diodes to ground), memoryless, antiderivative anti-aliasing (ADAA);
//             CHARACTER = asymmetry (one diode against two in series: even harmonics).
//   FOLD      a wavefolder (sine folding), analytic ADAA; CHARACTER = offset (even harmonics, ring-like).
#include <axell/dsp/Math.h>
#include <axell/dsp/Filters.h>
#include <axell/dsp/Loudness.h>
#include <axell/dsp/SibilanceDetector.h>
#include <axell/dsp/LatencyHandshake.h>
#include <axell/dsp/Oversampler.h>
#include "IirOversampler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstdint>
#include <vector>
#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace afs
{
using axell::kPi;
constexpr int kMaxCh = 2;

enum Style : int { kTape = 0, kTriode, kPentode, kConsole, kGermanium, kIron, kDiode, kFold, kNumStyles };
inline const char* styleName (int s)
{
    static const char* n[] = { "TAPE", "TRIODE", "PENTODE", "CONSOLE", "GERMANIUM", "IRON", "DIODE", "FOLD" };
    return n[std::clamp (s, 0, kNumStyles - 1)];
}
// what CHARACTER does in each style (the knob's label)
inline const char* characterName (int s)
{
    static const char* n[] = { "SPEED", "BIAS", "CLASS", "IRON SIZE", "STARVE", "CORE", "ASYMMETRY", "OFFSET" };
    return n[std::clamp (s, 0, kNumStyles - 1)];
}

struct Params
{
    int style = kTriode;
    float driveDb = 6.0f;        // 0 .. 30
    bool punish = false;         // + 20 dB
    float character = 50.0f;     // 0 .. 100
    float lowCutHz = 20.0f;      // 20 = off .. 1000
    bool thump = false;
    float tone = 0.0f;           // -100 dark .. +100 bright
    float highCutHz = 20000.0f;  // 20000 = off .. 1000
    bool steep = false;
    float airDb = 0.0f;          // 0 .. 9
    bool autoGain = true;
    float mix = 100.0f;          // 0 .. 100
    float outputDb = 0.0f;       // -24 .. 12
    int quality = 2;             // 0: 2x, 1: 4x, 2: 8x (default), 3: 16x oversampling, 4: LIVE (4x, minimum-phase filters, ~4 samples)
    // 2.0
    int bands = 1;               // 1 (one band: the whole signal), 2 or 3
    float xLow = 200.0f;         // crossovers (Hz)
    float xHigh = 3000.0f;
    std::array<int, 3> bandStyle { -1, -1, -1 };          // -1 = the main STYLE
    std::array<float, 3> bandDrive { 0.0f, 0.0f, 0.0f };  // dB on top of DRIVE (-18 .. 18)
    std::array<float, 3> bandMix { 100.0f, 100.0f, 100.0f };
    float dynamics = 0.0f;       // -100 (quiet parts driven harder) .. +100 (loud parts driven harder)
    // 3.0
    float inputDb = 0.0f;        // INPUT: the level into the circuits (-24 .. 24; LEARN sets it so a take sits at the designed level)
    float essGuard = 0.0f;       // 0 = off .. 100 %: how much of each ess stays out of the saturation (the suite's sibilance network)
    // 4.0
    float focus = 100.0f;        // 0 .. 100 %: the colour kept out of the vowels' range (the box) and the fizz band, put into weight and bite
    bool voicing = true;         // the seal's tone: a touch of weight and air, the honk range eased (off: the voice exactly as it came in)
};

//==============================================================================
// biquad, transposed direct form II, double precision
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<double, kMaxCh> z1 {}, z2 {};
    bool active = true;

    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) noexcept
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }
    static double w (double fs, double f) noexcept { return 2.0 * kPi * std::min (f, 0.49 * fs) / fs; }
    void lowPass (double fs, double f, double q)
    {
        const double w0 = w (fs, f), c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highPass (double fs, double f, double q)
    {
        const double w0 = w (fs, f), c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void onePoleLow (double fs, double f)
    {
        const double k = std::tan (0.5 * w (fs, f));
        set (k, k, 0, 1 + k, k - 1, 0);
    }
    void allPass (double fs, double f, double q)
    {
        const double w0 = w (fs, f), c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
        set (1 - al, -2 * c, 1 + al, 1 + al, -2 * c, 1 - al);
    }
    void peak (double fs, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w0 = w (fs, f), c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void shelf (double fs, double f, double db, bool high)
    {
        const double A = std::pow (10.0, db / 40.0), w0 = w (fs, f), c = std::cos (w0), s = std::sin (w0);
        const double al = s / 2.0 * std::sqrt (2.0), sq = 2.0 * std::sqrt (A) * al;
        if (high)
            set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                 (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
        else
            set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                 (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    double process (double x, int ch) noexcept
    {
        const double y = b0 * x + z1[(size_t) ch];
        z1[(size_t) ch] = b1 * x - a1 * y + z2[(size_t) ch];
        z2[(size_t) ch] = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1.fill (0.0); z2.fill (0.0); }
};

// lookup table with linear interpolation over [-r, r]; outside, the end slopes continue. f(0) is removed, so
// silence stays exactly 0. With `integral`, a second table holds the antiderivative (for ADAA).
struct Table
{
    std::vector<float> v;
    std::vector<double> F;   // antiderivative, F(0) = 0
    float r = 1, scale = 1, mid = 0, slopeLo = 0, slopeHi = 0;
    template <typename Fn>
    void build (Fn f, float range, int n, bool integral = false)
    {
        n |= 1;
        r = range;
        v.resize ((size_t) n);
        const double f0 = f (0.0);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (float) (f (-r + 2.0 * r * i / (n - 1)) - f0);
        v[(size_t) n / 2] = 0.0f;
        mid = (float) (n / 2);
        scale = (float) (n - 1) / (2.0f * r);
        slopeLo = (v[1] - v[0]) * scale;
        slopeHi = (v[(size_t) n - 1] - v[(size_t) n - 2]) * scale;
        if (integral)
        {
            F.assign ((size_t) n, 0.0);
            const double h = 2.0 * r / (n - 1);
            for (int i = n / 2 + 1; i < n; ++i)
                F[(size_t) i] = F[(size_t) i - 1] + 0.5 * h * (v[(size_t) i] + v[(size_t) i - 1]);
            for (int i = n / 2 - 1; i >= 0; --i)
                F[(size_t) i] = F[(size_t) i + 1] - 0.5 * h * (v[(size_t) i] + v[(size_t) i + 1]);
        }
    }
    float operator() (float x) const noexcept
    {
        if (x <= -r)
            return v.front() + (x + r) * slopeLo;
        if (x >= r)
            return v.back() + (x - r) * slopeHi;
        const float p = mid + x * scale;
        const int i = std::min ((int) p, (int) v.size() - 2);
        const float t = p - (float) i;
        return v[(size_t) i] + t * (v[(size_t) i + 1] - v[(size_t) i]);
    }
    // antiderivative: exact integral of the piecewise-linear table (and of the linear continuation outside)
    double integral (double x) const noexcept
    {
        const int n = (int) v.size();
        if (x <= -r)
        {
            const double d = x + r;
            return F.front() + d * v.front() + 0.5 * d * d * slopeLo;
        }
        if (x >= r)
        {
            const double d = x - r;
            return F.back() + d * v.back() + 0.5 * d * d * slopeHi;
        }
        const double p = mid + x * scale;
        const int i = std::min ((int) p, n - 2);
        const double t = p - i, h = 1.0 / scale;
        const double a = v[(size_t) i], b = v[(size_t) i + 1];
        return F[(size_t) i] + h * (a * t + 0.5 * (b - a) * t * t);
    }
};

// first-order antiderivative anti-aliasing of a memoryless curve: y = (F(x) - F(x1)) / (x - x1); where the step is
// tiny, the curve at the midpoint
template <typename Fn, typename AntiFn>
inline double adaa (double x, double& x1, double& F1, Fn f, AntiFn F) noexcept
{
    const double Fx = F (x);
    const double d = x - x1;
    const double y = std::abs (d) > 1.0e-5 ? (Fx - F1) / d : f (0.5 * (x + x1));
    x1 = x;
    F1 = Fx;
    return y;
}

//==============================================================================
// L(x) = coth x - 1/x and L'(x) from one exponential (the Langevin function of the Jiles-Atherton model)
inline void langevinBoth (double x, double& l, double& dl) noexcept
{
    const double ax = std::abs (x);
    if (ax < 1.0e-2)
    {
        const double x2 = x * x;
        l = x * (1.0 / 3.0 - x2 / 45.0);
        dl = 1.0 / 3.0 - x2 / 15.0;
        return;
    }
    if (ax > 18.0)
    {
        l = (x > 0 ? 1.0 : -1.0) - 1.0 / x;
        dl = 1.0 / (x * x);
        return;
    }
    const double e = std::exp (-2.0 * ax), om = 1.0 - e;
    l = (x > 0 ? 1.0 : -1.0) * (1.0 + e) / om - 1.0 / x;
    dl = 1.0 / (x * x) - 4.0 * e / (om * om);
}

// TAPE ========================================================================================================
class TapeModel
{
public:
    static constexpr double kA = 1.0, kK = 0.2, kC = 0.995, kHd = 2.2, kClamp = 1.5, kGate = 0.01;

    void prepare (double fsOs)
    {
        fs = fsOs;
        std::call_once (tabOnce, [] {
            tabN = (int) std::lround (2.0 * kTabMax / kTabStep) + 1;
            tab.resize (2 * (size_t) tabN);
            for (int i = 0; i < tabN; ++i)
            {
                double l, d;
                langevinBoth ((-kTabMax + i * kTabStep) / kA, l, d);
                tab[2 * (size_t) i] = l;
                tab[2 * (size_t) i + 1] = d / kA;
            }
        });
        substeps = std::max (1, (int) std::ceil (768000.0 / fs - 1.0e-9));
        setCharacter (0.5, true);
        // the small-signal slope of the loop, measured once (a -60 dBFS sine), so the model has unity gain
        norm = 1.0;
        reset();
        double peakM = 0.0;
        const double f = 1000.0, amp = 1.0e-3;
        const int per = (int) (fs / f);
        for (int i = 0; i < per * 4; ++i)
        {
            const double m = step (amp * kHd * std::sin (2.0 * kPi * f * i / fs), 0);
            if (i > per * 2)
                peakM = std::max (peakM, std::abs (m));
        }
        norm = peakM > 0.0 ? amp / peakM : 1.0;
        reset();
    }
    // speed 7.5 .. 30 ips (log): record emphasis 70 -> 17.5 us, head bump 30 -> 120 Hz, gap loss far above the band
    void setCharacter (double c, bool force = false)
    {
        c = std::clamp (c, 0.0, 1.0);
        if (! force && std::abs (c - lastC) < 1.0e-4)
            return;
        lastC = c;
        const double ips = 7.5 * std::pow (4.0, c);
        const double tau = 70.0e-6 * 7.5 / ips;
        // pre-emphasis (1 + s tau) / (1 + s 0.4 tau), de-emphasis the exact inverse
        analog (pre, 1.0, tau, 1.0, 0.4 * tau);
        analog (de, 1.0, 0.4 * tau, 1.0, tau);
        bump.peak (fs, 4.0 * ips, 1.1, 2.5);
        gap.lowPass (fs, std::min (0.45 * fs, 1400.0 * ips), 0.6);
    }
    void reset() noexcept
    {
        pre.reset(); de.reset(); bump.reset(); gap.reset();
        M.fill (0.0); H.fill (0.0);
    }
    // 4.0: in place, the COLOUR only - what the magnetic loop adds to the emphasised signal, de-emphasised (the
    // voice itself never passes the model)
    void process (float* x, int n, int ch) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double e = pre.process ((double) x[i], ch);
            x[i] = (float) de.process (step (e * kHd, ch) * norm - e, ch);
        }
    }

private:
    static constexpr double kTabMax = 64.0, kTabStep = 0.005, kMaxStep = 0.05;
    void analog (Biquad& b, double B0, double B1, double A0, double A1)   // first order, bilinear
    {
        const double K = 2.0 * fs;
        b.set (B0 + B1 * K, B0 - B1 * K, 0.0, A0 + A1 * K, A0 - A1 * K, 0.0);
    }
    void lookup (double h, double& man, double& dl) const noexcept
    {
        const double x = std::clamp (h, -kTabMax, kTabMax) / kTabStep + kTabMax / kTabStep;
        const int i = std::min ((int) x, tabN - 2);
        const double t = x - i;
        const double* q = tab.data() + 2 * i;
        man = q[0] + t * (q[2] - q[0]);
        dl = q[1] + t * (q[3] - q[1]);
    }
    double dMdH (double h, double m, double delta) const noexcept
    {
        double man, dl;
        lookup (h, man, dl);
        const double dm = man - m;
        const double x = delta * dm / kGate;
        const double gate = 0.5 * (1.0 + x / (1.0 + std::abs (x)));
        return std::max (0.0, gate * dm * delta * (1.0 / kK) + kC * dl);
    }
    double step (double h, int ch) noexcept
    {
        auto& m = M[(size_t) ch];
        double& h0 = H[(size_t) ch];
        // the solver's step follows the field's move: a big swing in one sample (a loud, high note driven hard) is
        // cut into steps of at most kMaxStep, so the integration never overshoots the loop (it went unstable and
        // produced sub-harmonics at extreme drive); at least `substeps` (>= 768 kHz) as before
        const int steps = std::clamp ((int) std::ceil (std::abs (h - h0) / kMaxStep), substeps, 256);
        const double dh = (h - h0) / steps;
        if (std::abs (dh) > 0.0)
        {
            const double d = dh > 0.0 ? 1.0 : -1.0;
            double ha = h0;
            for (int s = 0; s < steps; ++s)
            {
                const double hb = ha + dh;
                const double k1 = dMdH (ha, m, d);
                const double k2 = dMdH (hb, m + dh * k1, d);
                m += 0.5 * dh * (k1 + k2);
                ha = hb;
            }
            m = std::clamp (m, -kClamp, kClamp);
        }
        h0 = h;
        return m;
    }
    Biquad pre, de, bump, gap;
    std::array<double, kMaxCh> M {}, H {};
    double fs = 192000.0, norm = 1.0, lastC = -1.0;
    int substeps = 1;
    inline static int tabN = 0;
    inline static std::vector<double> tab;
    inline static std::once_flag tabOnce;
};

// TRIODE ======================================================================================================
class TriodeModel
{
public:
    // Koren triode (12AX7-like): mu 100, Ex 1.4, Kg1 1060, Kp 600, Kvb 300; B+ 250 V, plate load 100k
    static constexpr double kMu = 100.0, kEx = 1.4, kKg1 = 1060.0, kKp = 600.0, kKvb = 300.0, kBplus = 250.0, kRa = 100.0e3;
    static constexpr double kVin = 2.4, kGridV = 0.25, kChargeMs = 2.0, kReleaseMs = 120.0, kShift = 0.35;
    static constexpr int kBiases = 9;

    static double plateCurrent (double vgk, double vp) noexcept
    {
        const double e = kKp * (1.0 / kMu + vgk / std::sqrt (kKvb + vp * vp));
        const double soft = e > 30.0 ? e : std::log1p (std::exp (e));
        const double e1 = vp / kKp * soft;
        return e1 > 0.0 ? 2.0 * std::pow (e1, kEx) / kKg1 : 0.0;
    }
    static double plateVoltage (double vgk) noexcept
    {
        double lo = 0.0, hi = kBplus;
        for (int it = 0; it < 70; ++it)
        {
            const double vp = 0.5 * (lo + hi);
            (vp + kRa * plateCurrent (vgk, vp) > kBplus ? hi : lo) = vp;
        }
        return 0.5 * (lo + hi);
    }

    void prepare (double fsOs)
    {
        T = 1.0 / fsOs;
        std::call_once (curvesOnce, [] { for (int b = 0; b < kBiases; ++b)
            {
                // cathode resistor 3.3k (cold) .. 680 (hot), log steps; the operating point from the self-bias loop
                const double rk = 3300.0 * std::pow (680.0 / 3300.0, b / (double) (kBiases - 1));
                double vk = 1.0;
                for (int it = 0; it < 100; ++it)
                    vk = 0.5 * vk + 0.5 * rk * plateCurrent (-vk, plateVoltage (-vk));
                const double bias = -vk, vp0 = plateVoltage (bias), h = 1.0e-4;
                const double g0 = (plateVoltage (bias - h) - plateVoltage (bias + h)) / (2.0 * h);
                biasV[(size_t) b] = bias;
                curves[(size_t) b].build ([=] (double u)
                                          {
                                              double vg = bias + u * kVin;
                                              if (vg > 0.0)
                                                  vg = kGridV * std::tanh (vg / kGridV);
                                              return (vp0 - plateVoltage (vg)) / (g0 * kVin);
                                          },
                                          (float) (12.0 / kVin), 4097, true);
            } });
        lp.lowPass (fsOs, 35000.0, 0.5);
        chargeK = 1.0 - std::exp (-T / (kChargeMs * 0.001));
        relK = 1.0 - std::exp (-T / (kReleaseMs * 0.001));
        dcK = 1.0 - std::exp (-2.0 * kPi * 3.0 * T);
        reset();
    }
    void setCharacter (double c) noexcept
    {
        const double p = std::clamp (c, 0.0, 1.0) * (kBiases - 1);
        lo = std::min ((int) p, kBiases - 2);
        frac = (float) (p - lo);
    }
    void reset() noexcept { shift.fill (0.0); dc.fill (0.0); lp.reset(); x1.fill (0.0); F1.fill (0.0); in1.fill (0.0); }
    // 4.0: in place, the COLOUR only (the stage's output minus its small-signal response; ADAA's is the two-sample mean)
    void process (float* x, int n, int ch) noexcept
    {
        double& s = shift[(size_t) ch];
        double& w = in1[(size_t) ch];
        double& d = dc[(size_t) ch];
        const auto& A = curves[(size_t) lo];
        const auto& B = curves[(size_t) lo + 1];
        const double fr = frac;
        const double bias = biasV[(size_t) lo] + fr * (biasV[(size_t) lo + 1] - biasV[(size_t) lo]);
        auto f = [&] (double u) { return (1.0 - fr) * A ((float) u) + fr * B ((float) u); };
        auto F = [&] (double u) { return (1.0 - fr) * A.integral (u) + fr * B.integral (u); };
        double& p = x1[(size_t) ch];
        double& q = F1[(size_t) ch];
        q = F (p);   // the curve may have moved (CHARACTER): restart its antiderivative from the last input
        for (int i = 0; i < n; ++i)
        {
            const double u = x[i] - s / kVin;
            const double cond = std::max (0.0, bias + u * kVin);   // grid conduction charges the coupling capacitor
            s += chargeK * kShift * cond - relK * s;
            const double c = lp.process (adaa (u, p, q, f, F) - 0.5 * (x[i] + w), ch);
            w = x[i];
            d += dcK * (c - d);
            x[i] = (float) (c - d);
        }
    }

private:
    inline static std::array<Table, kBiases> curves;
    inline static std::array<double, kBiases> biasV {};
    inline static std::once_flag curvesOnce;
    Biquad lp;
    std::array<double, kMaxCh> shift {}, dc {}, x1 {}, F1 {}, in1 {};
    double T = 1.0 / 192000.0, chargeK = 0, relK = 0, dcK = 0;
    int lo = 4;
    float frac = 0.0f;
};

// PENTODE (push-pull) ===========================================================================================
class PentodeModel
{
public:
    // Koren pentode (EL84-like power pentode): mu 19.5, Ex 1.35, Kg1 1180, Kp 152, Kvb 4.5; screen 250 V
    static constexpr double kMu = 19.5, kEx = 1.35, kKg1 = 1180.0, kKp = 152.0, kKvb = 4.5, kVg2 = 250.0, kVp = 250.0;
    static constexpr double kVin = 22.0, kGridV = 3.0;   // grid conduction rounds the clip (a soft knee, not a wall)
    static constexpr int kBiases = 9;

    static double plateCurrent (double vg1) noexcept
    {
        const double e = kKp * (1.0 / kMu + vg1 / kVg2);
        const double soft = e > 30.0 ? e : std::log1p (std::exp (e));
        const double e1 = kVg2 / kKp * soft;
        return e1 > 0.0 ? std::pow (e1, kEx) / kKg1 * 2.0 * std::atan (kVp / kKvb) : 0.0;
    }
    void prepare (double fsOs)
    {
        T = 1.0 / fsOs;
        std::call_once (curvesOnce, [] { for (int b = 0; b < kBiases; ++b)
            {
                // grid bias -7 V (class A, both tubes always conduct) .. -15 V (cold class B: a crossover region)
                const double bias = -7.0 - 8.0 * b / (double) (kBiases - 1);
                auto grid = [] (double vg) { return vg > 0.0 ? kGridV * std::tanh (vg / kGridV) : vg; };
                auto out = [=] (double u) { return plateCurrent (grid (bias + u * kVin)) - plateCurrent (grid (bias - u * kVin)); };
                // normalise on the gain at a moderate swing (class B has almost none at the very smallest)
                const double g = out (0.05) / 0.05;
                curves[(size_t) b].build ([=] (double u) { return out (u) / g; }, 4.0f, 4097, true);
            } });
        sagA = 1.0 - std::exp (-T / 0.004);
        sagR = 1.0 - std::exp (-T / 0.180);
        lp.lowPass (fsOs, 28000.0, 0.6);
        reset();
    }
    void setCharacter (double c) noexcept
    {
        const double p = std::clamp (c, 0.0, 1.0) * (kBiases - 1);
        lo = std::min ((int) p, kBiases - 2);
        frac = (float) (p - lo);
    }
    void reset() noexcept { sag.fill (0.0); lp.reset(); x1.fill (0.0); F1.fill (0.0); in1.fill (0.0); }
    // 4.0: in place, the COLOUR only
    void process (float* x, int n, int ch) noexcept
    {
        double& s = sag[(size_t) ch];
        double& w = in1[(size_t) ch];
        const auto& A = curves[(size_t) lo];
        const auto& B = curves[(size_t) lo + 1];
        const double fr = frac;
        auto f = [&] (double u) { return (1.0 - fr) * A ((float) u) + fr * B ((float) u); };
        auto F = [&] (double u) { return (1.0 - fr) * A.integral (u) + fr * B.integral (u); };
        double& p = x1[(size_t) ch];
        double& q = F1[(size_t) ch];
        q = F (p);
        for (int i = 0; i < n; ++i)
        {
            // supply sag: the stage draws more current on loud passages, the supply droops, the gain falls (bloom)
            const double xin = x[i];
            const double u = xin / (1.0 + 0.6 * s);
            const double y = adaa (u, p, q, f, F);
            const double load = std::abs (y);
            s += (load > s ? sagA : sagR) * (load - s);
            x[i] = (float) lp.process (y - 0.5 * (xin + w), ch);
            w = xin;
        }
    }

private:
    inline static std::array<Table, kBiases> curves;
    inline static std::once_flag curvesOnce;
    Biquad lp;
    std::array<double, kMaxCh> sag {}, x1 {}, F1 {}, in1 {};
    double T = 1.0 / 192000.0, sagA = 0, sagR = 0;
    int lo = 4;
    float frac = 0.0f;
};

// transformer core: v_core = v - wc phi (1 + K (phi/phiS)^2), phi' = v_core (trapezoid, one fixed-point pass)
struct TransformerCore
{
    double T = 1.0 / 192000.0, wc = 1.0, phiS = 1.0, K = 24.0;
    std::array<double, kMaxCh> phi {}, phiL {};
    void prepare (double fs, double hpfHz, double satHz, double k)
    {
        T = 1.0 / fs;
        wc = 2.0 * kPi * hpfHz;
        setSat (satHz);
        K = k;
    }
    void setSat (double satHz) noexcept { phiS = 1.0 / (2.0 * kPi * satHz); }
    void reset() noexcept { phi.fill (0.0); phiL.fill (0.0); }
    // the same core without saturation (its small-signal response: a first-order high pass), same integration
    double linear (double v, int ch) noexcept
    {
        double& p = phiL[(size_t) ch];
        const double vc0 = v - wc * p;
        const double vc = 0.5 * (vc0 + v - wc * (p + T * vc0));
        p += T * vc;
        return vc;
    }
    double process (double v, int ch) noexcept
    {
        double& p = phi[(size_t) ch];
        auto core = [&] (double ph) { const double r = ph / phiS; return v - wc * ph * (1.0 + K * r * r); };
        const double vc0 = core (p);
        const double vc = 0.5 * (vc0 + core (p + T * vc0));
        p += T * vc;
        return vc;
    }
};

// CONSOLE =====================================================================================================
class ConsoleModel
{
public:
    static constexpr double kR = 3.0, kG = 3.6;
    void prepare (double fsOs)
    {
        T = 1.0 / fsOs;
        core.prepare (fsOs, 6.0, 45.0, 24.0);
        std::call_once (stageOnce, [] {
            auto current = [] (double gv)
            {
                double i = 1.0;
                for (int it = 0; it < 60; ++it)
                {
                    const double f = kR * (i - 1.0) + std::log (i) - gv, df = kR + 1.0 / i;
                    double ni = i - f / df;
                    if (ni <= 0.0)
                        ni = 0.5 * i;
                    if (std::abs (ni - i) < 1.0e-13) { i = ni; break; }
                    i = ni;
                }
                const double rail = 2.15, knee = 0.25, over = i - (rail - knee);
                if (over > 0.0)
                    i = rail - knee + knee * std::tanh (over / knee);
                return i;
            };
            const double slope = kG / (kR + 1.0);
            stage.build ([&] (double v) { return (current (kG * v) - 1.0) / slope; }, 6.0f, 8193, true);
        });
        dcK = 1.0 - std::exp (-2.0 * kPi * 3.0 * T);
        reset();
    }
    // a big transformer (saturates at 25 Hz) .. a small one (90 Hz)
    void setCharacter (double c) noexcept { core.setSat (25.0 * std::pow (90.0 / 25.0, std::clamp (c, 0.0, 1.0))); }
    void reset() noexcept { core.reset(); dc.fill (0.0); x1.fill (0.0); F1.fill (0.0); in1.fill (0.0); }
    // 4.0: in place, the COLOUR only (the transformer's and the stage's saturation, without their linear response)
    void process (float* x, int n, int ch) noexcept
    {
        double& d = dc[(size_t) ch];
        double& w = in1[(size_t) ch];
        auto f = [&] (double u) { return (double) stage ((float) u); };
        auto F = [&] (double u) { return stage.integral (u); };
        double& p = x1[(size_t) ch];
        double& q = F1[(size_t) ch];
        for (int i = 0; i < n; ++i)
        {
            const double lin = core.linear (x[i], ch);
            const double c = adaa (core.process (x[i], ch), p, q, f, F) - 0.5 * (lin + w);
            w = lin;
            d += dcK * (c - d);
            x[i] = (float) (c - d);
        }
    }

private:
    TransformerCore core;
    inline static Table stage;
    inline static std::once_flag stageOnce;
    std::array<double, kMaxCh> dc {}, x1 {}, F1 {}, in1 {};
    double T = 1.0 / 192000.0, dcK = 0;
};

// GERMANIUM ===================================================================================================
class GermaniumModel
{
public:
    // germanium common-emitter stage, little degeneration (strong 2nd harmonic) and a low collector rail: R (i-1) +
    // ln i = g v, the rail at i = 1.7 with a soft knee; the bias starves with the signal's envelope
    static constexpr double kR = 0.8, kG = 2.4;
    void prepare (double fsOs)
    {
        T = 1.0 / fsOs;
        std::call_once (stageOnce, [] {
            auto current = [] (double gv)
            {
                double i = 1.0;
                for (int it = 0; it < 80; ++it)
                {
                    const double f = kR * (i - 1.0) + std::log (i) - gv, df = kR + 1.0 / i;
                    double ni = i - f / df;
                    if (ni <= 0.0)
                        ni = 0.5 * i;
                    if (std::abs (ni - i) < 1.0e-13) { i = ni; break; }
                    i = ni;
                }
                const double rail = 1.7, knee = 0.12, over = i - (rail - knee);
                if (over > 0.0)
                    i = rail - knee + knee * std::tanh (over / knee);
                return i;
            };
            const double slope = kG / (kR + 1.0);
            stage.build ([&] (double v) { return (current (kG * v) - 1.0) / slope; }, 8.0f, 8193, true);
        });
        envA = 1.0 - std::exp (-T / 0.002);
        envR = 1.0 - std::exp (-T / 0.090);
        dcK = 1.0 - std::exp (-2.0 * kPi * 4.0 * T);
        lp.lowPass (fsOs, 18000.0, 0.6);
        reset();
    }
    void setCharacter (double c) noexcept { starve = std::clamp (c, 0.0, 1.0); }
    void reset() noexcept { env.fill (0.0); dc.fill (0.0); lp.reset(); x1.fill (0.0); F1.fill (0.0); in1.fill (0.0); }
    // 4.0: in place, the COLOUR only
    void process (float* x, int n, int ch) noexcept
    {
        double& e = env[(size_t) ch];
        double& w = in1[(size_t) ch];
        double& d = dc[(size_t) ch];
        auto f = [&] (double u) { return (double) stage ((float) u); };
        auto F = [&] (double u) { return stage.integral (u); };
        double& p = x1[(size_t) ch];
        double& q = F1[(size_t) ch];
        for (int i = 0; i < n; ++i)
        {
            const double u = x[i];
            const double a = std::abs (u);
            e += (a > e ? envA : envR) * (a - e);
            // a starving bias pulls the operating point toward cut-off on loud passages: the stage gates and sputters
            const double off = -0.35 * starve * std::min (1.0, e);
            const double y = (adaa (u + off, p, q, f, F) - stage ((float) off)) / (1.0 - 0.5 * starve * std::min (1.0, e));
            const double c = y - 0.5 * (u + w);
            w = u;
            d += dcK * (c - d);
            x[i] = (float) lp.process (c - d, ch);
        }
    }

private:
    inline static Table stage;
    inline static std::once_flag stageOnce;
    Biquad lp;
    std::array<double, kMaxCh> env {}, dc {}, x1 {}, F1 {}, in1 {};
    double T = 1.0 / 192000.0, envA = 0, envR = 0, dcK = 0, starve = 0.5;
};

// IRON ========================================================================================================
class IronModel
{
public:
    void prepare (double fsOs)
    {
        core.prepare (fsOs, 4.0, 40.0, 18.0);
        // the core voltage grows without bound when the flux saturates; a soft output stage keeps it bounded
        std::call_once (outOnce, [] { out.build ([] (double v) { return std::tanh (v * 0.7) / 0.7; }, 12.0f, 4097); });
        reset();
    }
    // a big core (saturates at 20 Hz) .. a small one (120 Hz)
    void setCharacter (double c) noexcept { core.setSat (20.0 * std::pow (6.0, std::clamp (c, 0.0, 1.0))); }
    void reset() noexcept { core.reset(); }
    // 4.0: in place, the COLOUR only (the core's saturation and the output stage's, without the core's linear response)
    void process (float* x, int n, int ch) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double lin = core.linear (x[i], ch);
            x[i] = (float) (out ((float) core.process (x[i], ch)) - lin);
        }
    }

private:
    TransformerCore core;
    inline static Table out;
    inline static std::once_flag outOnce;
};

// DIODE =======================================================================================================
class DiodeModel
{
public:
    static constexpr int kSteps = 9;
    void prepare (double fsOs)
    {
        std::call_once (curvesOnce, [] { for (int k = 0; k < kSteps; ++k)
            {
                // series R into diodes to ground: (u - v)/R = Is (e^{v/nVt} - 1) - m Is (e^{-v/(m nVt)} - 1)... the
                // negative side has `m` diodes in series (1 .. 2): a higher threshold there - even harmonics
                const double m = 1.0 + k / (double) (kSteps - 1);
                auto solve = [m] (double u)
                {
                    const double a = 1.0 / 0.33, Is = 0.004;   // normalised: the knee near |v| = 0.33 .. 0.66
                    auto I = [&] (double v) { return Is * (std::exp (std::min (60.0, v * a)) - 1.0) - Is * (std::exp (std::min (60.0, -v * a / m)) - 1.0); };
                    double a0 = -6.0, a1 = 6.0;
                    for (int it = 0; it < 80; ++it)
                    {
                        const double v = 0.5 * (a0 + a1);
                        (u - v - I (v) > 0.0 ? a0 : a1) = v;
                    }
                    return 0.5 * (a0 + a1);
                };
                const double g = solve (1.0e-3) / 1.0e-3;
                curves[(size_t) k].build ([&] (double u) { return solve (u) / g; }, 16.0f, 8193, true);
            } });
        lp.onePoleLow (fsOs, 22000.0);
        dcK = 1.0 - std::exp (-2.0 * kPi * 3.0 / fsOs);
        reset();
    }
    void setCharacter (double c) noexcept
    {
        const double p = std::clamp (c, 0.0, 1.0) * (kSteps - 1);
        lo = std::min ((int) p, kSteps - 2);
        frac = p - lo;
    }
    void reset() noexcept { x1.fill (0.0); F1.fill (0.0); lp.reset(); dc.fill (0.0); }
    // 4.0: in place, the COLOUR only (the asymmetric clipper makes DC: blocked at 3 Hz)
    void process (float* x, int n, int ch) noexcept
    {
        const auto& A = curves[(size_t) lo];
        const auto& B = curves[(size_t) lo + 1];
        const double fr = frac;
        auto f = [&] (double u) { return (1.0 - fr) * A ((float) u) + fr * B ((float) u); };
        auto F = [&] (double u) { return (1.0 - fr) * A.integral (u) + fr * B.integral (u); };
        double& p = x1[(size_t) ch];
        double& q = F1[(size_t) ch];
        double& d = dc[(size_t) ch];
        q = F (p);
        for (int i = 0; i < n; ++i)
        {
            const double prev = p;
            const double c = lp.process (adaa (x[i], p, q, f, F) - 0.5 * (x[i] + prev), ch);
            d += dcK * (c - d);
            x[i] = (float) (c - d);
        }
    }

private:
    inline static std::array<Table, kSteps> curves;
    inline static std::once_flag curvesOnce;
    Biquad lp;
    std::array<double, kMaxCh> x1 {}, F1 {}, dc {};
    double dcK = 0.0;
    int lo = 4;
    double frac = 0.0;
};

// FOLD ========================================================================================================
class FoldModel
{
public:
    static constexpr double kA = 1.6;   // folding rate: the first fold at |u| = pi / (2 a)
    void prepare (double fsOs) { lp.onePoleLow (fsOs, 24000.0); dcK = 1.0 - std::exp (-2.0 * kPi * 3.0 / fsOs); reset(); }
    void setCharacter (double c) noexcept
    {
        o = 0.6 * (std::clamp (c, 0.0, 1.0) - 0.5) * 2.0;   // offset -0.6 .. 0.6 (0 at the centre: symmetric, odd only)
        so = std::sin (kA * o);
        co = std::cos (kA * o);
    }
    void reset() noexcept { x1.fill (0.0); F1.fill (0.0); lp.reset(); dc.fill (0.0); }
    // 4.0: in place, the COLOUR only
    void process (float* x, int n, int ch) noexcept
    {
        // y = (sin(a(u+o)) - sin(a o)) / (a cos(a o)): unity slope at 0; F its exact antiderivative
        const double den = kA * co;
        auto f = [&] (double u) { return (std::sin (kA * (u + o)) - so) / den; };
        auto F = [&] (double u) { return (-std::cos (kA * (u + o)) / kA - so * u) / den; };
        double& p = x1[(size_t) ch];
        double& q = F1[(size_t) ch];
        double& d = dc[(size_t) ch];
        q = F (p);
        for (int i = 0; i < n; ++i)
        {
            const double prev = p;
            const double c = lp.process (adaa (x[i], p, q, f, F) - 0.5 * (x[i] + prev), ch);
            d += dcK * (c - d);
            x[i] = (float) (c - d);
        }
    }

private:
    Biquad lp;
    std::array<double, kMaxCh> x1 {}, F1 {}, dc {};
    double o = 0.0, so = 0.0, co = 1.0, dcK = 0.0;
};

//==============================================================================
// AUTO: the loudness the colour costs (the saturation compresses), measured per style, DRIVE, CHARACTER, at FOCUS 0 and
// 100 (tools/SealCalibrate.cpp -> AutoGain.h); AUTO takes it back
#include "AutoGain.h"

// FOCUS 100: the share of the circuit's squash kept (flat over the voice); docs/DECISIONS F14
constexpr double kFocusSquash = 0.3;
// FOCUS 100, per style: where its new harmonics land and their level - found on the artist's voice against the reference
// tape machine at both speeds, at equal squash (docs/DECISIONS F14): the weight shelf at 180 Hz per DRIVE (the low
// harmonics follow the drive as the reference's do), the box notch (f, Q, dB), a second dip (f, dB, Q 1.2), the fizz
// shelf (f, dB), the harmonics' gain
struct FocusShape { std::array<double, 9> weightDb; double f1, q1, g1, f2, g2, fH, gH, harm; };
#include "FocusShapes.h"
// VOICING (shared): weight at 110 Hz, the honk range at 1.2 kHz (Q 0.7), air at 10 kHz
constexpr double kVoiceLowDb = 2.0, kVoiceMidDb = -1.5, kVoiceAirDb = 0.894;

// the weight shelf at a drive above the designed level (linear between the measured drives, held beyond them)
inline double weightAt (const FocusShape& f, double drive) noexcept
{
    const auto& d = kWeightDrives;
    if (drive <= d.front()) return f.weightDb.front();
    if (drive >= d.back()) return f.weightDb.back();
    size_t i = 0;
    while (drive > d[i + 1]) ++i;
    const double t = (drive - d[i]) / (d[i + 1] - d[i]);
    return f.weightDb[i] + t * (f.weightDb[i + 1] - f.weightDb[i]);
}

inline float autoGainDb (int style, float driveDb, float character, float focus) noexcept
{
    using namespace autogain;
    const float dp = std::clamp (driveDb / kDriveStep, 0.0f, (float) (kDrives - 1) - 1.0e-4f);
    const float cp = std::clamp (character / 100.0f * (kChars - 1), 0.0f, (float) (kChars - 1) - 1.0e-4f);
    const int di = (int) dp, ci = (int) cp;
    const float dt = dp - di, ct = cp - ci;
    auto at = [&] (const auto& t)
    {
        const float a = t[(size_t) di][(size_t) ci] + dt * (t[(size_t) di + 1][(size_t) ci] - t[(size_t) di][(size_t) ci]);
        const float b = t[(size_t) di][(size_t) ci + 1] + dt * (t[(size_t) di + 1][(size_t) ci + 1] - t[(size_t) di][(size_t) ci + 1]);
        return a + ct * (b - a);
    };
    const int st = std::clamp (style, 0, kNumStyles - 1);
    const float fo = std::clamp (focus, 0.0f, 100.0f) / 100.0f;
    return -(at (kFlat[(size_t) st]) * (1.0f - fo) + at (kFocus[(size_t) st]) * fo);
}

//==============================================================================
// one band's saturation: its own oversampler and models; a STYLE change crossfades over 15 ms
class Stage
{
public:
    static constexpr int kMaxOsLog2 = 4;

    static constexpr int kLive = 4;
    void prepare (double sampleRate, int maxBlock, int quality, double character)
    {
        fs = sampleRate;
        block = maxBlock;
        os.prepare (block, fs);
        ios.prepare (block, fs);
        liveLat = measureLiveLatency();
        const size_t top = (size_t) block << kMaxOsLog2;
        for (int c = 0; c < kMaxCh; ++c)
        {
            fadeOld[(size_t) c].assign (top, 0.0f);
            fadeCur[(size_t) c].assign (top, 0.0f);
        }
        probe.assign (top / 7 + 2, 0.0f);
        setRate (quality);
        setCharacter (character);
        reset();
    }
    // the models at a new oversampled rate (tables are shared and kept: no allocation)
    void setRate (int quality)
    {
        live = quality == kLive;
        osLog2 = live ? 2 : std::clamp (quality + 1, 1, kMaxOsLog2);
        const double fsOs = fs * (1 << osLog2);
        tape.prepare (fsOs); triode.prepare (fsOs); pentode.prepare (fsOs); console.prepare (fsOs);
        germanium.prepare (fsOs); iron.prepare (fsOs); diode.prepare (fsOs); fold.prepare (fsOs);
        lastC = -1.0;
    }
    void setCharacter (double c) noexcept
    {
        if (std::abs (c - lastC) < 1.0e-5)
            return;
        lastC = c;
        tape.setCharacter (c); triode.setCharacter (c); pentode.setCharacter (c); console.setCharacter (c);
        germanium.setCharacter (c); iron.setCharacter (c); diode.setCharacter (c); fold.setCharacter (c);
    }
    void setStyle (int s, bool immediate) noexcept
    {
        if (s == style)
            return;
        if (style >= 0 && ! immediate)
        {
            prevStyle = style;
            fadeLeft = fadeLen = (int) (0.015 * fs);
        }
        style = s;
    }
    int getStyle() const noexcept { return style; }
    void reset() noexcept
    {
        os.reset();
        ios.reset();
        tape.reset(); triode.reset(); pentode.reset(); console.reset(); germanium.reset(); iron.reset(); diode.reset(); fold.reset();
        fadeLeft = 0;
    }
    int latency() const noexcept { return live ? liveLat : os.latencyFor (osLog2); }
    int latencyFor (int quality) const noexcept { return quality == kLive ? liveLat : os.latencyFor (std::clamp (quality + 1, 1, kMaxOsLog2)); }

    // in place at the base rate (n <= block): up, model, down; the saturation added is accumulated for the meter.
    // `last`: the last channel of this block (the style fade advances once per block)
    void process (float* x, int n, int c, bool last, double& heatIn, double& heatDiff) noexcept
    {
        const int fact = 1 << osLog2, N = n * fact;
        float* hi = live ? ios.up (x, n, c) : os.up (x, n, c, osLog2);
        for (int i = 0; i < N; i += 7)
            heatIn += (double) hi[i] * hi[i];
        if (fadeLeft > 0)
        {
            float* tmp = fadeOld[(size_t) c].data();
            float* cur = fadeCur[(size_t) c].data();
            std::copy (hi, hi + N, tmp);
            std::copy (hi, hi + N, cur);
            run (prevStyle, tmp, N, c);
            run (style, cur, N, c);
            int left = fadeLeft;
            for (int i = 0; i < N; ++i)
            {
                if (i % fact == 0 && left > 0)
                    --left;
                const float t = 1.0f - (float) left / (float) std::max (1, fadeLen);
                hi[i] = tmp[i] + t * (cur[i] - tmp[i]);
            }
            if (last)
                fadeLeft = left;
        }
        else
        {
            for (int i = 0; i < N; i += 7)
                probe[(size_t) (i / 7)] = hi[i];
            run (style, hi, N, c);
            for (int i = 0; i < N; i += 7)
            {
                const double e = (double) hi[i] - probe[(size_t) (i / 7)];
                heatDiff += e * e;
            }
        }
        if (live)
            ios.down (x, n, c);
        else
            os.down (x, n, c, osLog2);
    }
    void resetModelsIfBroken (bool broken) noexcept
    {
        if (broken)
            reset();
    }

private:
    void run (int s, float* x, int n, int c) noexcept
    {
        switch (s)
        {
            case kTape: tape.process (x, n, c); break;
            case kTriode: triode.process (x, n, c); break;
            case kPentode: pentode.process (x, n, c); break;
            case kConsole: console.process (x, n, c); break;
            case kGermanium: germanium.process (x, n, c); break;
            case kIron: iron.process (x, n, c); break;
            case kDiode: diode.process (x, n, c); break;
            default: fold.process (x, n, c); break;
        }
    }
    // the LIVE filters' delay at low frequencies (where the voice's energy is), in whole samples: the dry is delayed by
    // it and the host told it
    int measureLiveLatency()
    {
        iir::Oversampler4 t;
        t.prepare (2048, fs);
        std::vector<float> x (2048), y (2048);
        const double f = 300.0;
        for (int i = 0; i < 2048; ++i)
            x[(size_t) i] = (float) std::sin (2.0 * kPi * f * i / fs);
        t.up (x.data(), 2048, 0);
        t.down (y.data(), 2048, 0);
        double re = 0, im = 0, rx = 0, ix = 0;
        for (int i = 1024; i < 2048; ++i)
        {
            const double a = 2.0 * kPi * f * i / fs;
            re += y[(size_t) i] * std::cos (a); im -= y[(size_t) i] * std::sin (a);
            rx += x[(size_t) i] * std::cos (a); ix -= x[(size_t) i] * std::sin (a);
        }
        double ph = std::atan2 (ix, rx) - std::atan2 (im, re);
        while (ph < 0) ph += 2.0 * kPi;
        return (int) std::lround (ph / (2.0 * kPi * f / fs));
    }
    double fs = 48000.0, lastC = -1.0;
    int block = 512, osLog2 = 3, style = -1, prevStyle = 0, fadeLeft = 0, fadeLen = 1, liveLat = 4;
    bool live = false;
    axell::Oversampler os;
    iir::Oversampler4 ios;
    std::array<std::vector<float>, kMaxCh> fadeOld, fadeCur;
    std::vector<float> probe;
    TapeModel tape;
    TriodeModel triode;
    PentodeModel pentode;
    ConsoleModel console;
    GermaniumModel germanium;
    IronModel iron;
    DiodeModel diode;
    FoldModel fold;
};

// a ring buffer: a fixed delay
struct Delay
{
    std::vector<float> v;
    int pos = 0;
    void prepare (int maxDelay) { v.assign ((size_t) maxDelay + 1, 0.0f); pos = 0; }
    void reset() noexcept { std::fill (v.begin(), v.end(), 0.0f); pos = 0; }
    // writes x, returns the sample written d samples ago (d <= maxDelay)
    float process (float x, int d) noexcept
    {
        const int L = (int) v.size();
        v[(size_t) pos] = x;
        int r = pos - d;
        if (r < 0)
            r += L;
        pos = pos + 1 == L ? 0 : pos + 1;
        return v[(size_t) r];
    }
};

//==============================================================================
class Engine
{
public:
    static constexpr int kMaxOsLog2 = 4, kBands = 3;

    void prepare (double sampleRate, int maxBlock = 512)
    {
        fs = sampleRate;
        block = std::max (16, maxBlock);
        active.quality = want.quality = std::clamp (params.quality, 0, 4);
        active.ess = want.ess = params.essGuard > 0.05f;
        activeBands = std::clamp (params.bands, 1, kBands);
        for (auto& st : stages)
            st.prepare (fs, block, active.quality, params.character / 100.0);
        ess.prepare (fs, axell::voice::DetectorConfig::live());
        // the audio waits for the network's decision, plus 4 ms: the guard is fully on before an ess reaches the circuit
        preRoll = (int) std::lround (0.004 * fs);
        look = ess.frameDelay() + ess.hopSize() + preRoll;
        const int maxLat = stages[0].latencyFor (3) + look;
        for (auto& d : dryDelay) d.prepare (maxLat + 4);
        for (auto& d : lookDelay) d.prepare (look + 4);
        for (auto& d : preDelay) d.prepare (stages[0].latencyFor (3) + 4);
        for (auto& d : compDelay) d.prepare (stages[0].latencyFor (3) + 4);
        for (auto& b : band) for (auto& v : b) v.assign ((size_t) block, 0.0f);
        for (auto& v : preLine) v.assign ((size_t) block, 0.0f);
        for (auto& b : bandIn) for (auto& v : b) v.assign ((size_t) block, 0.0f);
        for (auto& b : bandInDelay) for (auto& d : b) d.prepare (stages[0].latencyFor (3) + 4);
        bgK = 1.0 - std::exp (-1.0 / (0.0025 * fs));
        for (auto& v : comp) v.assign ((size_t) block, 1.0f);
        dryIn.assign ((size_t) block * kMaxCh, 0.0f);
        learnMono.assign ((size_t) block, 0.0f);
        learnMeter.prepare (fs, 1);
        guardRun.assign ((size_t) block, 0.0f);
        guardDelay.prepare (stages[0].latencyFor (3) + 4);
        fadeStep = (float) (1.0 / std::max (1.0, 0.003 * fs));
        byStep = (float) (1.0 / std::max (1.0, 0.01 * fs));
        kIn.prepare (fs);
        kOut.prepare (fs);
        lK = 1.0 - std::exp (-1.0 / (3.0 * fs));
        gA = 1.0f - std::exp (-1.0f / (0.001f * (float) fs));   // the colour fades out at an ess within ~1 ms (only the colour fades: no click)
        gR = 1.0f - std::exp (-1.0f / (0.040f * (float) fs));
        envA = 1.0f - std::exp (-1.0f / (0.005f * (float) fs));
        envR = 1.0f - std::exp (-1.0f / (0.080f * (float) fs));
        dK = 1.0f - std::exp (-1.0f / (0.02f * (float) fs));
        applyParams (true);
        hs.prepared (latencyOf (active), fs);
        latencyPub.store (latencyOf (active), std::memory_order_release);
        reset();
    }

    void setParams (const Params& p) noexcept
    {
        params = p;
        applyParams (false);
    }
    const Params& getParams() const noexcept { return params; }

    // the latency the host must compensate: the oversampling's, plus the ess look-ahead while ESS GUARD is on (both
    // change only through the handshake: a new latency plays after the host was told)
    int latencySamples() const noexcept { return latencyPub.load (std::memory_order_acquire); }
    axell::LatencyHandshake& handshake() noexcept { return hs; }
    int essLookAhead() const noexcept { return look; }
    // tests: how far behind the newest input the network's decision is (the look-ahead without the guard's head start)
    int essDecisionLag() const noexcept { return look - preRoll; }

    void reset() noexcept
    {
        if (! (want == active) && (! hs.tracking() || latencyOf (want) == latencyOf (active)))
            switchConfig();
        if (wantBands != activeBands)
            activeBands = wantBands;
        for (auto& st : stages)
            st.reset();
        for (auto* f : { &lowCut, &thumpF, &tiltLo, &tiltHi, &hc1, &hc2, &hc3, &air, &apDry1, &apDry2, &apPre1, &apPre2 })
            f->reset();
        for (auto& b : fRb) for (auto& f : b) f.reset();
        for (auto& f : vc) f.reset();
        for (auto& b : xo)
            for (auto& f : b)
                f.reset();
        for (auto& d : dryDelay) d.reset();
        for (auto& d : lookDelay) d.reset();
        for (auto& d : preDelay) d.reset();
        for (auto& d : compDelay) d.reset();
        for (auto& b : bandInDelay) for (auto& d : b) d.reset();
        for (auto* a : { &bgNum, &bgDen, &bgNum2, &bgDen2 })
            for (auto& b : *a) b.fill (0.0);
        guardDelay.reset();
        ess.reset();
        essP = 0.0f;
        guardDb = 0.0f;
        env = 0.0f;
        kIn.resetState();
        kOut.resetState();
        lIn = lOut = 0.0;
        lCount = 0;
        trimTick = 0;
        trimDb = 0.0f;
        trimG = 1.0f;
        driveG = driveT;
        wetG = wetT;
        dryG = dryT;
        bandDbG = bandDb;
        bandMixG = bandMixT;
        heatDb = -100.0f;
    }

    float heat() const noexcept { return heatDb; }
    // DYNAMICS: the drive's offset right now (dB, any thread; for the display)
    float dynamicsNow() const noexcept { return dynPub.load (std::memory_order_relaxed); }
    float essProbability() const noexcept { return essP; }
    float guardNow() const noexcept { return guardDb; }
    void setBypass (bool b) noexcept { bypassT = b ? 1.0f : 0.0f; }

    // LEARN (any thread): measures the incoming take's integrated loudness (BS.1770, gated) for 6 s of signal
    void startLearn() noexcept { learnReq.store (true, std::memory_order_release); }
    bool learningNow() const noexcept { return learning.load (std::memory_order_acquire) || learnReq.load (std::memory_order_acquire); }
    // message thread: the result once, then false; `lufs` the take's loudness
    bool takeLearned (float& lufs) noexcept
    {
        if (! learnDone.exchange (false, std::memory_order_acq_rel))
            return false;
        lufs = learnedLufs.load (std::memory_order_acquire);
        return true;
    }
    // the INPUT that puts a take of this loudness at the designed level (a voice at -18 dBFS RMS)
    static float inputForLoudness (float lufs) noexcept { return std::clamp (-18.0f - lufs, -24.0f, 24.0f); }
    // tests only: force the ess decision (-1 = the network decides, 0 = never an ess, 1 = always)
    void forceEssDecision (int v) noexcept { forcedEss = v; }

    void process (float* const* io, int ch, int n) noexcept
    {
        control (n);
#if defined(__SSE__) || defined(__x86_64__)
        const unsigned csr = _mm_getcsr();
        _mm_setcsr (csr | 0x8040);
#endif
        ch = std::min (ch, kMaxCh);
        for (int off = 0; off < n; off += block)
        {
            const int m = std::min (block, n - off);
            float* p[kMaxCh] = { io[0] + off, ch > 1 ? io[1] + off : nullptr };
            processChunk (p, ch, m);
        }
#if defined(__SSE__) || defined(__x86_64__)
        _mm_setcsr (csr);
#endif
    }

private:
    struct Config
    {
        int quality = 2;
        bool ess = false;
        bool operator== (const Config& o) const noexcept { return quality == o.quality && ess == o.ess; }
    };
    int latencyOf (const Config& c) const noexcept { return stages[0].latencyFor (c.quality) + (c.ess ? look : 0); }

    // QUALITY / ESS GUARD (latency) and BANDS (structure): fade out, switch (after the host was told the latency), fade in
    void control (int n) noexcept
    {
        const Config tgt = hs.isHolding() ? active : want;
        const bool ready = hs.ready (latencyOf (tgt), fade <= 0.0f, n);
        const bool structural = ! (tgt == active) || wantBands != activeBands;
        if (structural)
        {
            fadeTgt = 0.0f;
            if (fade <= 0.0f && ready)
            {
                if (! (tgt == active))
                {
                    active = tgt;
                    switchConfig();
                    warm = latencyOf (active) + (hs.tracking() ? block : 0);
                }
                if (wantBands != activeBands)
                {
                    activeBands = wantBands;
                    for (auto& st : stages)
                        st.reset();
                    for (auto& b : xo)
                        for (auto& f : b)
                            f.reset();
                    apDry1.reset();
                    apDry2.reset();
                    apPre1.reset();
                    apPre2.reset();
                    warm = std::max (warm, stages[0].latency() + block);
                }
            }
        }
        else
            fadeTgt = (! ready || warm > 0) ? 0.0f : 1.0f;
        if (warm > 0)
            warm = std::max (0, warm - n);
    }
    void switchConfig() noexcept
    {
        for (auto& st : stages)
        {
            st.setRate (active.quality);
            st.setCharacter (params.character / 100.0);
            st.reset();
        }
        for (auto& d : dryDelay) d.reset();
        for (auto& d : lookDelay) d.reset();
        for (auto& d : preDelay) d.reset();
        for (auto& d : compDelay) d.reset();
        for (auto& b : bandInDelay) for (auto& d : b) d.reset();
        for (auto* a : { &bgNum, &bgDen, &bgNum2, &bgDen2 })
            for (auto& b : *a) b.fill (0.0);
        guardDelay.reset();
        ess.reset();
        latencyPub.store (latencyOf (active), std::memory_order_release);
    }

    static void glide (float& v, float target, float k) noexcept
    {
        v += (target - v) * k;
        if (std::abs (target - v) <= 1.0e-4f * std::max (1.0f, std::abs (target)))
            v = target;
    }

    int bandStyle (int b) const noexcept
    {
        if (activeBands == 1)
            return params.style;
        const int s = params.bandStyle[(size_t) b];
        return s < 0 ? params.style : std::clamp (s, 0, kNumStyles - 1);
    }

    void applyParams (bool force)
    {
        const auto& p = params;
        want.quality = std::clamp (p.quality, 0, 4);
        want.ess = p.essGuard > 0.05f;
        wantBands = std::clamp (p.bands, 1, kBands);
        for (auto& st : stages)
            st.setCharacter (p.character / 100.0);
        lowCutOn = p.lowCutHz > 20.5f;
        if (lowCutOn)
        {
            lowCut.highPass (fs, p.lowCutHz, p.thump ? 0.9 : 0.7071);
            thumpF.peak (fs, p.lowCutHz * 1.15, 1.4, 4.0);
        }
        thumpOn = lowCutOn && p.thump;
        toneOn = std::abs (p.tone) > 0.05f;
        if (toneOn)
        {
            const double g = 6.0 * p.tone / 100.0;
            tiltLo.shelf (fs, 450.0, -g, false);
            tiltHi.shelf (fs, 1800.0, g, true);
        }
        highCutOn = p.highCutHz < 19999.0f;
        if (highCutOn)
        {
            if (p.steep)
            {
                hc1.lowPass (fs, p.highCutHz, 0.6180340);
                hc2.lowPass (fs, p.highCutHz, 1.6180340);
                hc3.onePoleLow (fs, p.highCutHz);
            }
            else
                hc1.onePoleLow (fs, p.highCutHz);
        }
        airOn = p.airDb > 0.05f;
        if (airOn)
            air.shelf (fs, 11000.0, p.airDb, true);
        // crossovers: Linkwitz-Riley 24 dB/oct (two Butterworth sections each); the low band of three passes the high
        // crossover's allpass, and so does the dry: every band and the dry stay phase-coherent
        const double x1 = std::clamp ((double) p.xLow, 40.0, 2000.0);
        const double x2 = std::clamp ((double) p.xHigh, x1 * 1.5, 16000.0);
        for (int c = 0; c < 1; ++c)
        {
            xo[0][0].lowPass (fs, x1, 0.7071); xo[0][1].lowPass (fs, x1, 0.7071);    // low
            xo[1][0].highPass (fs, x1, 0.7071); xo[1][1].highPass (fs, x1, 0.7071);  // above x1
            xo[2][0].lowPass (fs, x2, 0.7071); xo[2][1].lowPass (fs, x2, 0.7071);    // mid (from above x1)
            xo[3][0].highPass (fs, x2, 0.7071); xo[3][1].highPass (fs, x2, 0.7071);  // high
            xo[4][0].allPass (fs, x2, 0.7071);                                         // low band: x2's phase
        }
        apDry1.allPass (fs, x1, 0.7071);
        apDry2.allPass (fs, x2, 0.7071);
        apPre1.allPass (fs, x1, 0.7071);
        apPre2.allPass (fs, x2, 0.7071);
        // FOCUS: per band, its style's shape (each curve two identical sections at half the gain: the measured
        // zero-phase prototype's magnitude, exactly)
        const double fo = std::clamp (p.focus, 0.0f, 100.0f) / 100.0;
        focusOn = fo > 1.0e-3;
        squashAmt = 1.0 - fo * (1.0 - kFocusSquash);   // FOCUS 0: the circuit's whole squash (classic); 100: the measured share
        // VOICING: the seal's tone - weight, the honk range eased, a breath of air (two sections each, as FOCUS)
        voicingOn = p.voicing;
        for (int k = 0; k < 2; ++k)
        {
            vc[(size_t) (0 + k)].shelf (fs, 110.0, 0.5 * kVoiceLowDb, false);
            vc[(size_t) (2 + k)].peak (fs, 1200.0, 0.7, 0.5 * kVoiceMidDb);
            vc[(size_t) (4 + k)].shelf (fs, 10000.0, 0.5 * kVoiceAirDb, true);
        }
        inputDb = std::clamp (p.inputDb, -24.0f, 24.0f);
        // INPUT acts like drive into the circuits; the colour comes back divided by the drive, so neither changes the
        // level of the voice - AUTO only takes back what the saturation's compression costs
        driveDb = inputDb + p.driveDb + (p.punish ? 20.0f : 0.0f);
        driveT = 1.0f;   // the drive itself is per band and per sample (below)
        for (int b = 0; b < kBands; ++b)
        {
            const int s = bandStyle (b);
            stages[(size_t) b].setStyle (s, force);
            bandDb[(size_t) b] = driveDb + (activeBands > 1 ? std::clamp (p.bandDrive[(size_t) b], -18.0f, 18.0f) : 0.0f);
            // AUTO's table: the loudness the colour costs at a drive above the designed level (INPUT puts the take there)
            bandAutoDb[(size_t) b] = autoGainDb (s, bandDb[(size_t) b] - inputDb, p.character, p.focus);
            bandMixT[(size_t) b] = activeBands > 1 ? std::clamp (p.bandMix[(size_t) b], 0.0f, 100.0f) / 100.0f : 1.0f;
        }
        // FOCUS per band (after the bands' drives: the weight follows each band's drive)
        for (int b = 0; b < kBands; ++b)
        {
            const auto& f = kFocusShapes[(size_t) std::clamp (bandStyle (b), 0, kNumStyles - 1)];
            auto& fr = fRb[(size_t) b];
            for (int k = 0; k < 2; ++k)
            {
                const double lowDb = weightAt (f, (double) (bandDb[(size_t) b] - inputDb));
                fr[(size_t) (0 + k)].shelf (fs, 180.0, 0.5 * lowDb * fo, false);
                fr[(size_t) (2 + k)].peak (fs, f.f1, f.q1, 0.5 * f.g1 * fo);
                fr[(size_t) (4 + k)].peak (fs, f.f2, 1.2, 0.5 * f.g2 * fo);
                fr[(size_t) (6 + k)].shelf (fs, f.fH, 0.5 * f.gH * fo, true);
            }
            harmAmtB[(size_t) b] = 1.0 + fo * (f.harm - 1.0);
        }
        autoOn = p.autoGain;
        {
            float a = 0.0f;
            for (int b = 0; b < activeBands; ++b)
                a += bandAutoDb[(size_t) b] * (activeBands > 1 ? bandMixT[(size_t) b] : 1.0f);
            autoStaticDb = a / (float) activeBands + (voicingOn ? autogain::kVoicingDb : 0.0f);
            autoStaticLin = (float) std::pow (10.0, autoStaticDb / 20.0);
            autoStatic = autoOn ? autoStaticLin : 1.0f;
            // the loudness the vowels lose to the saturation (the table's estimate): a guarded ess gets the same, so the
            // esses keep their balance to the vowels with AUTO on or off
            colourLoss = (float) std::pow (10.0, -(a / (float) activeBands) / 20.0);
        }
        dynK = std::clamp (p.dynamics, -100.0f, 100.0f) / 100.0f;
        guardMax = std::clamp (p.essGuard, 0.0f, 100.0f) / 100.0f;
        const float mx = std::clamp (p.mix, 0.0f, 100.0f) / 100.0f;
        const float out = (float) std::pow (10.0, p.outputDb / 20.0);
        wetBase = out * mx;
        wetT = wetBase;
        dryT = out * (1.0f - mx);
        if (force)
        {
            for (int b = 0; b < kBands; ++b)
            {
                bandMixG[(size_t) b] = bandMixT[(size_t) b];
                bandDbG[(size_t) b] = bandDb[(size_t) b];
            }
            wetG = wetT;
            dryG = dryT;
        }
    }

    void processChunk (float* const* io, int ch, int n) noexcept
    {
        if (learnReq.exchange (false, std::memory_order_acq_rel))
        {
            learnMeter.reset();
            learnCount = 0;
            learning.store (true, std::memory_order_release);
        }
        if (learning.load (std::memory_order_relaxed))
        {
            for (int i = 0; i < n; ++i)
            {
                const float a = std::isfinite (io[0][i]) ? io[0][i] : 0.0f;
                const float b = ch > 1 && std::isfinite (io[1][i]) ? io[1][i] : a;
                learnMono[(size_t) i] = 0.5f * (a + b);
            }
            const float* lp[1] = { learnMono.data() };
            learnMeter.process (lp, 1, n);
            learnCount += n;
            if (learnCount >= (int64_t) (6.0 * fs) && learnMeter.integratedLufs() > -70.0f)
            {
                learnedLufs.store (learnMeter.integratedLufs(), std::memory_order_release);
                learning.store (false, std::memory_order_release);
                learnDone.store (true, std::memory_order_release);
            }
        }
        const int osLat = stages[0].latency();
        const int lat = latencyOf (active);
        const int nb = activeBands;
        // 1. per sample, in time order: the ess decision, the dynamics, the pre-saturation filters, the band split,
        //    the drive of each band (DRIVE x band trim x the per-sample offsets) and the level that offsets cost
        for (int i = 0; i < n; ++i)
        {
            float in[kMaxCh] {};
            for (int c = 0; c < ch; ++c)
            {
                in[c] = std::isfinite (io[c][i]) ? io[c][i] : 0.0f;
                dryIn[(size_t) (c * block + i)] = in[c];
            }
            float mono = ch > 1 ? 0.5f * (in[0] + in[1]) : in[0];
            if (active.ess)
            {
                // the network hears the input now; the circuit gets it `look` samples later, with the decision
                if (ess.push (mono))
                    essP = forcedEss >= 0 ? (float) forcedEss : ess.out().p;
                for (int c = 0; c < ch; ++c)
                    in[c] = lookDelay[(size_t) c].process (in[c], look);
                mono = ch > 1 ? 0.5f * (in[0] + in[1]) : in[0];
            }
            // ESS GUARD: the share of the ess kept out of the saturation (fast in, a 40 ms release)
            const float gTgt = active.ess && essP > 0.5f ? guardMax : 0.0f;
            guardDb += (gTgt > guardDb ? gA : gR) * (gTgt - guardDb);
            guardRun[(size_t) i] = guardDb;
            // DYNAMICS: the drive follows the level (re -18 dBFS), up to +-18 dB
            const float a = std::abs (mono);
            env += (a > env ? envA : envR) * (a - env);
            const float envDb = 20.0f * std::log10 (env + 1.0e-9f);
            const float dynDb = std::clamp (dynK * 0.6f * (envDb + inputDb + 18.0f), -18.0f, 18.0f);   // re -18 dBFS after INPUT
            const float offDb = dynDb;
            if (i == n - 1)
                dynPub.store (offDb, std::memory_order_relaxed);
            for (int c = 0; c < ch; ++c)
            {
                double v = in[c];
                if (lowCutOn)
                {
                    v = lowCut.process (v, c);
                    if (thumpOn)
                        v = thumpF.process (v, c);
                }
                if (toneOn)
                    v = tiltHi.process (tiltLo.process (v, c), c);
                preLine[(size_t) c][(size_t) i] = (float) v;   // what the wet carries of the voice
                double parts[kBands] = { v, 0.0, 0.0 };
                if (nb >= 2)
                {
                    const double lo = xo[0][1].process (xo[0][0].process (v, c), c);
                    const double hi = xo[1][1].process (xo[1][0].process (v, c), c);
                    if (nb == 2)
                    {
                        parts[0] = lo;
                        parts[1] = hi;
                    }
                    else
                    {
                        parts[0] = xo[4][0].process (lo, c);
                        parts[1] = xo[2][1].process (xo[2][0].process (hi, c), c);
                        parts[2] = xo[3][1].process (xo[3][0].process (hi, c), c);
                    }
                }
                for (int b = 0; b < nb; ++b)
                {
                    // the clean band, for the band's MIX, delayed like the wet
                    band[(size_t) b][(size_t) c][(size_t) i] = (float) parts[b];
                }
            }
            for (int b = 0; b < nb; ++b)
            {
                glide (bandDbG[(size_t) b], bandDb[(size_t) b], dK);   // DRIVE moves smoothly (no zipper)
                const float g = (float) std::pow (10.0, (bandDbG[(size_t) b] + offDb) / 20.0);
                for (int c = 0; c < ch; ++c)
                {
                    float& v = band[(size_t) b][(size_t) c][(size_t) i];
                    bandIn[(size_t) b][(size_t) c][(size_t) i] = v;   // the band's signal, for the squash / harmonics split
                    v *= g;
                }
                comp[(size_t) b][(size_t) i] = 1.0f / g;   // the colour comes back at the voice's level
            }
        }
        // 2. each band through its circuit (oversampled)
        double heatIn = 0.0, heatDiff = 0.0;
        for (int b = 0; b < nb; ++b)
            for (int c = 0; c < ch; ++c)
                stages[(size_t) b].process (band[(size_t) b][(size_t) c].data(), n, c, c == ch - 1, heatIn, heatDiff);
        // 3. per sample, in time order: the colour of the bands (back at the voice's level, band MIX, ESS GUARD), FOCUS,
        //    the voice added, VOICING, high cut, air, AUTO (unity on a guarded ess: its balance to the vowels kept), MIX
        float wg = wetG, dg = dryG;
        for (int i = 0; i < n; ++i)
        {
            fade = fadeTgt > fade ? std::min (fadeTgt, fade + fadeStep) : std::max (fadeTgt, fade - fadeStep);
            bypass = bypassT > bypass ? std::min (bypassT, bypass + byStep) : std::max (bypassT, bypass - byStep);
            glide (wg, wetT, dK);
            glide (dg, dryT, dK);
            glide (trimG, trimT, dK * 0.25f);
            float bandScale[kBands];
            for (int b = 0; b < nb; ++b)
            {
                glide (bandMixG[(size_t) b], bandMixT[(size_t) b], dK);
                bandScale[b] = compDelay[(size_t) b].process (comp[(size_t) b][(size_t) i], osLat) * bandMixG[(size_t) b];
            }
            const float gk = guardDelay.process (guardRun[(size_t) i], osLat);   // aligned with the circuit's output
            const float keep = 1.0f - gk;
            // the vowels (unguarded): AUTO gives back exactly what the saturation took (measured, below); a guarded ess
            // gets exactly the vowels' net change - the input's level with AUTO, the vowels' own loss without it
            const float vowelGain = autoOn ? autoStaticLin * trimG : 1.0f;              // the table's estimate x the measured correction
            const float essGain = autoOn ? 1.0f : 1.0f / (autoStaticLin * trimG);   // without AUTO: the vowels' own loss
            const float aEff = vowelGain + gk * (essGain - vowelGain);
            double inSq = 0.0, outSq = 0.0;
            for (int c = 0; c < ch; ++c)
            {
                double squash = 0.0, harm = 0.0;
                for (int b = 0; b < nb; ++b)
                {
                    const double cb = (double) band[(size_t) b][(size_t) c][(size_t) i] * bandScale[b];
                    const double sb = bandInDelay[(size_t) b][(size_t) c].process (bandIn[(size_t) b][(size_t) c][(size_t) i], osLat) * bandMixG[(size_t) b];
                    // two one-pole stages (2.5 ms each): the products' ripple at twice the signal's frequencies stays out
                    // of the gain (a rippling gain would itself make sidebands)
                    double& n1 = bgNum[(size_t) b][(size_t) c];
                    double& d1 = bgDen[(size_t) b][(size_t) c];
                    double& n2 = bgNum2[(size_t) b][(size_t) c];
                    double& d2 = bgDen2[(size_t) b][(size_t) c];
                    n1 += bgK * (cb * sb - n1);
                    d1 += bgK * (sb * sb - d1);
                    n2 += bgK * (n1 - n2);
                    d2 += bgK * (d1 - d2);
                    const double gsq = d2 > 1.0e-14 ? n2 / d2 : 0.0;
                    squash += gsq * sb;
                    double hb = cb - gsq * sb;
                    if (focusOn)
                        for (auto& f : fRb[(size_t) b])
                            hb = f.process (hb, c);
                    harm += harmAmtB[(size_t) b] * hb;
                }
                const double col = (squashAmt * squash + harm) * keep;   // after FOCUS: a guarded ess gets none of its filters' ring-out either
                double pv = preDelay[(size_t) c].process (preLine[(size_t) c][(size_t) i], osLat);
                if (nb >= 2)
                {
                    pv = apPre1.process (pv, c);
                    if (nb == 3)
                        pv = apPre2.process (pv, c);
                }
                double v = pv + col;
                if (voicingOn)
                    for (auto& f : vc)
                        v = f.process (v, c);
                if (highCutOn)
                {
                    v = hc1.process (v, c);
                    if (params.steep)
                        v = hc3.process (hc2.process (v, c), c);
                }
                if (airOn)
                    v = air.process (v, c);
                // the dry, delayed by the whole latency; with bands, through the crossovers' allpass (phase-coherent)
                const float dRaw = dryDelay[(size_t) c].process (dryIn[(size_t) (c * block + i)], lat);
                double dv = dRaw;
                if (nb >= 2)
                {
                    dv = apDry1.process (dv, c);
                    if (nb == 3)
                        dv = apDry2.process (dv, c);
                }
                if (autoOn || active.ess)
                {
                    const double ka = kIn.process (dRaw, c), kb = kOut.process (v, c);   // the wet before any gain
                    inSq += ka * ka;
                    outSq += kb * kb;
                }
                float y = (float) (wg * v * aEff + dg * dv) * fade;
                y = bypass >= 1.0f ? dRaw : (bypass > 0.0f ? y + bypass * (dRaw - y) : y);
                io[c][i] = std::isfinite (y) ? y : 0.0f;
            }
            if ((autoOn || active.ess) && gk < 0.05f)
            {
                // the loudness the saturation takes from the vowels: the integrated loudness (3 s) of the wet against the
                // input's, on the unguarded parts only (a guarded ess never steers it); slow and bounded (no pumping)
                lIn += lK * (inSq / ch - lIn);
                lOut += lK * (outSq / ch - lOut);
                if (lIn > 1.0e-8)
                    ++lCount;
                if ((++trimTick & 31) == 0 && lCount > (int64_t) (0.5 * fs) && lIn > 1.0e-8 && lOut > 1.0e-12)
                {
                    const double lossDb = 10.0 * std::log10 (lOut / lIn);   // <= 0: what the vowels lost
                    trimDb = (float) std::clamp (-lossDb - (double) autoStaticDb, -4.0, 4.0);   // the table's error, corrected
                    trimT = (float) std::pow (10.0, trimDb / 20.0);
                }
            }
        }
        wetG = wg;
        dryG = dg;
        const bool broken = ! std::isfinite (heatDiff) || ! std::isfinite (heatIn);
        for (auto& st : stages)
            st.resetModelsIfBroken (broken);
        heatDb = heatIn > 1.0e-12 ? (float) (10.0 * std::log10 (heatDiff / heatIn + 1.0e-12)) : -100.0f;
    }

    Params params;
    double fs = 48000.0;
    int block = 512, activeBands = 1, wantBands = 1, look = 384, preRoll = 96, warm = 0, forcedEss = -1;
    Config active, want;
    std::array<Stage, kBands> stages;
    std::array<std::array<std::vector<float>, kMaxCh>, kBands> band;
    std::array<std::vector<float>, kMaxCh> preLine;
    std::array<std::array<std::vector<float>, kMaxCh>, kBands> bandIn;
    std::array<std::array<Delay, kMaxCh>, kBands> bandInDelay;
    std::array<std::array<double, kMaxCh>, kBands> bgNum {}, bgDen {}, bgNum2 {}, bgDen2 {};
    double bgK = 0.004, squashAmt = 1.0;
    std::array<std::vector<float>, kBands> comp;
    std::vector<float> dryIn, guardRun;
    Delay guardDelay;
    std::array<Delay, kMaxCh> dryDelay, lookDelay, preDelay;
    std::array<Delay, kBands> compDelay;
    std::array<std::array<Biquad, 2>, 5> xo;
    Biquad lowCut, thumpF, tiltLo, tiltHi, hc1, hc2, hc3, air, apDry1, apDry2, apPre1, apPre2;
    std::array<std::array<Biquad, 8>, kBands> fRb;
    std::array<double, kBands> harmAmtB { 1.0, 1.0, 1.0 };
    std::array<Biquad, 6> vc;
    bool lowCutOn = false, thumpOn = false, toneOn = false, highCutOn = false, airOn = false, autoOn = true, focusOn = true, voicingOn = true;
    float autoStatic = 1.0f, autoStaticLin = 1.0f, autoStaticDb = 0.0f, trimT = 1.0f, trimG = 1.0f, colourLoss = 1.0f;
    float driveDb = 6.0f, driveT = 1, driveG = 1, wetT = 1, wetG = 1, dryT = 0, dryG = 0, heatDb = -100.0f, wetBase = 1.0f;
    std::array<float, kBands> bandDb {}, bandDbG {}, bandAutoDb {}, bandMixT { 1, 1, 1 }, bandMixG { 1, 1, 1 };
    float dynK = 0.0f, env = 0.0f, envA = 0, envR = 0, dK = 0.001f;
    axell::voice::SibilanceDetector ess;
    float essP = 0.0f, guardDb = 0.0f, guardMax = 0.0f, gA = 0, gR = 0, inputDb = 0.0f;
    axell::LoudnessMeter learnMeter;
    std::vector<float> learnMono;
    int64_t learnCount = 0;
    std::atomic<bool> learnReq { false }, learning { false }, learnDone { false };
    std::atomic<float> learnedLufs { -70.0f };
    axell::KWeighting kIn, kOut;
    double lIn = 0.0, lOut = 0.0, lK = 0.0;
    int64_t lCount = 0;
    uint32_t trimTick = 0;
    float trimDb = 0.0f;
    axell::LatencyHandshake hs;
    std::atomic<int> latencyPub { 0 };
    std::atomic<float> dynPub { 0.0f };
    float fade = 1.0f, fadeTgt = 1.0f, fadeStep = 0.01f, bypass = 0.0f, bypassT = 0.0f, byStep = 0.01f;
};
} // namespace afs
