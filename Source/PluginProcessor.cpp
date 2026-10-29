#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
const juce::Identifier kPresetNameId ("presetName");
juce::AudioParameterFloatAttributes fmt (std::function<juce::String (float)> toText)
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
        .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
}
juce::String dbText (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
juce::String pctText (float v) { return juce::String (juce::roundToInt (v)) + " %"; }
juce::String hzText (float v) { return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v / 1000.0f, 1) + " kHz"; }
juce::String lowCutText (float v) { return v <= 20.5f ? juce::String ("Off") : hzText (v); }
juce::String highCutText (float v) { return v >= 19999.0f ? juce::String ("Off") : hzText (v); }
juce::String toneText (float v)
{
    if (std::abs (v) < 0.5f)
        return "Neutral";
    return juce::String (juce::roundToInt (std::abs (v))) + (v < 0 ? " Dark" : " Bright");
}
juce::NormalisableRange<float> skewed (float lo, float hi, float step, float centre)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    r.setSkewForCentre (centre);
    return r;
}
juce::StringArray styles()
{
    juce::StringArray s;
    for (int i = 0; i < afs::kNumStyles; ++i)
        s.add (juce::String (afs::styleName (i)).substring (0, 1) + juce::String (afs::styleName (i)).substring (1).toLowerCase());
    return s;
}
const juce::StringArray kQuality { "Eco (2x)", "Good (4x)", "Best (8x)", "Extreme (16x)", "Live (4x)" };
const juce::StringArray kBands { "One", "Two", "Three" };
juce::StringArray bandStyles()
{
    juce::StringArray s { "Main" };
    s.addArray (styles());
    return s;
}
juce::String dynText (float v)
{
    if (std::abs (v) < 0.5f)
        return "Off";
    return juce::String (juce::roundToInt (std::abs (v))) + (v < 0 ? " Quiet" : " Loud");
}
} // namespace

APVTS::ParameterLayout SealProcessor::createLayout()
{
    using namespace juce;
    const afs::Params d;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    // IDs and order are frozen from 1.0 on (saved projects, automation); new parameters are appended
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "style", 1 }, "Style", styles(), d.style));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Drive", NormalisableRange<float> (0.0f, 30.0f, 0.1f), d.driveDb, fmt (dbText)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "punish", 1 }, "Punish", d.punish));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "character", 1 }, "Character", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.character, fmt (pctText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "lowCut", 1 }, "Low Cut", skewed (20.0f, 1000.0f, 1.0f, 150.0f), d.lowCutHz, fmt (lowCutText)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "thump", 1 }, "Thump", d.thump));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tone", 1 }, "Tone", NormalisableRange<float> (-100.0f, 100.0f, 0.1f), d.tone, fmt (toneText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "highCut", 1 }, "High Cut", skewed (1000.0f, 20000.0f, 10.0f, 6000.0f), d.highCutHz, fmt (highCutText)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "steep", 1 }, "Steep", d.steep));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "air", 1 }, "Air", NormalisableRange<float> (0.0f, 9.0f, 0.1f), d.airDb, fmt (dbText)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "auto", 1 }, "Auto Gain", d.autoGain));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.mix, fmt (pctText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), d.outputDb, fmt (dbText)));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "quality", 1 }, "Quality", kQuality, d.quality));
    // 2.0 (appended)
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "bands", 2 }, "Bands", kBands, d.bands - 1));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "xLow", 2 }, "Crossover Low", skewed (40.0f, 2000.0f, 1.0f, 250.0f), d.xLow, fmt (hzText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "xHigh", 2 }, "Crossover High", skewed (500.0f, 16000.0f, 10.0f, 3500.0f), d.xHigh, fmt (hzText)));
    const char* bn[3] = { "Low", "Mid", "High" };
    for (int b = 0; b < 3; ++b)
    {
        const juce::String n (bn[b]), id = juce::String ("b") + juce::String (b + 1);
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id + "Style", 2 }, n + " Style", bandStyles(), d.bandStyle[(size_t) b] + 1));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id + "Drive", 2 }, n + " Drive", NormalisableRange<float> (-18.0f, 18.0f, 0.1f), d.bandDrive[(size_t) b], fmt (dbText)));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id + "Mix", 2 }, n + " Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.bandMix[(size_t) b], fmt (pctText)));
    }
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "dynamics", 2 }, "Dynamics", NormalisableRange<float> (-100.0f, 100.0f, 0.1f), d.dynamics, fmt (dynText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "essGuard", 2 }, "Ess Guard", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.essGuard,
                                                         fmt ([] (float v) { return v < 0.05f ? juce::String ("Off") : pctText (v); })));
    // 3.0 (appended)
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "input", 3 }, "Input", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), d.inputDb, fmt (dbText)));
    return { p.begin(), p.end() };
}

