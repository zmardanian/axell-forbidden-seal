// SealRender - the shipping engine on a file (research and measurement).
//   SealRender in.wav out.wav [key=value ...]
//   SealRender --sine out.wav freq peakDbfs seconds fs [key=value ...]   (a sine in)
// keys: style drive punish character lowcut thump tone highcut steep air auto mix output quality block
//       bands xlow xhigh b1style b2style b3style b1drive .. b1mix .. dyn essguard
// The output has the engine's latency removed (aligned with the input).
#include "../Source/Engine.h"
#include "../Source/Presets.h"
#include "WavIO.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void setKey (afs::Params& p, const std::string& k, double v, int& block)
{
    if (k == "style") p.style = (int) v;
    else if (k == "drive") p.driveDb = (float) v;
    else if (k == "punish") p.punish = v != 0;
    else if (k == "character") p.character = (float) v;
    else if (k == "lowcut") p.lowCutHz = (float) v;
    else if (k == "thump") p.thump = v != 0;
    else if (k == "tone") p.tone = (float) v;
    else if (k == "highcut") p.highCutHz = (float) v;
    else if (k == "steep") p.steep = v != 0;
    else if (k == "air") p.airDb = (float) v;
    else if (k == "auto") p.autoGain = v != 0;
    else if (k == "mix") p.mix = (float) v;
    else if (k == "output") p.outputDb = (float) v;
    else if (k == "quality") p.quality = (int) v;
    else if (k == "bands") p.bands = (int) v;
    else if (k == "xlow") p.xLow = (float) v;
    else if (k == "xhigh") p.xHigh = (float) v;
    else if (k.size() == 7 && k.rfind ("b", 0) == 0 && k.substr (2) == "style") p.bandStyle[(size_t) (k[1] - '1')] = (int) v;
    else if (k.size() == 7 && k.rfind ("b", 0) == 0 && k.substr (2) == "drive") p.bandDrive[(size_t) (k[1] - '1')] = (float) v;
    else if (k.size() == 5 && k.rfind ("b", 0) == 0 && k.substr (2) == "mix") p.bandMix[(size_t) (k[1] - '1')] = (float) v;
    else if (k == "dyn") p.dynamics = (float) v;
    else if (k == "essguard") p.essGuard = (float) v;
    else if (k == "input") p.inputDb = (float) v;
    else if (k == "block") block = (int) v;
    else std::fprintf (stderr, "unknown key %s\n", k.c_str());
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: SealRender in.wav out.wav [key=value ...] | --sine out.wav freq peakDbfs seconds fs [key=value ...]\n");
        return 1;
    }
    afs::Params p;
    int block = 512, first = 3;
    wav::Audio a;
    const bool sine = std::strcmp (argv[1], "--sine") == 0;
    if (sine)
    {
        if (argc < 7)
            return 1;
        const double f = std::atof (argv[3]), amp = std::pow (10.0, std::atof (argv[4]) / 20.0);
        a.fs = std::atof (argv[6]);
        const size_t n = (size_t) (std::atof (argv[5]) * a.fs);
        a.ch.assign (2, std::vector<float> (n));
        for (size_t i = 0; i < n; ++i)
            a.ch[0][i] = a.ch[1][i] = (float) (amp * std::sin (2.0 * afs::kPi * f * (double) i / a.fs));
        first = 7;
    }
    else if (! wav::read (argv[1], a))
    {
        std::fprintf (stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    // preset=<name> first (other keys then adjust it)
    for (int i = first; i < argc; ++i)
        if (std::strncmp (argv[i], "preset=", 7) == 0)
            for (const auto& pr : afs::factoryPresets())
                if (std::string (pr.name) == argv[i] + 7)
                {
                    const int q = p.quality;
                    p = pr.p;
                    p.quality = q;
                }
    for (int i = first; i < argc; ++i)
    {
        const std::string s = argv[i];
        if (s.rfind ("preset=", 0) == 0)
            continue;
        const auto eq = s.find ('=');
        if (eq != std::string::npos)
            setKey (p, s.substr (0, eq), std::atof (s.c_str() + eq + 1), block);
    }
    if (a.ch.size() == 1)
        a.ch.push_back (a.ch[0]);
    afs::Engine e;
    e.setParams (p);
    e.prepare (a.fs, block);
    e.setParams (p);
    const int lat = e.latencySamples();
    const size_t n = a.frames();
    for (auto& c : a.ch)
        c.resize (n + (size_t) lat, 0.0f);
    for (size_t pos = 0; pos < n + (size_t) lat; pos += (size_t) block)
    {
        const int m = (int) std::min<size_t> ((size_t) block, n + (size_t) lat - pos);
        float* io[2] = { a.ch[0].data() + pos, a.ch[1].data() + pos };
        e.process (io, 2, m);
    }
    for (auto& c : a.ch)
        c.erase (c.begin(), c.begin() + lat);
    std::fprintf (stderr, "latency %d samples\n", lat);
    return wav::write (sine ? argv[2] : argv[2], a) ? 0 : 1;
}
