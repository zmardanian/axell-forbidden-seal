#include "PluginEditor.h"

#include <axell/gui/BrandTitle.h>

#include <cmath>

using namespace ui;

namespace
{
const juce::Identifier kUiWidth ("uiWidth");
const juce::Colour kRed (0xffe0413a);     // the seal's red
const juce::Colour kGold (0xffd9a55b);
namespace tip
{
const char* style = "TAPE: magnetic hysteresis - highs saturate first, the lows thicken. TRIODE: a single tube stage - even "
                    "harmonics, warm and punchy. PENTODE: a push-pull power stage - odd harmonics, aggressive. CONSOLE: an input "
                    "transformer into a class-A channel - weight and density. GERMANIUM: a vintage transistor stage that starves "
                    "and sputters. IRON: transformer saturation alone - low-end weight. DIODE: a clipper - clean edges to "
                    "grit. FOLD: a wavefolder - metallic, ring-like.";
const char* drive = "How hard the signal hits the circuit. With AUTO on, the level stays where it was: DRIVE changes the colour, "
                    "not the loudness.";
const char* punish = "20 dB more drive, for when it has to break.";
const char* character = "What it changes depends on the style: tape SPEED (slow: fatter lows, softer highs), tube BIAS (cold: more "
                        "even harmonics), CLASS (A to B: crossover grit), transformer / core SIZE, germanium STARVE, diode "
                        "ASYMMETRY, fold OFFSET.";
const char* lowCut = "Removes lows BEFORE the saturation: no flabby low end, a tighter distortion.";
const char* thump = "A resonant bump at the low cut (like tape's head bump): more weight hits the circuit.";
const char* tone = "A gentle tilt BEFORE the saturation: dark sends the lows into the circuit, bright the highs.";
const char* highCut = "Removes highs AFTER the saturation: tames fizz.";
const char* steep = "HIGH CUT at 30 dB per octave instead of 6 (a speaker-cabinet-like edge).";
const char* air = "A high shelf after the saturation.";
const char* autoGain = "Keeps the loudness while you turn DRIVE and CHARACTER (measured for every style), so you compare colour, "
                       "not volume.";
const char* mix = "Dry / wet. Parallel saturation: keep the transients of the dry signal under the colour.";
const char* output = "Output level.";
const char* quality = "Oversampling. Best (8x) keeps aliasing 60 - 120 dB down even on a loud, high note driven hard; Extreme "
                      "(16x) for mastering. Higher costs more CPU and latency. Live (4x): for recording through it - minimum-phase filters, about 4 samples of latency instead of 70-80 (the phase is not linear).";
const char* bands = "Split the signal into two or three bands, each with its own circuit, drive and mix (phase-coherent "
                    "crossovers): tape on the lows, a tube on the mids, the highs left clean...";
const char* xLow = "Where the low band ends and the mid band (or the high band, with two) begins.";
const char* xHigh = "Where the mid band ends and the high band begins (three bands).";
const char* bandStyle = "This band's circuit: MAIN follows the main STYLE.";
const char* bandDrive = "This band's drive on top of DRIVE.";
const char* bandMix = "This band's dry / wet: 0 % leaves its frequencies untouched.";
const char* dynamics = "The drive follows the level: LOUD - the accents saturate harder (grit on the hits); QUIET - the soft "
                       "parts are driven up (thicker details, the peaks spared).";
const char* essGuard = "Keeps your esses (S, SH, CH) out of the saturation - the suite's sibilance network finds them; they keep "
                       "their natural level and the circuit never turns them into harsh fizz. Adds 8 ms of latency while on.";
const char* input = "The level into the circuits. A saturator reacts to level like the hardware: a quiet take barely touches "
                    "the curve, a hot one breaks it up. AUTO keeps the output level the same.";
const char* learn = "LEARN: play your take for 6 seconds - INPUT is set so the take sits at the level the styles and presets are "
                    "designed for (a voice at -18 dBFS RMS), whatever level it was recorded at.";
const char* view = "What the seal does: output against input over one cycle of a sine at the current drive (the transfer curve; "
                   "tape draws its loop), and the harmonics it made - even in gold, odd in red. HEAT: how much it adds.";
} // namespace tip
} // namespace