SealProcessor::SealProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    startTimerHz (30);
}

afs::Params SealProcessor::readParams() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    afs::Params p;
    p.style = juce::roundToInt (v ("style"));
    p.driveDb = v ("drive");
    p.punish = v ("punish") > 0.5f;
    p.character = v ("character");
    p.lowCutHz = v ("lowCut");
    p.thump = v ("thump") > 0.5f;
    p.tone = v ("tone");
    p.highCutHz = v ("highCut");
    p.steep = v ("steep") > 0.5f;
    p.airDb = v ("air");
    p.autoGain = v ("auto") > 0.5f;
    p.mix = v ("mix");
    p.outputDb = v ("output");
    p.quality = juce::roundToInt (v ("quality"));
    p.bands = juce::roundToInt (v ("bands")) + 1;
    p.xLow = v ("xLow");
    p.xHigh = v ("xHigh");
    for (int b = 0; b < 3; ++b)
    {
        const juce::String id = "b" + juce::String (b + 1);
        p.bandStyle[(size_t) b] = juce::roundToInt (apvts.getRawParameterValue (id + "Style")->load()) - 1;
        p.bandDrive[(size_t) b] = apvts.getRawParameterValue (id + "Drive")->load();
        p.bandMix[(size_t) b] = apvts.getRawParameterValue (id + "Mix")->load();
    }
    p.dynamics = v ("dynamics");
    p.essGuard = v ("essGuard");
    p.inputDb = v ("input");
    return p;
}

void SealProcessor::writeParams (const afs::Params& p)
{
    auto set = [this] (const char* id, float v) {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    };
    set ("style", (float) p.style);
    set ("drive", p.driveDb);
    set ("punish", p.punish ? 1.0f : 0.0f);
    set ("character", p.character);
    set ("lowCut", p.lowCutHz);
    set ("thump", p.thump ? 1.0f : 0.0f);
    set ("tone", p.tone);
    set ("highCut", p.highCutHz);
    set ("steep", p.steep ? 1.0f : 0.0f);
    set ("air", p.airDb);
    set ("auto", p.autoGain ? 1.0f : 0.0f);
    set ("mix", p.mix);
    set ("output", p.outputDb);
    set ("bands", (float) (p.bands - 1));
    set ("xLow", p.xLow);
    set ("xHigh", p.xHigh);
    for (int b = 0; b < 3; ++b)
    {
        const juce::String id = "b" + juce::String (b + 1);
        if (auto* prm = apvts.getParameter (id + "Style"))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (p.bandStyle[(size_t) b] + 1)));
        if (auto* prm = apvts.getParameter (id + "Drive"))
            prm->setValueNotifyingHost (prm->convertTo0to1 (p.bandDrive[(size_t) b]));
        if (auto* prm = apvts.getParameter (id + "Mix"))
            prm->setValueNotifyingHost (prm->convertTo0to1 (p.bandMix[(size_t) b]));
    }
    set ("dynamics", p.dynamics);
    set ("essGuard", p.essGuard);
    // INPUT is not part of a preset either: it belongs to the take (LEARN sets it)
    // QUALITY is not part of a preset: a preset never changes the latency of a session
}

bool SealProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == layouts.getMainInputChannelSet();
}

void SealProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedChannels = juce::jlimit (1, 2, getMainBusNumOutputChannels());
    dsp.setParams (readParams());
    dsp.prepare (sampleRate, juce::jmax (32, samplesPerBlock));
    dsp.handshake().setNonRealtime (isNonRealtime());
    setLatencySamples (dsp.latencySamples());
    dsp.handshake().setHostLatency (getLatencySamples());
}

void SealProcessor::reset()
{
    if (preparedChannels > 0)
        dsp.reset();
}

