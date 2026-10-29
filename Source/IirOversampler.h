#pragma once
// Axell Forbidden Seal - LIVE oversampling: 4x with polyphase IIR half-band filters (two all-pass chains per stage), so
// the latency is about one sample instead of the linear-phase filters' 70-80. The phase is not linear (fine for
// monitoring while recording); the stop band is >= 100 dB.
//
// Design: an elliptic half-band filter split into two all-pass branches (Valenzuela & Constantinides, "Digital signal
// processing schemes for efficient interpolation and decimation", IEE Proc. G 130 (1983); the coefficients follow
// from the transition width and the stop-band attenuation through the elliptic modulus and its nome q, below).
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace afs::iir
{
// coefficients of an elliptic half-band filter as two all-pass branches: a[0], a[2], .. in branch 0, a[1], a[3], .. in
// branch 1 (each section (a + z^-2) / (1 + a z^-2) at the high rate). `transition`: the transition band as a share of
// the sample rate (0 .. 0.5), `attenuationDb`: the stop band.
inline std::vector<double> designHalfband (double transition, double attenuationDb)
{
    const double pi = 3.14159265358979323846;
    // the elliptic modulus of the half-band filter and its nome
    const double k = std::pow (std::tan ((1.0 - 2.0 * transition) * pi / 4.0), 2.0);
    const double kp = std::pow (1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kp) / (1.0 + kp), e2 = e * e, e4 = e2 * e2;
    const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    // the order for the attenuation (odd)
    const double ap = std::pow (10.0, -attenuationDb / 10.0), a = ap / (1.0 - ap);
    int order = (int) std::ceil (std::log (a * a / 16.0) / std::log (q));
    if (order % 2 == 0)
        ++order;
    const int n = (order - 1) / 2;
    std::vector<double> c ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double m = i + 1;
        // theta series of the elliptic functions (they converge in a few terms: q is small)
        double num = 0.0, den = 0.0;
        for (int j = 0;; ++j)
        {
            const double t = std::pow (q, j * (j + 1)) * std::sin ((2 * j + 1) * m * pi / order) * (j % 2 ? -1.0 : 1.0);
            num += t;
            if (std::abs (t) < 1.0e-30 || j > 50)
                break;
        }
        for (int j = 1;; ++j)
        {
            const double t = std::pow (q, j * j) * std::cos (2.0 * j * m * pi / order) * (j % 2 ? -1.0 : 1.0);
            den += t;
            if (std::abs (t) < 1.0e-30 || j > 50)
                break;
        }
        num *= std::pow (q, 0.25);
        den = 0.5 + den;
        const double w = num / den, w2 = w * w;
        const double x = std::sqrt ((1.0 - w2 * k) * (1.0 - w2 / k)) / (1.0 + w2);
        c[(size_t) i] = (1.0 - x) / (1.0 + x);
    }
    return c;
}

// one 2x stage, both directions, for up to 2 channels
class HalfbandStage
{
public:
    static constexpr int kMaxCoefs = 16;
    void design (double transition, double attenuationDb)
    {
        const auto c = designHalfband (transition, attenuationDb);
        nc = std::min ((int) c.size(), kMaxCoefs);
        for (int i = 0; i < nc; ++i)
            coef[(size_t) i] = c[(size_t) i];
        reset();
    }
    void reset() noexcept
    {
        for (auto& s : st)
            s.fill (0.0);
    }
    int numCoefs() const noexcept { return nc; }

    // upsample: one input sample -> two output samples (y0 first)
    void up (double x, int ch, double& y0, double& y1) noexcept
    {
        double a = x, b = x;
        auto& s = st[(size_t) ch];
        for (int i = 0; i < nc; i += 2)
            a = section (a, coef[(size_t) i], s[(size_t) (2 * i)], s[(size_t) (2 * i + 1)]);
        for (int i = 1; i < nc; i += 2)
            b = section (b, coef[(size_t) i], s[(size_t) (2 * i)], s[(size_t) (2 * i + 1)]);
        y0 = a;   // H(z) = (A0(z^2) + z^-1 A1(z^2)) / 2, x2: the even output is branch 0, the odd one branch 1
        y1 = b;
    }
    // downsample: two input samples (x0 earlier) -> one output sample; decimating H's odd phase:
    // y[n] = (A0(x[2n+1]) + A1(x[2n])) / 2
    double down (double x0, double x1, int ch) noexcept
    {
        double a = x1, b = x0;
        auto& s = dst[(size_t) ch];
        for (int i = 0; i < nc; i += 2)
            a = section (a, coef[(size_t) i], s[(size_t) (2 * i)], s[(size_t) (2 * i + 1)]);
        for (int i = 1; i < nc; i += 2)
            b = section (b, coef[(size_t) i], s[(size_t) (2 * i)], s[(size_t) (2 * i + 1)]);
        return 0.5 * (a + b);
    }
    void resetDown() noexcept
    {
        for (auto& s : dst)
            s.fill (0.0);
    }

private:
    // first-order all-pass at the low rate: y = c (x - y1) + x1
    static double section (double x, double c, double& x1, double& y1) noexcept
    {
        const double y = c * (x - y1) + x1;
        x1 = x;
        y1 = y;
        return y;
    }
    std::array<double, kMaxCoefs> coef {};
    int nc = 0;
    std::array<std::array<double, 2 * kMaxCoefs>, 2> st {}, dst {};
};

// 4x up / down for the LIVE quality
class Oversampler4
{
public:
    void prepare (int maxBlock, double baseRate)
    {
        // stage A (fs <-> 2fs): flat to 20 kHz at 44.1 kHz (and above, at higher rates); stage B (2fs <-> 4fs) only has
        // to protect stage A's band, so a wide transition
        const double edge = std::min (0.4535, 20000.0 / baseRate);
        a.design (0.5 - edge, 100.0);
        b.design (0.25, 100.0);
        hi.assign ((size_t) maxBlock * 4, 0.0f);
        reset();
    }
    void reset() noexcept
    {
        a.reset(); b.reset(); a.resetDown(); b.resetDown();
    }
    float* up (const float* in, int n, int ch) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            double m0, m1, h0, h1, h2, h3;
            a.up (in[i], ch, m0, m1);
            b.up (m0, ch, h0, h1);
            b.up (m1, ch, h2, h3);
            float* o = hi.data() + 4 * i;
            o[0] = (float) h0; o[1] = (float) h1; o[2] = (float) h2; o[3] = (float) h3;
        }
        return hi.data();
    }
    void down (float* out, int n, int ch) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float* h = hi.data() + 4 * i;
            const double m0 = b.down (h[0], h[1], ch), m1 = b.down (h[2], h[3], ch);
            out[i] = (float) a.down (m0, m1, ch);
        }
    }
    float* data() noexcept { return hi.data(); }

private:
    HalfbandStage a, b;
    std::vector<float> hi;
};
} // namespace afs::iir