//==============================================================================
SealView::SealView()
{
    eng.prepare (48000.0, 512);
}

void SealView::set (const afs::Params& p, float heatDb)
{
    heat = heatDb;
    const bool same = valid && p.style == shown.style && std::abs (p.driveDb - shown.driveDb) < 0.05f && p.punish == shown.punish
                      && std::abs (p.character - shown.character) < 0.2f;
    if (! same)
    {
        shown = p;
        render();
        valid = true;
    }
    repaint();
}

void SealView::render()
{
    // the style alone: no filters, no AUTO, full wet, 2x (fast); a 100 Hz sine at -6 dBFS, the last full cycle
    afs::Params q;
    q.style = shown.style;
    q.driveDb = shown.driveDb;
    q.punish = shown.punish;
    q.character = shown.character;
    q.autoGain = false;
    q.quality = 0;
    eng.setParams (q);
    eng.reset();
    const double fs = 48000.0, f = 100.0;
    const int per = (int) (fs / f), n = per * 6;
    std::vector<float> l ((size_t) n), r ((size_t) n), x0 ((size_t) n);
    for (int i = 0; i < n; ++i)
        x0[(size_t) i] = l[(size_t) i] = r[(size_t) i] = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * f * i / fs));
    for (int pos = 0; pos < n; pos += 512)
    {
        float* io[2] = { l.data() + pos, r.data() + pos };
        eng.process (io, 2, std::min (512, n - pos));
    }
    const int lat = eng.latencySamples(), from = n - per;
    xin.assign ((size_t) per, 0.0f);
    yout.assign ((size_t) per, 0.0f);
    for (int i = 0; i < per; ++i)
    {
        xin[(size_t) i] = x0[(size_t) (from - lat + i)];
        yout[(size_t) i] = l[(size_t) (from + i)];
    }
    // harmonics of the 100 Hz cycle (DFT at each multiple)
    for (int k = 1; k < (int) harm.size(); ++k)
    {
        double re = 0, im = 0;
        for (int i = 0; i < per; ++i)
        {
            const double a = 2.0 * juce::MathConstants<double>::pi * k * i / per;
            re += yout[(size_t) i] * std::cos (a);
            im -= yout[(size_t) i] * std::sin (a);
        }
        harm[(size_t) k] = (float) (2.0 * std::sqrt (re * re + im * im) / per);
    }
}

