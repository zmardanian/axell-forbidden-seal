#pragma once
#include <axell/gui/LookAndFeel.h>
#include "PluginProcessor.h"
#include "SealControls.h"

namespace gui
{
constexpr int kWidth = 660;
constexpr int kHeight = 350;
}

// What the seal does to a sound, drawn from the engine itself: a private engine instance renders one cycle of a sine
// at the current STYLE / DRIVE / CHARACTER (filters and AUTO out of the way); the plot shows output against input
// (the transfer curve - tape draws its hysteresis loop) and the harmonics it made, H2 .. H9 (even in gold, odd in red)
class SealView : public juce::Component, public juce::SettableTooltipClient
{
public:
    SealView();
    void set (const afs::Params& p, float heatDb);
    void paint (juce::Graphics&) override;

private:
    void render();
    afs::Engine eng;
    afs::Params shown;
    bool valid = false;
    std::vector<float> xin, yout;
    std::array<float, 10> harm {};
    float heat = -100.0f;
    juce::Image staticLayer;   // the well, its paper and the ensō (2x)
};

class SealEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SealEditor (SealProcessor&);
    ~SealEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (SealEditor& e) : owner (e) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        SealEditor& owner;
    };
    void timerCallback() override;
    void layout();
    void showPresetMenu();
    void buildBackdrop();

    SealProcessor& proc;
    ctl::SealLook lnf;
    Content content { *this };
    juce::TooltipWindow tooltips { this, 700 };
    juce::TextButton presetPrev { "<" }, presetNext { ">" };
    ui::MenuButton presetName;
    ctl::Tabs slots { { "A", "B" }, art::kGold };
    juce::TextButton slotCopy { "COPY" };
    SealView view;
    ctl::Tabs style;
    ctl::Tabs quality { { "2x", "4x", "8x", "16x", "LIVE" }, art::kGold };
    // 火 FIRE: the katana and its ensō
    ctl::Katana drive { "DRIVE" };
    ctl::Enso character { "CHARACTER", art::kVermilion, 40 }, dynamics { "DYNAMICS", art::kVermilion, 40 }, essGuard { "ESS GUARD", art::kGold, 40 };
    ctl::Stamp punish { "PUNISH" };
    // 地 EARTH
    ctl::Enso input { "INPUT", art::kOchre, 34 }, lowCut { "LOW CUT", art::kOchre, 34 }, tone { "TONE", art::kOchre, 34 };
    ctl::Stamp learn { "LEARN", art::kOchre }, thump { "THUMP", art::kOchre };
    // 水 WATER
    ctl::Tabs bandsSel;
    ctl::Enso xLow { "LOW/MID", art::kIndigo, 26 }, xHigh { "MID/HIGH", art::kIndigo, 26 };
    std::array<ui::Choice, 3> bandStyle;
    std::array<std::unique_ptr<ctl::Enso>, 3> bandDrive, bandMix;
    // 風 WIND
    ctl::Enso focus { "FOCUS", art::kJade, 34 }, highCut { "HIGH CUT", art::kJade, 34 }, air { "AIR", art::kJade, 34 };
    ctl::Stamp voicing { "VOICING", art::kJade }, steep { "STEEP", art::kJade };
    // 空 VOID
    ctl::Enso mix { "MIX", art::kSilver, 34 }, output { "OUTPUT", art::kSilver, 34 };
    ctl::Stamp autoGain { "AUTO", art::kSilver };
    ctl::InkMeter inMeter { "IN" }, outMeter { "OUT" };

    std::array<juce::Rectangle<int>, 3> bandCols;
    juce::Rectangle<int> headerArea, viewArea, fireArea, earthArea, waterArea, windArea, voidArea;
    juce::Image backdrop;              // washi, ink mountains: drawn once (2x), then only blitted
    juce::Rectangle<float> sealArea;   // the seals in the header (they glow with HEAT)
    float glowShown = 0.0f;
    int lastBands = -1, lastStyle = -1;
    bool constructed = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SealEditor)
};
