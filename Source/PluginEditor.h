#pragma once
#include <axell/gui/LookAndFeel.h>
#include "PluginProcessor.h"

namespace gui
{
constexpr int kWidth = 1040;
constexpr int kHeight = 790;
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

    SealProcessor& proc;
    ui::LookAndFeel lnf;
    Content content { *this };
    juce::TooltipWindow tooltips { this, 700 };
    juce::TextButton presetPrev { "<" }, presetNext { ">" };
    ui::MenuButton presetName;
    juce::TextButton slotA { "A" }, slotB { "B" }, slotCopy { "COPY" };
    SealView view;
    ui::Segmented style;
    ui::Knob drive { "DRIVE", juce::Colour (0xffe0413a), 92 }, character { "CHARACTER", ui::col::tube, 64 };
    ui::Toggle punish { "PUNISH", juce::Colour (0xffe0413a), "pill" };
    ui::Knob lowCut { "LOW CUT", ui::col::inductor, 62 }, tone { "TONE", ui::col::inductor, 62 }, input { "INPUT", ui::col::inductor, 62 };
    juce::TextButton learn { "LEARN" };
    ui::Toggle thump { "THUMP", ui::col::inductor, "pill" };
    ui::Knob highCut { "HIGH CUT", ui::col::air, 70 }, air { "AIR", ui::col::air, 70 };
    ui::Toggle steep { "STEEP", ui::col::air, "pill" };
    ui::Toggle autoGain { "AUTO", ui::col::global, "pill" };
    ui::Knob mix { "MIX", ui::col::global, 76 }, output { "OUTPUT", ui::col::global, 64 };
    ui::Choice quality;
    // 2.0: BANDS, DYNAMICS, ESS GUARD
    ui::Segmented bandsSel { { "ONE BAND", "TWO", "THREE" }, ui::col::tube };
    ui::Knob xLow { "LOW / MID", ui::col::tube, 52 }, xHigh { "MID / HIGH", ui::col::tube, 52 };
    std::array<ui::Choice, 3> bandStyle;
    std::array<std::unique_ptr<ui::Knob>, 3> bandDrive, bandMix;
    ui::Knob dynamics { "DYNAMICS", ui::col::tube, 56 };
    ui::Knob essGuard { "ESS GUARD", ui::col::warn, 64 };
    std::array<juce::Rectangle<int>, 3> bandCols;
    juce::Rectangle<int> bandsArea;
    int lastBands = -1;
    ui::LevelMeter inMeter { "IN" }, outMeter { "OUT" };
    juce::Rectangle<int> headerArea, viewArea, driveArea, preArea, postArea, outArea;
    int lastStyle = -1;
    bool constructed = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SealEditor)
};