void SealView::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto r = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (r, 10.0f);
    // transfer plot (square, left)
    auto plot = r.reduced (14.0f).withWidth (r.getHeight() - 28.0f);
    g.setColour (col::grid);
    g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());
    g.drawVerticalLine ((int) plot.getCentreX(), plot.getY(), plot.getBottom());
    g.drawRect (plot, 1.0f);
    float peak = 1.0e-6f;
    for (float v : yout)
        peak = std::max (peak, std::abs (v));
    if (! xin.empty())
    {
        Path p;
        for (size_t i = 0; i <= xin.size(); ++i)
        {
            const size_t k = i % xin.size();
            const float px = plot.getCentreX() + xin[k] / 0.5f * plot.getWidth() * 0.46f;
            const float py = plot.getCentreY() - yout[k] / peak * plot.getHeight() * 0.46f;
            if (i == 0)
                p.startNewSubPath (px, py);
            else
                p.lineTo (px, py);
        }
        g.setColour (kRed.withAlpha (0.25f));
        g.strokePath (p, PathStrokeType (5.0f));
        g.setColour (kRed);
        g.strokePath (p, PathStrokeType (1.8f));
    }
    g.setFont (ui::caps (9.5f, 1));
    g.setColour (col::faint);
    g.drawText ("IN", plot.withTrimmedTop (plot.getHeight() - 14.0f).withTrimmedLeft (plot.getWidth() - 24.0f), Justification::centred);
    g.drawText ("OUT", plot.withHeight (14.0f).withWidth (30.0f), Justification::centred);
    // harmonics (right)
    auto bars = r.reduced (14.0f).withTrimmedLeft (plot.getWidth() + 26.0f);
    auto heatArea = bars.removeFromRight (70.0f);
    bars.removeFromRight (10.0f);
    g.setFont (ui::caps (10.0f, 1));
    g.setColour (col::dim);
    g.drawText ("HARMONICS (dB re the fundamental)", bars.removeFromTop (16.0f), Justification::centredLeft);
    auto chart = bars.withTrimmedBottom (16.0f);
    for (int db = 0; db <= 80; db += 20)
    {
        const float y = chart.getY() + chart.getHeight() * db / 80.0f;
        g.setColour (col::grid);
        g.drawHorizontalLine ((int) y, chart.getX() + 26.0f, chart.getRight());
        g.setColour (col::faint);
        g.setFont (ui::font (9.5f));
        g.drawText (String (-db), Rectangle<float> (chart.getX(), y - 6.0f, 24.0f, 12.0f), Justification::right);
    }
    const float bw = (chart.getWidth() - 30.0f) / 8.0f;
    for (int k = 2; k <= 9; ++k)
    {
        const float rel = harm[1] > 0.0f ? 20.0f * std::log10 (harm[(size_t) k] / harm[1] + 1.0e-9f) : -100.0f;
        const float h = jlimit (0.0f, 1.0f, (rel + 80.0f) / 80.0f) * chart.getHeight();
        const float x = chart.getX() + 30.0f + (k - 2) * bw;
        g.setColour ((k % 2 == 0 ? kGold : kRed).withAlpha (0.9f));
        g.fillRoundedRectangle (x + bw * 0.2f, chart.getBottom() - h, bw * 0.6f, h, 2.0f);
        g.setColour (col::faint);
        g.drawText ("H" + String (k), Rectangle<float> (x, chart.getBottom() + 2.0f, bw, 13.0f), Justification::centred);
    }
    // HEAT
    g.setFont (ui::caps (10.0f, 1));
    g.setColour (col::dim);
    g.drawText ("HEAT", heatArea.removeFromTop (16.0f), Justification::centred);
    auto meter = heatArea.withTrimmedBottom (18.0f).withSizeKeepingCentre (16.0f, heatArea.getHeight() - 18.0f);
    g.setColour (col::track);
    g.fillRoundedRectangle (meter, 4.0f);
    const float frac = jlimit (0.0f, 1.0f, (heat + 60.0f) / 60.0f);
    g.setGradientFill (ColourGradient (kGold, 0.0f, meter.getBottom(), kRed, 0.0f, meter.getY(), false));
    g.fillRoundedRectangle (meter.withTop (meter.getBottom() - meter.getHeight() * frac), 4.0f);
    g.setFont (ui::font (11.0f));
    g.setColour (col::text);
    g.drawText (heat > -99.0f ? String (roundToInt (heat)) + " dB" : String ("-"), heatArea.withTop (heatArea.getBottom() - 16.0f), Justification::centred);
}

