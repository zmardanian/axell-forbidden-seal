#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "Presets.h"

#include <atomic>

// Thin JUCE wrapper around afs::Engine (all DSP lives in the JUCE-free Engine.h). The latency is the oversampling's;
// QUALITY changes it through the latency handshake (a new latency plays only after the host was told).
class SealProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    SealProcessor();
    ~SealProcessor() override { stopTimer(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    void setNonRealtime (bool isNonRealtime) noexcept override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    afs::Params readParams() const;
    void writeParams (const afs::Params& p);
    int getActiveSlot() const { return activeSlot; }
    void selectSlot (int slot);
    void copyActiveToOther();
    juce::String getPresetName() const;
    void setPresetName (const juce::String& n);

    // meters (any thread): peak level of the last block (linear); the saturation added (dB re the signal)
    std::atomic<float> inPeak[2] { 0.0f, 0.0f }, outPeak[2] { 0.0f, 0.0f }, heatDb { -100.0f };
    // LEARN: listen to the take for 6 s, then set INPUT
    void startLearn() noexcept { dsp.startLearn(); }
    bool learningNow() const noexcept { return dsp.learningNow(); }
    juce::String learnedText;   // message thread

private:
    void timerCallback() override;
    void run (juce::AudioBuffer<float>& buffer, bool bypass);
    void restoreTree (const juce::ValueTree& tree);
    juce::ValueTree stateWithName();

    afs::Engine dsp;
    int preparedChannels = 0;
    int currentProgram = 0, activeSlot = 0;
    juce::ValueTree slots[2];
    juce::CriticalSection nameLock;
    juce::String presetName { "Init" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SealProcessor)
};
