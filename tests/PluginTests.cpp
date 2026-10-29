// Axell Forbidden Seal - plugin-level tests (JUCE): parameters, presets, state, A/B, latency (QUALITY), host bypass,
// mono, rates, editor.
// Exit code = number of failures.
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <axell/dsp/Loudness.h>

#include <cstdio>
#include <random>

namespace
{
int failures = 0, checks = 0;
void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok)
        ++failures;
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
}
void setParam (SealProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}
float getParam (SealProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }
void prepare (SealProcessor& p, double fs = 48000.0, int block = 256, int ch = 2)
{
    p.setPlayConfigDetails (ch, ch, fs, block);
    p.prepareToPlay (fs, block);
}
void pumpMessages (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

std::vector<std::vector<float>> render (SealProcessor& p, std::vector<std::vector<float>> x, int block, bool bypassed = false)
{
    const int nc = (int) x.size();
    juce::AudioBuffer<float> buf (nc, block);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < x[0].size(); pos += (size_t) block)
    {
        const int m = (int) std::min<size_t> ((size_t) block, x[0].size() - pos);
        buf.setSize (nc, m, false, false, true);
        for (int c = 0; c < nc; ++c)
            buf.copyFrom (c, 0, x[(size_t) c].data() + pos, m);
        if (bypassed)
            p.processBlockBypassed (buf, midi);
        else
            p.processBlock (buf, midi);
        for (int c = 0; c < nc; ++c)
            std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + m, x[(size_t) c].begin() + (long) pos);
    }
    return x;
}
std::vector<float> voiceLike (int n, double fs, unsigned seed)
{
    std::vector<float> x ((size_t) n);
    std::mt19937 g (seed);
    std::normal_distribution<float> nd;
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs, u = std::fmod (t, 0.5);
        ph += 2 * juce::MathConstants<double>::pi * (130 + 20 * std::sin (t)) / fs;
        double y = 0;
        for (int h = 1; h < 25; ++h)
            y += std::sin (h * ph) / h;
        x[(size_t) i] = (float) ((u < 0.35 ? 0.15 * y : 0.0) + 0.001 * nd (g));
    }
    return x;
}
bool sameParams (const afs::Params& a, const afs::Params& b)
{
    auto near = [] (float x, float y) { return std::abs (x - y) <= 0.051f + 0.002f * std::abs (y); };   // the parameters' steps
    return a.style == b.style && near (a.driveDb, b.driveDb) && a.punish == b.punish && near (a.character, b.character)
           && near (a.lowCutHz, b.lowCutHz) && a.thump == b.thump && near (a.tone, b.tone) && near (a.highCutHz, b.highCutHz)
           && a.steep == b.steep && near (a.airDb, b.airDb) && a.autoGain == b.autoGain && near (a.mix, b.mix)
           && near (a.outputDb, b.outputDb) && a.bands == b.bands && near (a.xLow, b.xLow) && near (a.xHigh, b.xHigh)
           && a.bandStyle == b.bandStyle && near (a.bandDrive[0], b.bandDrive[0]) && near (a.bandDrive[1], b.bandDrive[1])
           && near (a.bandDrive[2], b.bandDrive[2]) && near (a.bandMix[0], b.bandMix[0]) && near (a.bandMix[1], b.bandMix[1])
           && near (a.bandMix[2], b.bandMix[2]) && near (a.dynamics, b.dynamics) && near (a.essGuard, b.essGuard);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    //== 1. parameters: frozen IDs and order, version hints, defaults = the engine's
    {
        SealProcessor p;
        const juce::StringArray frozen { "style", "drive", "punish", "character", "lowCut", "thump", "tone", "highCut", "steep",
                                         "air", "auto", "mix", "output", "quality",
                                         // 2.0, appended
                                         "bands", "xLow", "xHigh", "b1Style", "b1Drive", "b1Mix", "b2Style", "b2Drive", "b2Mix",
                                         "b3Style", "b3Drive", "b3Mix", "dynamics", "essGuard",
                                         // 3.0, appended
                                         "input" };
        auto& params = p.getParameters();
        check (params.size() == frozen.size(), "parameter count = " + juce::String (frozen.size()));
        bool ids = true;
        for (int i = 0; i < params.size() && i < frozen.size(); ++i)
        {
            auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[i]);
            ids = ids && wp != nullptr && wp->getParameterID() == frozen[i] && wp->getVersionHint() == (i < 14 ? 1 : (i < 28 ? 2 : 3));
        }
        check (ids, "every parameter in its frozen place, version hint 1 (1.0) / 2 (2.0) / 3 (3.0)");
        check (sameParams (p.readParams(), afs::Params {}) && p.readParams().quality == afs::Params {}.quality,
               "defaults = the engine's defaults (= the Init preset), QUALITY Best (8x)");
        prepare (p);
        check (p.getLatencySamples() > 0 && p.getLatencySamples() < 200, "latency reported (Best, 8x): " + juce::String (p.getLatencySamples()) + " samples");
    }

    //== 2. presets: each applies exactly its settings
    {
        SealProcessor p;
        bool ok = true;
        juce::String bad;
        for (int i = 0; i < p.getNumPrograms(); ++i)
        {
            p.setCurrentProgram (i);
            if (! sameParams (p.readParams(), afs::factoryPresets()[(size_t) i].p))
            {
                ok = false;
                bad << p.getProgramName (i) << " ";
            }
        }
        check (ok && p.getNumPrograms() == (int) afs::factoryPresets().size(),
               "every factory preset applies its exact settings (" + juce::String (p.getNumPrograms()) + ")" + (ok ? "" : ": " + bad));
    }

    //== 3. state round trip and A/B
    {
        SealProcessor a;
        setParam (a, "drive", 17.3f);
        setParam (a, "style", 5.0f);
        setParam (a, "punish", 1.0f);
        setParam (a, "highCut", 4500.0f);
        setParam (a, "tone", -35.0f);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        SealProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (sameParams (a.readParams(), b.readParams()), "state restore: every parameter identical");
        SealProcessor c;
        setParam (c, "drive", 4.0f);
        c.selectSlot (1);
        setParam (c, "drive", 22.0f);
        c.selectSlot (0);
        const float s0 = getParam (c, "drive");
        c.selectSlot (1);
        check (std::abs (s0 - 4.0f) < 0.06f && std::abs (getParam (c, "drive") - 22.0f) < 0.06f, "A/B: each slot keeps its own settings");
    }

    //== 3b. the preset name (1.0.1): saved with the state, per A/B slot; a 1.0.0 state (no name) still loads
    {
        SealProcessor a;
        a.setCurrentProgram (7);
        const auto name = a.getProgramName (7);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        SealProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (b.getPresetName() == name && sameParams (a.readParams(), b.readParams()), "state restore: the preset name comes back (" + name + ")");
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        xml->setAttribute ("presetName", "Grit \"bearer\" Seal");
        juce::MemoryBlock renamed;
        juce::AudioProcessor::copyXmlToBinary (*xml, renamed);
        SealProcessor c;
        c.setStateInformation (renamed.getData(), (int) renamed.getSize());
        check (c.getPresetName() == "Grit \"bearer\" Seal", "a name given by the Dojo is shown and saved again");
        xml->removeAttribute ("presetName");
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        SealProcessor d;
        d.setStateInformation (old.getData(), (int) old.getSize());
        check (sameParams (a.readParams(), d.readParams()), "a state without a preset name loads every parameter");
        SealProcessor e;
        e.setCurrentProgram (1);
        e.selectSlot (1);
        e.setCurrentProgram (5);
        e.selectSlot (0);
        const auto n0 = e.getPresetName();
        e.selectSlot (1);
        check (n0 == e.getProgramName (1) && e.getPresetName() == e.getProgramName (5), "A/B: each slot keeps its preset name");
    }

    //== 4. audio through the plugin at every rate: MIX 0 = the input delayed by the latency, host bypass the same,
    //      the effect heard, finite
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const juce::String R = " @ " + juce::String (fs / 1000.0, 1) + " kHz";
        SealProcessor p;
        setParam (p, "mix", 0.0f);
        setParam (p, "drive", 20.0f);
        prepare (p, fs, 256);
        const int lat = p.getLatencySamples();
        const int n = (int) (fs * 1.5);
        auto v = voiceLike (n, fs, 3);
        std::vector<std::vector<float>> x { v, v };
        auto y = render (p, x, 256);
        double e = 0;
        for (int i = lat; i < n; ++i) e = std::max (e, (double) std::abs (y[0][(size_t) i] - x[0][(size_t) (i - lat)]));
        check (e == 0.0, "MIX 0 %: the input delayed by the reported latency (" + juce::String (lat) + "), sample for sample" + R);
        setParam (p, "mix", 100.0f);
        p.reset();
        auto w = render (p, x, 256);
        double diff = 0;
        bool finite = true;
        for (int i = lat; i < n; ++i)
        {
            diff += std::abs (w[0][(size_t) i] - x[0][(size_t) (i - lat)]);
            finite = finite && std::isfinite (w[0][(size_t) i]) && std::isfinite (w[1][(size_t) i]);
        }
        check (diff > 1.0 && finite, "DRIVE +20 dB: the saturation is heard, every sample finite" + R);
        SealProcessor q;
        setParam (q, "drive", 20.0f);
        prepare (q, fs, 256);
        render (q, x, 256);
        auto b = render (q, x, 256, true);
        double eb = 0;
        for (int i = (int) (0.02 * fs) + lat; i < n; ++i) eb = std::max (eb, (double) std::abs (b[0][(size_t) i] - x[0][(size_t) (i - lat)]));
        check (eb == 0.0, "host bypass: after the 10 ms crossfade, the input delayed by the latency, exactly" + R);
    }

    //== 4b. QUALITY changes the latency through the handshake: the host is told before the new rate plays
    {
        SealProcessor p;
        prepare (p, 48000.0, 256);
        const int lat8 = p.getLatencySamples();
        setParam (p, "quality", 0.0f);   // Eco (2x)
        auto v = voiceLike (48000, 48000.0, 4);
        bool reported = false;
        // short render steps with the message thread running between them (as in a host): the request is answered
        // well inside the handshake's 0.5 s
        std::vector<std::vector<float>> step { std::vector<float> (v.begin(), v.begin() + 1024), std::vector<float> (v.begin(), v.begin() + 1024) };
        for (int k = 0; k < 200 && ! reported; ++k)
        {
            render (p, step, 256);
            pumpMessages (5);
            reported = p.getLatencySamples() != lat8;
        }
        for (int k = 0; k < 10; ++k)   // the engine switches and fades back in
        {
            render (p, step, 256);
            pumpMessages (2);
        }
        const int lat2 = p.getLatencySamples();
        // after the switch: MIX 0 is the input delayed by the new latency
        setParam (p, "mix", 0.0f);
        std::vector<std::vector<float>> x { v, v };
        auto y = render (p, x, 256);
        double e = 0;
        for (int i = 24000 + lat2; i < 48000; ++i) e = std::max (e, (double) std::abs (y[0][(size_t) i] - x[0][(size_t) (i - lat2)]));   // after the MIX glide
        check (reported && lat2 < lat8 && e == 0.0, "QUALITY Best -> Eco: latency " + juce::String (lat8) + " -> " + juce::String (lat2)
                                                       + " reported to the host, the audio aligned with it");
    }

    //== 4c. 2.0: the bands and the guard survive a save; ESS GUARD adds the look-ahead latency through the handshake
    {
        SealProcessor a;
        setParam (a, "bands", 2.0f);
        setParam (a, "b1Style", 1.0f);
        setParam (a, "b3Mix", 35.0f);
        setParam (a, "dynamics", -40.0f);
        setParam (a, "essGuard", 70.0f);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        SealProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (sameParams (a.readParams(), b.readParams()) && b.readParams().bands == 3 && b.readParams().bandStyle[0] == 0,
               "2.0 state restore: bands, band styles, DYNAMICS, ESS GUARD identical");
        SealProcessor p;
        prepare (p, 48000.0, 256);
        const int lat0 = p.getLatencySamples();
        setParam (p, "essGuard", 80.0f);
        auto v = voiceLike (48000, 48000.0, 5);
        std::vector<std::vector<float>> step { std::vector<float> (v.begin(), v.begin() + 1024), std::vector<float> (v.begin(), v.begin() + 1024) };
        bool reported = false;
        for (int k = 0; k < 200 && ! reported; ++k)
        {
            render (p, step, 256);
            pumpMessages (5);
            reported = p.getLatencySamples() != lat0;
        }
        check (reported && p.getLatencySamples() - lat0 > 300, "ESS GUARD on: latency " + juce::String (lat0) + " -> " + juce::String (p.getLatencySamples())
                                                               + " (the 8 ms look-ahead) reported to the host");
    }

    //== 4d. 3.0: LEARN measures the take for 6 s and sets INPUT so it sits at the designed level
    {
        SealProcessor p;
        prepare (p, 48000.0, 512);
        auto v = voiceLike (48000 * 8, 48000.0, 6);
        for (auto& x : v) x *= 0.12f;   // a quiet take
        axell::LoudnessMeter m;
        m.prepare (48000.0, 1);
        const float* mp[1] = { v.data() };
        m.process (mp, 1, (int) v.size());
        const float want = afs::Engine::inputForLoudness (m.integratedLufs());
        p.startLearn();
        for (int pos = 0; pos + 4800 <= (int) v.size(); pos += 4800)
        {
            std::vector<std::vector<float>> st { std::vector<float> (v.begin() + pos, v.begin() + pos + 4800), std::vector<float> (v.begin() + pos, v.begin() + pos + 4800) };
            render (p, st, 512);
            pumpMessages (5);
        }
        pumpMessages (60);
        const float got = getParam (p, "input");
        check (std::abs (got - want) < 0.2f && ! p.learningNow(), "LEARN: a take at " + juce::String (m.integratedLufs(), 1) + " LUFS -> INPUT "
                                                                   + juce::String (got, 1) + " dB (expected " + juce::String (want, 1) + ")");
    }

    //== 5. mono track (1 in / 1 out) and blocks larger than announced
    {
        SealProcessor p;
        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        check (p.setBusesLayout (mono), "mono layout accepted");
        prepare (p, 48000.0, 128, 1);
        auto v = voiceLike (48000, 48000.0, 9);
        auto y = render (p, { v }, 700);   // larger than the announced 128
        bool fin = true;
        for (float s : y[0]) fin = fin && std::isfinite (s);
        check (fin, "mono, blocks larger than announced: processes, finite");
    }

    //== 7. editor: open / close while processing, size limits, the last size remembered
    {
        SealProcessor p;
        prepare (p);
        auto v = voiceLike (4096, 48000.0, 2);
        bool ok = true;
        for (int i = 0; i < 5; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            render (p, { v, v }, 512);
            pumpMessages (40);
            ok = ok && ed->getWidth() == gui::kWidth;
        }
        check (ok, "editor: opens / closes 5x while processing (100 % size)");
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* c = ed->getConstrainer();
        check (c != nullptr && c->getMinimumWidth() == gui::kWidth * 3 / 4 && c->getMaximumWidth() == 2 * gui::kWidth, "editor: resizable 75-200 %");
        ed->setSize (gui::kWidth * 3 / 2, gui::kHeight * 3 / 2);
        ed.reset();
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (p.createEditor());
        check (ed2->getWidth() == gui::kWidth * 3 / 2, "editor: re-opens at the last used size (150 %)");
    }

    if (failures == 0)
        std::printf ("ALL PASS: %d of %d plugin checks passed\n", checks, checks);
    else
        std::printf ("FAILURES: %d of %d plugin checks failed\n", failures, checks);
    return failures;
}