//==============================================================================
SealEditor::SealEditor (SealProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      style ([] { juce::StringArray s; for (int i = 0; i < afs::kNumStyles; ++i) s.add (afs::styleName (i)); return s; }(), kRed)
{
    const int savedWidth = (int) proc.apvts.state.getProperty (kUiWidth, gui::kWidth);
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    auto& s = proc.apvts;
    if (auto* sp = s.getParameter ("style"))
        style.bind (*sp, tip::style);
    drive.bind (s, "drive", tip::drive);
    punish.bind (s, "punish", tip::punish);
    character.bind (s, "character", tip::character);
    lowCut.bind (s, "lowCut", tip::lowCut);
    input.bind (s, "input", tip::input);
    learn.setTooltip (tip::learn);
    learn.onClick = [this] { proc.startLearn(); };
    learn.setColour (juce::TextButton::buttonOnColourId, col::inductor);
    thump.bind (s, "thump", tip::thump);
    tone.bind (s, "tone", tip::tone);
    highCut.bind (s, "highCut", tip::highCut);
    steep.bind (s, "steep", tip::steep);
    air.bind (s, "air", tip::air);
    autoGain.bind (s, "auto", tip::autoGain);
    mix.bind (s, "mix", tip::mix);
    output.bind (s, "output", tip::output);
    quality.bind (s, "quality", tip::quality);
    if (auto* bp = s.getParameter ("bands"))
        bandsSel.bind (*bp, tip::bands);
    xLow.bind (s, "xLow", tip::xLow);
    xHigh.bind (s, "xHigh", tip::xHigh);
    const char* bn[3] = { "LOW", "MID", "HIGH" };
    for (int b = 0; b < 3; ++b)
    {
        const juce::String id = "b" + juce::String (b + 1);
        bandStyle[(size_t) b].bind (s, id + "Style", tip::bandStyle);
        bandDrive[(size_t) b] = std::make_unique<ui::Knob> (juce::String (bn[b]) + " DRIVE", kRed, 44);
        bandMix[(size_t) b] = std::make_unique<ui::Knob> (juce::String (bn[b]) + " MIX", col::global, 44);
        bandDrive[(size_t) b]->bind (s, id + "Drive", tip::bandDrive);
        bandMix[(size_t) b]->bind (s, id + "Mix", tip::bandMix);
        content.addAndMakeVisible (bandStyle[(size_t) b]);
        content.addAndMakeVisible (*bandDrive[(size_t) b]);
        content.addAndMakeVisible (*bandMix[(size_t) b]);
    }
    dynamics.bind (s, "dynamics", tip::dynamics);
    essGuard.bind (s, "essGuard", tip::essGuard);
    for (auto* c : std::initializer_list<juce::Component*> { &bandsSel, &xLow, &xHigh, &dynamics, &essGuard })
        content.addAndMakeVisible (c);
    view.setTooltip (tip::view);
    for (auto* c : std::initializer_list<juce::Component*> {
             &presetPrev, &presetNext, &presetName, &slotA, &slotB, &slotCopy, &view, &style, &drive, &punish, &character,
             &lowCut, &thump, &tone, &highCut, &steep, &air, &autoGain, &mix, &output, &quality, &inMeter, &outMeter, &input, &learn })
        content.addAndMakeVisible (c);
    for (auto* b : { &slotA, &slotB })
        b->setColour (juce::TextButton::buttonOnColourId, col::global);
    presetPrev.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + proc.getNumPrograms() - 1) % proc.getNumPrograms()); };
    presetNext.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    presetName.onClick = [this] { showPresetMenu(); };
    slotA.onClick = [this] { proc.selectSlot (0); };
    slotB.onClick = [this] { proc.selectSlot (1); };
    slotCopy.onClick = [this] { proc.copyActiveToOther(); };
    presetName.setTooltip ("Presets: vocals, ad-libs, 808s, drums, beat bus, master, lo-fi.");
    slotA.setTooltip ("A/B: two complete settings to compare.");
    slotB.setTooltip ("A/B: two complete settings to compare.");
    slotCopy.setTooltip ("Copy the current slot to the other one.");
    setResizable (true, true);
    setResizeLimits (gui::kWidth * 3 / 4, gui::kHeight * 3 / 4, gui::kWidth * 2, gui::kHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) gui::kWidth / gui::kHeight);
    const int w = juce::jlimit (gui::kWidth * 3 / 4, gui::kWidth * 2, savedWidth);
    setSize (w, juce::roundToInt (w * (double) gui::kHeight / gui::kWidth));
    constructed = true;
    timerCallback();
    startTimerHz (30);
}

SealEditor::~SealEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SealEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void SealEditor::resized()
{
    content.setBounds (0, 0, gui::kWidth, gui::kHeight);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) gui::kWidth));
    if (constructed)
        proc.apvts.state.setProperty (kUiWidth, getWidth(), nullptr);
}

void SealEditor::Content::resized() { owner.layout(); }