void SealProcessor::setNonRealtime (bool isOffline) noexcept
{
    AudioProcessor::setNonRealtime (isOffline);
    dsp.handshake().setNonRealtime (isOffline);
}

void SealProcessor::timerCallback()
{
    // latency handshake: a new latency plays only after it was reported to the host (axell::LatencyHandshake)
    auto& hs = dsp.handshake();
    hs.heartbeat();
    // LEARN finished: INPUT so the take sits at the designed level (one undo step in the host)
    float lufs = 0.0f;
    if (dsp.takeLearned (lufs))
        if (auto* prm = apvts.getParameter ("input"))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (afs::Engine::inputForLoudness (lufs)));
            prm->endChangeGesture();
            learnedText = "take at " + juce::String (lufs, 1) + " LUFS";
        }
    int lat = 0;
    if (preparedChannels > 0 && hs.take (lat))
    {
        if (lat != getLatencySamples())
            setLatencySamples (lat);
        hs.ack();
    }
}

void SealProcessor::run (juce::AudioBuffer<float>& buffer, bool bypass)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getMainBusNumInputChannels(); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);
    const int nch = juce::jmin (buffer.getNumChannels(), preparedChannels);
    if (preparedChannels == 0 || n == 0)
        return;
    for (int c = 0; c < nch; ++c)
        inPeak[c] = buffer.getMagnitude (c, 0, n);
    dsp.setParams (readParams());
    dsp.setBypass (bypass);
    float* io[2] { buffer.getWritePointer (0), nch > 1 ? buffer.getWritePointer (1) : nullptr };
    dsp.process (io, nch, n);
    for (int c = 0; c < nch; ++c)
        outPeak[c] = buffer.getMagnitude (c, 0, n);
    heatDb = dsp.heat();
}

void SealProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) { run (b, false); }
void SealProcessor::processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer&) { run (b, true); }

//== presets, A/B ==================================================================================================
int SealProcessor::getNumPrograms() { return (int) afs::factoryPresets().size(); }

void SealProcessor::setCurrentProgram (int index)
{
    index = juce::jlimit (0, getNumPrograms() - 1, index);
    currentProgram = index;
    writeParams (afs::factoryPresets()[(size_t) index].p);
    setPresetName (afs::factoryPresets()[(size_t) index].name);
}

const juce::String SealProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? juce::String (afs::factoryPresets()[(size_t) index].name) : juce::String();
}

void SealProcessor::selectSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == activeSlot)
        return;
    slots[activeSlot] = stateWithName();
    if (slots[slot].isValid())
    {
        restoreTree (slots[slot].createCopy());
        setPresetName (slots[slot].getProperty (kPresetNameId, getPresetName()).toString());
    }
    else
        slots[slot] = stateWithName();
    activeSlot = slot;
}

void SealProcessor::copyActiveToOther() { slots[1 - activeSlot] = stateWithName(); }

juce::String SealProcessor::getPresetName() const
{
    const juce::ScopedLock sl (nameLock);
    return presetName;
}

void SealProcessor::setPresetName (const juce::String& n)
{
    const juce::ScopedLock sl (nameLock);
    presetName = n;
}

juce::ValueTree SealProcessor::stateWithName()
{
    auto state = apvts.copyState();
    state.setProperty (kPresetNameId, getPresetName(), nullptr);
    return state;
}

// APVTS::replaceState skips a parameter whose snapped value already matches; re-apply explicitly (pluginval scenario)
void SealProcessor::restoreTree (const juce::ValueTree& tree)
{
    apvts.replaceState (tree);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto child = apvts.state.getChildWithProperty ("id", rp->paramID);
            if (child.isValid() && child.hasProperty ("value"))
            {
                const float norm = rp->convertTo0to1 ((float) child.getProperty ("value"));
                if (std::abs (rp->getValue() - norm) > 1.0e-6f)
                    rp->setValueNotifyingHost (norm);
            }
        }
}

void SealProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = stateWithName();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SealProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        setPresetName (tree.getProperty (kPresetNameId, getPresetName()).toString());
        tree.removeProperty (kPresetNameId, nullptr);
        restoreTree (tree);
    }
}

juce::AudioProcessorEditor* SealProcessor::createEditor() { return new SealEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SealProcessor(); }
