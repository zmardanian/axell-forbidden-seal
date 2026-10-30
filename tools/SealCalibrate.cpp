// SealCalibrate - measures, for every style, CHARACTER (5 steps) and DRIVE (0 .. 50 dB, 2 dB steps), how much louder
// than the plain drive the model makes a vocal-like signal at -18 dBFS RMS (BS.1770 K-weighted mean square over the
// whole signal), and writes Source/AutoGain.h. AUTO then takes back the drive and this, so turning DRIVE changes
// the colour, not the loudness.   SealCalibrate [out.h]
#include "../Source/Engine.h"

#include <axell/dsp/Filters.h>

#include <cstdio>
#include <random>
#include <string>
#include <thread>
#include <vector>

// a voice-like test signal: a glottal pulse train (f0 110 - 190 Hz) through three formants, with fricative noise
// bursts and short pauses, 4 s at 48 kHz, -18 dBFS RMS
static std::vector<float> voice (double fs)
{
    const size_t n = (size_t) (4.0 * fs);
    std::vector<float> x (n);
    std::mt19937 g (5);
    std::normal_distribution<double> nd;
    struct Res { double b0, a1, a2, s1 = 0, s2 = 0; };
    auto res = [fs] (double f, double bw) {
        const double r = std::exp (-afs::kPi * bw / fs), w = 2.0 * afs::kPi * f / fs;
        return Res { 1.0 - r, -2.0 * r * std::cos (w), r * r };
    };
    Res f1 = res (650, 110), f2 = res (1150, 140), f3 = res (2600, 240), fr = res (6500, 2500);
    auto run = [] (Res& r, double v) { const double y = r.b0 * v - r.a1 * r.s1 - r.a2 * r.s2; r.s2 = r.s1; r.s1 = y; return y; };
    double ph = 0.0, glott = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / fs, seg = std::fmod (t, 0.5);
        const double f0 = 150.0 + 40.0 * std::sin (2.0 * afs::kPi * 0.6 * t);
        ph += f0 / fs;
        double pulse = 0.0;
        if (ph >= 1.0) { ph -= 1.0; pulse = 1.0; }
        glott = 0.96 * glott + pulse;
        const double env = seg < 0.32 ? std::min (1.0, std::min (seg, 0.32 - seg) / 0.02) : 0.0;
        double v = env * (run (f1, glott) + 0.6 * run (f2, glott) + 0.3 * run (f3, glott));
        const double fenv = seg > 0.36 && seg < 0.46 ? std::sin (afs::kPi * (seg - 0.36) / 0.1) : 0.0;
        v += 0.5 * fenv * run (fr, nd (g));
        x[i] = (float) v;
    }
    double e = 0.0;
    for (float v : x) e += (double) v * v;
    const double gain = std::pow (10.0, -18.0 / 20.0) / std::sqrt (e / (double) n);
    for (auto& v : x) v = (float) (v * gain);
    return x;
}

static double loudness (const std::vector<float>& x, double fs, size_t from)
{
    axell::KWeighting k;
    k.prepare (fs);
    double e = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double y = k.process (x[i], 0);
        if (i >= from)
            e += y * y;
    }
    return 10.0 * std::log10 (e / (double) (x.size() - from) + 1.0e-30);
}

// 4.0: the loudness of the wet against the input (the colour's compression), AUTO and VOICING off, FOCUS 0 and 100
static double measure (const std::vector<float>& in, double fs, size_t skip, afs::Params p)
{
    p.autoGain = false;
    p.quality = 2;
    afs::Engine e;
    e.setParams (p);
    e.prepare (fs, 512);
    e.setParams (p);
    const int lat = e.latencySamples();
    std::vector<float> l (in), r (in);
    l.resize (in.size() + (size_t) lat, 0.0f);
    r.resize (l.size(), 0.0f);
    for (size_t pos = 0; pos < l.size(); pos += 512)
    {
        const int m = (int) std::min<size_t> (512, l.size() - pos);
        float* io[2] = { l.data() + pos, r.data() + pos };
        e.process (io, 2, m);
    }
    l.erase (l.begin(), l.begin() + lat);
    return loudness (l, fs, skip);
}

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : "Source/AutoGain.h";
    const double fs = 48000.0;
    const auto in = voice (fs);
    const size_t skip = (size_t) (0.25 * fs);
    const double lin = loudness (in, fs, skip);
    using namespace afs::autogain;
    using T = std::vector<std::array<std::array<float, kChars>, kDrives>>;
    T flat (afs::kNumStyles), focus (afs::kNumStyles);
    std::vector<std::thread> workers;
    for (int s = 0; s < afs::kNumStyles; ++s)
        workers.emplace_back ([&, s] {
            for (int c = 0; c < kChars; ++c)
                for (int d = 0; d < kDrives; ++d)
                    for (int fo = 0; fo < 2; ++fo)
                    {
                        afs::Params p;
                        p.style = s;
                        p.character = 100.0f * c / (kChars - 1);
                        p.driveDb = d * kDriveStep;
                        p.focus = fo ? 100.0f : 0.0f;
                        p.voicing = false;
                        (fo ? focus : flat)[(size_t) s][(size_t) d][(size_t) c] = (float) (measure (in, fs, skip, p) - lin);
                    }
        });
    for (auto& w : workers)
        w.join();
    afs::Params pv;
    pv.driveDb = 0.0f;
    pv.voicing = true;
    const double von = measure (in, fs, skip, pv);
    pv.voicing = false;
    const double voff = measure (in, fs, skip, pv);
    FILE* f = std::fopen (out.c_str(), "w");
    if (f == nullptr)
        return 1;
    std::fprintf (f, "// generated by tools/SealCalibrate.cpp - the loudness the colour costs (dB, the wet against the input), per style,\n"
                     "// DRIVE and CHARACTER, at FOCUS 0 (kFlat) and 100 (kFocus), VOICING off; measured on a voice-like signal at\n"
                     "// -18 dBFS RMS (BS.1770 K-weighted). kVoicingDb takes back what VOICING adds. Do not edit.\n"
                     "namespace autogain\n{\nconstexpr float kDriveStep = %.1ff;   // 0, 2, .. 50 dB\nconstexpr int kDrives = %d, kChars = %d;\n"
                     "constexpr float kVoicingDb = %.3ff;\nusing Grid = std::array<std::array<float, kChars>, kDrives>;\n",
                 (double) kDriveStep, kDrives, kChars, -(von - voff));
    for (int t = 0; t < 2; ++t)
    {
        const T& tab = t ? focus : flat;
        std::fprintf (f, "constexpr std::array<Grid, kNumStyles> %s { {\n", t ? "kFocus" : "kFlat");
        for (int s = 0; s < afs::kNumStyles; ++s)
        {
            std::fprintf (f, "    { { // %s\n", afs::styleName (s));
            for (int d = 0; d < kDrives; ++d)
            {
                std::fprintf (f, "        { ");
                for (int c = 0; c < kChars; ++c)
                    std::fprintf (f, "%8.3ff%s", (double) tab[(size_t) s][(size_t) d][(size_t) c], c + 1 < kChars ? ", " : "");
                std::fprintf (f, " }%s\n", d + 1 < kDrives ? "," : "");
            }
            std::fprintf (f, "    } }%s\n", s + 1 < afs::kNumStyles ? "," : "");
        }
        std::fprintf (f, "} };\n");
    }
    std::fprintf (f, "} // namespace autogain\n");
    std::fclose (f);
    std::printf ("wrote %s (voicing %+.2f dB)\n", out.c_str(), von - voff);
    return 0;
}