void SealEditor::layout()
{
    using juce::Rectangle;
    const int m = 16;
    headerArea = { 0, 0, gui::kWidth, 58 };
    {
        auto pb = Rectangle<int> (380, 14, 330, 30);
        presetPrev.setBounds (pb.removeFromLeft (30));
        pb.removeFromLeft (4);
        presetNext.setBounds (pb.removeFromRight (30));
        pb.removeFromRight (4);
        presetName.setBounds (pb);
        auto ab = Rectangle<int> (gui::kWidth - m - 160, 14, 160, 30);
        slotA.setBounds (ab.removeFromLeft (34));
        ab.removeFromLeft (4);
        slotB.setBounds (ab.removeFromLeft (34));
        ab.removeFromLeft (8);
        slotCopy.setBounds (ab);
    }
    style.setBounds (m, 68, gui::kWidth - 2 * m - 190, 32);
    quality.setBounds (gui::kWidth - m - 180, 70, 180, 28);
    viewArea = { m, 110, 600, 230 };
    view.setBounds (viewArea);
    // DRIVE (the big knob), PUNISH; CHARACTER and DYNAMICS; AUTO
    driveArea = { m + 610, 110, gui::kWidth - 2 * m - 610, 230 };
    {
        auto a = driveArea.reduced (10, 8);
        auto left = a.removeFromLeft (a.getWidth() / 2 + 10);
        drive.setBounds (left.removeFromTop (176));
        punish.setBounds (left.withSizeKeepingCentre (110, 26));
        auto right = a;
        character.setBounds (right.removeFromTop (right.getHeight() / 2));
        dynamics.setBounds (right);
    }
    // BANDS: count, crossovers, then a column per band; ESS GUARD at the right
    bandsArea = { m, 350, gui::kWidth - 2 * m, 168 };
    {
        auto a = bandsArea.reduced (12, 10).withTrimmedTop (14);
        auto left = a.removeFromLeft (270);
        bandsSel.setBounds (left.removeFromTop (30));
        left.removeFromTop (6);
        xLow.setBounds (left.removeFromLeft (left.getWidth() / 2));
        xHigh.setBounds (left);
        auto guard = a.removeFromRight (120);
        essGuard.setBounds (guard);
        a.removeFromLeft (10);
        a.removeFromRight (10);
        const int cw = a.getWidth() / 3;
        for (int b = 0; b < 3; ++b)
        {
            auto col = a.removeFromLeft (cw).reduced (6, 0);
            bandCols[(size_t) b] = col;
            col.removeFromTop (16);
            bandStyle[(size_t) b].setBounds (col.removeFromTop (26));
            col.removeFromTop (4);
            bandDrive[(size_t) b]->setBounds (col.removeFromLeft (col.getWidth() / 2));
            bandMix[(size_t) b]->setBounds (col);
        }
    }
    const int top = 528, h = gui::kHeight - top - m;
    preArea = { m, top, 300, h };
    postArea = { m + 310, top, 300, h };
    outArea = { m + 620, top, gui::kWidth - 2 * m - 620, h };
    {
        auto a = preArea.reduced (8, 10).withTrimmedTop (18);
        auto row = a.removeFromTop (a.getHeight() - 36);
        const int kw = row.getWidth() / 3;
        input.setBounds (row.removeFromLeft (kw));
        lowCut.setBounds (row.removeFromLeft (kw));
        tone.setBounds (row);
        learn.setBounds (a.removeFromLeft (kw).withSizeKeepingCentre (84, 26));
        thump.setBounds (a.removeFromLeft (kw).withSizeKeepingCentre (84, 26));
    }
    {
        auto a = postArea.reduced (10, 10).withTrimmedTop (18);
        auto row = a.removeFromTop (a.getHeight() - 36);
        highCut.setBounds (row.removeFromLeft (row.getWidth() / 2));
        air.setBounds (row);
        steep.setBounds (a.removeFromLeft (a.getWidth() / 2).withSizeKeepingCentre (100, 26));
    }
    {
        auto a = outArea.reduced (10, 10).withTrimmedTop (18);
        auto meters = a.removeFromRight (96);
        inMeter.setBounds (meters.removeFromLeft (44));
        meters.removeFromLeft (8);
        outMeter.setBounds (meters.removeFromLeft (44));
        a.removeFromRight (6);
        autoGain.setBounds (a.removeFromBottom (30).withSizeKeepingCentre (90, 26));
        mix.setBounds (a.removeFromLeft (a.getWidth() / 2));
        output.setBounds (a);
    }
}

void SealEditor::Content::paint (juce::Graphics& g)
{
    using namespace juce;
    auto& e = owner;
    g.fillAll (col::bg);
    g.setColour (col::header);
    g.fillRect (e.headerArea);
    axell::brand::drawHeaderRule (g, (float) e.headerArea.getBottom() - 1.0f, 0.0f, (float) gui::kWidth, col::grid);
    axell::brand::drawEmblem (g, { 12.0f, 6.0f, 46.0f, 46.0f }, kRed);
    axell::brand::drawVersion (g, axell::brand::drawTitle (g, 68.0f, 28.0f, "Forbidden Seal", juce::String (juce::CharPointer_UTF8 ("\xe7\xa6\x81"))), // 禁
                               28.0f, AXELL_VERSION_STRING);
    g.setFont (ui::caps (10.0f, 1));
    g.setColour (col::dim);
    g.drawText ("SATURATION", 67, 37, 300, 14, Justification::centredLeft);
    auto panel = [&g] (Rectangle<int> a, Colour accent, const char* title)
    {
        const auto r = a.toFloat();
        g.setGradientFill (ColourGradient (col::panelTop, 0.0f, r.getY(), col::panel, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (accent.withAlpha (0.4f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.2f);
        if (title != nullptr)
        {
            g.setFont (ui::caps (9.5f, 1));
            g.setColour (col::faint);
            g.drawText (title, a.getX() + 14, a.getY() + 6, 200, 12, Justification::centredLeft);
        }
    };
    panel (e.driveArea, kRed, nullptr);
    panel (e.bandsArea, col::tube, "BANDS");
    {
        const char* names[3] = { "LOW", "MID", "HIGH" };
        const int nb = juce::roundToInt (e.proc.apvts.getRawParameterValue ("bands")->load()) + 1;
        for (int b = 0; b < 3; ++b)
        {
            const auto r = e.bandCols[(size_t) b];
            const bool on = nb > 1 && b < nb;
            g.setColour ((on ? col::tube : col::faint).withAlpha (on ? 0.9f : 0.6f));
            g.setFont (ui::caps (10.0f, 1));
            g.drawText (nb == 2 && b == 1 ? juce::String ("HIGH") : juce::String (names[b]), r.getX(), r.getY(), r.getWidth(), 14, Justification::centred);
        }
    }
    panel (e.preArea, col::inductor, "BEFORE THE SEAL");
    panel (e.postArea, col::air, "AFTER THE SEAL");
    panel (e.outArea, col::global, "OUTPUT");
}

void SealEditor::timerCallback()
{
    const auto p = proc.readParams();
    view.set (p, proc.heatDb.load());
    if (p.style != lastStyle)
    {
        lastStyle = p.style;
        character.setCaption (afs::characterName (p.style));
    }
    const int nb = p.bands;
    if (nb != lastBands)
    {
        lastBands = nb;
        for (int b = 0; b < 3; ++b)
        {
            const bool on = nb > 1 && b < nb;
            for (juce::Component* c : { (juce::Component*) &bandStyle[(size_t) b], (juce::Component*) bandDrive[(size_t) b].get(), (juce::Component*) bandMix[(size_t) b].get() })
            {
                c->setEnabled (on);
                c->setAlpha (on ? 1.0f : 0.35f);
            }
        }
        xLow.setAlpha (nb > 1 ? 1.0f : 0.35f);
        xHigh.setAlpha (nb > 2 ? 1.0f : 0.35f);
        xLow.setEnabled (nb > 1);
        xHigh.setEnabled (nb > 2);
        content.repaint (bandsArea);
    }
    const bool listening = proc.learningNow();
    learn.setButtonText (listening ? "LISTENING..." : "LEARN");
    learn.setToggleState (listening, juce::dontSendNotification);
    if (proc.learnedText.isNotEmpty())
        learn.setTooltip (juce::String (tip::learn) + "\nLast: " + proc.learnedText + ".");
    inMeter.push (proc.inPeak[0].load(), proc.inPeak[1].load(), 2);
    outMeter.push (proc.outPeak[0].load(), proc.outPeak[1].load(), 2);
    presetName.setButtonText (proc.getPresetName());
    slotA.setToggleState (proc.getActiveSlot() == 0, juce::dontSendNotification);
    slotB.setToggleState (proc.getActiveSlot() == 1, juce::dontSendNotification);
    slotCopy.setButtonText (proc.getActiveSlot() == 0 ? "COPY A>B" : "COPY B>A");
}

void SealEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    const auto& list = afs::factoryPresets();
    for (int i = 0; i < (int) list.size(); ++i)
        menu.addItem (1 + i, list[(size_t) i].name);
    juce::Component::SafePointer<SealEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetName), [safe] (int id) {
        if (safe != nullptr && id > 0)
            safe->proc.setCurrentProgram (id - 1);
    });
}
