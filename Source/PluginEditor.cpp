#include "PluginEditor.h"

#include <axell/gui/BrandTitle.h>
#include "MusashiArt.h"

#include <cmath>

using namespace ui;

namespace
{
const juce::Identifier kUiWidth ("uiWidth"), kUiVersion ("uiVersion");   // 4: the 660 x 350 face (a 3.x width is not reused)
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
const char* highCut = "Removes highs AFTER the saturation, from everything (the voice too): for lo-fi and band-limited effects. To "
                      "tame fizz without touching the voice, use FOCUS.";
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
const char* essGuard = "Keeps your esses (S, SH, CH) out of the saturation - the suite's sibilance network finds them a moment "
                       "ahead; the colour fades out over each ess, so it keeps its natural level and never turns into harsh "
                       "fizz. Adds 12 ms of latency while on (the host compensates it).";
const char* focus = "Where the colour lands. The circuit's new harmonics are kept out of the vowels' range (1 - 1.5 kHz: the "
                    "box) and softened above 7 kHz (the fizz); only a share of its squash is kept, spread evenly so the voice's "
                    "tone stays. 0 %: the classic circuit, all of its colour wherever it falls.";
const char* voicing = "The seal's tone: a touch of weight, the honk range eased. Off: the voice exactly as it came in, only the "
                      "colour added.";
const char* input = "The level into the circuits. A saturator reacts to level like the hardware: a quiet take barely touches "
                    "the curve, a hot one breaks it up. AUTO keeps the output level the same.";
const char* learn = "LEARN: play your take for 6 seconds - INPUT is set so the take sits at the level the styles and presets are "
                    "designed for (a voice at -18 dBFS RMS), whatever level it was recorded at.";
const char* view = "What the seal does: output against input over one cycle of a sine at the current settings (the transfer "
                   "curve; tape draws its loop), and the harmonics it made - even in gold, odd in red. HEAT: how much it adds.";
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
                      && std::abs (p.character - shown.character) < 0.2f && std::abs (p.focus - shown.focus) < 0.5f && p.voicing == shown.voicing;
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
    q.focus = shown.focus;
    q.voicing = shown.voicing;
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
    const float k = jlimit (0.5f, 1.0f, r.getHeight() / 230.0f), mg = 14.0f * k;   // the view's scale (text, margins)
    // transfer plot, inside a brush ensō (left); the paper and the ensō are drawn once (2x) and blitted
    auto plot = r.reduced (mg).withWidth (r.getHeight() - 2.0f * mg);
    const float er = 0.5f * plot.getWidth() - 4.0f;
    if (! staticLayer.isValid() || staticLayer.getWidth() != 2 * getWidth() || staticLayer.getHeight() != 2 * getHeight())
    {
        staticLayer = Image (Image::ARGB, 2 * getWidth(), 2 * getHeight(), true);
        Graphics sg (staticLayer);
        sg.addTransform (AffineTransform::scale (2.0f));
        sg.setColour (col::well);
        sg.fillRoundedRectangle (r, 10.0f);
        art::drawWashi (sg, r.reduced (2.0f), 0.05f, 21);
        art::drawEnso (sg, plot.getCentre(), er, 11.0f * k, art::kBone.withAlpha (0.78f));
    }
    g.drawImage (staticLayer, r);
    plot = plot.withSizeKeepingCentre (er * 1.32f, er * 1.32f);
    g.setColour (col::grid.withAlpha (0.6f));
    g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());
    g.drawVerticalLine ((int) plot.getCentreX(), plot.getY(), plot.getBottom());
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
    g.setFont (ui::caps (9.5f * k, 1));
    g.setColour (col::faint);
    g.drawText ("IN", plot.withTrimmedTop (plot.getHeight() / 2.0f).withHeight (14.0f).withTrimmedLeft (plot.getWidth() - 18.0f), Justification::centred);
    g.drawText ("OUT", plot.withWidth (plot.getWidth() / 2.0f + 30.0f).withTrimmedLeft (plot.getWidth() / 2.0f + 2.0f).withHeight (14.0f), Justification::centredLeft);
    // harmonics (right)
    auto bars = r.reduced (mg).withTrimmedLeft (r.getHeight() - 2.0f * mg + 14.0f * k);
    auto heatArea = bars.removeFromRight (bars.getWidth() > 300.0f ? 70.0f : 30.0f);
    bars.removeFromRight (6.0f * k);
    g.setFont (ui::caps (10.0f * k, 1));
    g.setColour (col::dim);
    g.drawText (bars.getWidth() > 220.0f ? "HARMONICS (dB re the fundamental)" : "HARMONICS", bars.removeFromTop (16.0f * k), Justification::centredLeft);
    auto chart = bars.withTrimmedBottom (14.0f * k);
    for (int db = 0; db <= 80; db += 20)
    {
        const float y = chart.getY() + chart.getHeight() * db / 80.0f;
        g.setColour (col::grid);
        g.drawHorizontalLine ((int) y, chart.getX() + 22.0f * k, chart.getRight());
        g.setColour (col::faint);
        g.setFont (ui::font (9.5f * k));
        g.drawText (String (-db), Rectangle<float> (chart.getX(), y - 6.0f * k, 20.0f * k, 12.0f * k), Justification::right);
    }
    const float bw = (chart.getWidth() - 26.0f * k) / 8.0f;
    for (int h2 = 2; h2 <= 9; ++h2)
    {
        const float rel = harm[1] > 0.0f ? 20.0f * std::log10 (harm[(size_t) h2] / harm[1] + 1.0e-9f) : -100.0f;
        const float h = jlimit (0.0f, 1.0f, (rel + 80.0f) / 80.0f) * chart.getHeight();
        const float x = chart.getX() + 26.0f * k + (h2 - 2) * bw;
        g.setColour ((h2 % 2 == 0 ? kGold : kRed).withAlpha (0.9f));
        g.fillRoundedRectangle (x + bw * 0.2f, chart.getBottom() - h, bw * 0.6f, h, 2.0f * k);
        g.setColour (col::faint);
        g.drawText ("H" + String (h2), Rectangle<float> (x, chart.getBottom() + 1.0f, bw, 12.0f * k), Justification::centred);
    }
    // HEAT
    g.setFont (ui::caps (10.0f * k, 1));
    g.setColour (col::dim);
    g.drawText ("HEAT", heatArea.removeFromTop (16.0f * k), Justification::centred);
    auto meter = heatArea.withTrimmedBottom (18.0f * k).withSizeKeepingCentre (14.0f * k, heatArea.getHeight() - 18.0f * k);
    g.setColour (col::track);
    g.fillRoundedRectangle (meter, 4.0f);
    const float frac = jlimit (0.0f, 1.0f, (heat + 60.0f) / 60.0f);
    g.setGradientFill (ColourGradient (kGold, 0.0f, meter.getBottom(), kRed, 0.0f, meter.getY(), false));
    g.fillRoundedRectangle (meter.withTop (meter.getBottom() - meter.getHeight() * frac), 4.0f);
    g.setFont (ui::font (11.0f * k));
    g.setColour (col::text);
    g.drawText (heat > -99.0f ? String (roundToInt (heat)) + " dB" : String ("-"), heatArea.withTop (heatArea.getBottom() - 16.0f * k), Justification::centred);
}

//==============================================================================
SealEditor::SealEditor (SealProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      style ([] { juce::StringArray s; for (int i = 0; i < afs::kNumStyles; ++i) s.add (afs::styleName (i)); return s; }()),
      bandsSel ({ "ONE", "TWO", "THREE" }, art::kIndigo)
{
    const int savedWidth = (int) proc.apvts.state.getProperty (kUiVersion, 0) == 4 ? (int) proc.apvts.state.getProperty (kUiWidth, gui::kWidth) : gui::kWidth;
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    auto& s = proc.apvts;
    if (auto* sp = s.getParameter ("style"))
        style.bind (*sp, tip::style);
    if (auto* bp = s.getParameter ("bands"))
        bandsSel.bind (*bp, tip::bands);
    drive.bind (s, "drive", tip::drive);
    punish.bind (s, "punish", tip::punish);
    character.bind (s, "character", tip::character);
    dynamics.bind (s, "dynamics", tip::dynamics);
    dynamics.setBipolar (true);
    essGuard.bind (s, "essGuard", tip::essGuard);
    input.bind (s, "input", tip::input);
    lowCut.bind (s, "lowCut", tip::lowCut);
    tone.bind (s, "tone", tip::tone);
    tone.setBipolar (true);
    thump.bind (s, "thump", tip::thump);
    learn.setTooltip (tip::learn);
    learn.setClickingTogglesState (false);
    learn.onClick = [this] { proc.startLearn(); };
    xLow.bind (s, "xLow", tip::xLow);
    xHigh.bind (s, "xHigh", tip::xHigh);
    const char* bn[3] = { "LOW", "MID", "HIGH" };
    for (int b = 0; b < 3; ++b)
    {
        const juce::String id = "b" + juce::String (b + 1);
        bandStyle[(size_t) b].bind (s, id + "Style", tip::bandStyle);
        bandDrive[(size_t) b] = std::make_unique<ctl::Enso> ("DRIVE", art::kVermilion, 24);
        bandMix[(size_t) b] = std::make_unique<ctl::Enso> ("MIX", art::kIndigo, 24);
        bandDrive[(size_t) b]->bind (s, id + "Drive", tip::bandDrive);
        bandDrive[(size_t) b]->setBipolar (true);
        bandMix[(size_t) b]->bind (s, id + "Mix", tip::bandMix);
        content.addAndMakeVisible (bandStyle[(size_t) b]);
        content.addAndMakeVisible (*bandDrive[(size_t) b]);
        content.addAndMakeVisible (*bandMix[(size_t) b]);
        (void) bn;
    }
    focus.bind (s, "focus", tip::focus);
    highCut.bind (s, "highCut", tip::highCut);
    air.bind (s, "air", tip::air);
    voicing.bind (s, "voicing", tip::voicing);
    steep.bind (s, "steep", tip::steep);
    mix.bind (s, "mix", tip::mix);
    output.bind (s, "output", tip::output);
    autoGain.bind (s, "auto", tip::autoGain);
    if (auto* qp = s.getParameter ("quality"))
        quality.bind (*qp, tip::quality);
    view.setTooltip (tip::view);
    for (auto* c : std::initializer_list<juce::Component*> {
             &presetPrev, &presetNext, &presetName, &slots, &slotCopy, &view, &style, &quality, &drive, &character,
             &dynamics, &essGuard, &punish, &input, &lowCut, &tone, &learn, &thump, &bandsSel, &xLow, &xHigh, &focus, &highCut,
             &air, &voicing, &steep, &mix, &output, &autoGain, &inMeter, &outMeter })
        content.addAndMakeVisible (c);
    slots.onSelect = [this] (int i) { proc.selectSlot (i); };
    presetPrev.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + proc.getNumPrograms() - 1) % proc.getNumPrograms()); };
    presetNext.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    presetName.onClick = [this] { showPresetMenu(); };
    slotCopy.onClick = [this] { proc.copyActiveToOther(); };
    presetName.setTooltip ("Presets: vocals, ad-libs, 808s, drums, beat bus, master, lo-fi.");
    slots.setTooltip ("A/B: two complete settings to compare.");
    slotCopy.setTooltip ("Copy the current slot to the other one.");
    setResizable (true, true);
    setResizeLimits (gui::kWidth * 3 / 4, gui::kHeight * 3 / 4, gui::kWidth * 2, gui::kHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) gui::kWidth / gui::kHeight);
    const int w = juce::jlimit (gui::kWidth * 3 / 4, gui::kWidth * 2, savedWidth);
    setSize (w, juce::roundToInt (w * (double) gui::kHeight / gui::kWidth));
    layout();
    buildBackdrop();
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
    {
        proc.apvts.state.setProperty (kUiWidth, getWidth(), nullptr);
        proc.apvts.state.setProperty (kUiVersion, 4, nullptr);
    }
}

void SealEditor::Content::resized() { owner.layout(); }

void SealEditor::layout()
{
    using juce::Rectangle;
    const int m = 6, W = gui::kWidth;
    headerArea = { 0, 0, W, 34 };
    {
        auto pb = Rectangle<int> (300, 7, 196, 20);
        presetPrev.setBounds (pb.removeFromLeft (16));
        presetNext.setBounds (pb.removeFromRight (16));
        presetName.setBounds (pb.reduced (2, 0));
        slots.setBounds (W - m - 112, 8, 52, 18);
        slotCopy.setBounds (W - m - 58, 8, 58, 18);
    }
    style.setBounds (m, 37, W - 2 * m - 140, 20);
    quality.setBounds (W - m - 134, 37, 134, 20);
    viewArea = { m, 61, 252, 134 };
    view.setBounds (viewArea);
    fireArea = { m + 258, 61, W - 2 * m - 258, 134 };
    {
        auto a = fireArea.reduced (8, 2).withTrimmedTop (17);
        drive.setBounds (a.removeFromTop (48));
        auto row = a;
        const int cw = 66;
        character.setBounds (row.removeFromLeft (cw));
        dynamics.setBounds (row.removeFromLeft (cw));
        essGuard.setBounds (row.removeFromLeft (cw));
        punish.setBounds (row.withSizeKeepingCentre (74, 20).translated (0, -6));
    }
    const int top = 200, h = gui::kHeight - top - m;
    earthArea = { m, top, 150, h };
    waterArea = { m + 154, top, 200, h };
    windArea = { m + 358, top, 150, h };
    voidArea = { m + 512, top, W - 2 * m - 512, h };
    auto threeRow = [] (Rectangle<int> row, juce::Component& a, juce::Component& b, juce::Component& c)
    {
        const int kw = row.getWidth() / 3;
        a.setBounds (row.removeFromLeft (kw));
        b.setBounds (row.removeFromLeft (kw));
        c.setBounds (row);
    };
    {
        auto a = earthArea.reduced (4, 3).withTrimmedTop (17);
        threeRow (a.removeFromTop (54), input, lowCut, tone);
        a.removeFromTop (4);
        auto st = a.removeFromTop (20);
        learn.setBounds (st.removeFromLeft (st.getWidth() / 2).withSizeKeepingCentre (64, 19));
        thump.setBounds (st.withSizeKeepingCentre (64, 19));
    }
    {
        auto a = waterArea.reduced (5, 3).withTrimmedTop (17);
        auto top2 = a.removeFromTop (40);
        bandsSel.setBounds (top2.removeFromLeft (96).withSizeKeepingCentre (96, 18).translated (0, -6));
        xLow.setBounds (top2.removeFromLeft (top2.getWidth() / 2));
        xHigh.setBounds (top2);
        const int cw = a.getWidth() / 3;
        for (int b = 0; b < 3; ++b)
        {
            auto col = a.removeFromLeft (cw).reduced (2, 0);
            bandCols[(size_t) b] = col;
            col.removeFromTop (9);
            bandStyle[(size_t) b].setBounds (col.removeFromTop (15));
            col.removeFromTop (2);
            bandDrive[(size_t) b]->setBounds (col.removeFromLeft (col.getWidth() / 2).withHeight (38));
            bandMix[(size_t) b]->setBounds (col.withHeight (38));
        }
    }
    {
        auto a = windArea.reduced (4, 3).withTrimmedTop (17);
        threeRow (a.removeFromTop (54), focus, highCut, air);
        a.removeFromTop (4);
        auto st = a.removeFromTop (20);
        voicing.setBounds (st.removeFromLeft (st.getWidth() / 2 + 6).withSizeKeepingCentre (70, 19));
        steep.setBounds (st.withSizeKeepingCentre (58, 19));
    }
    {
        auto a = voidArea.reduced (4, 3).withTrimmedTop (17);
        auto meters = a.removeFromRight (40);
        inMeter.setBounds (meters.removeFromLeft (20));
        outMeter.setBounds (meters);
        auto row = a.removeFromTop (54);
        mix.setBounds (row.removeFromLeft (row.getWidth() / 2));
        output.setBounds (row);
        a.removeFromTop (4);
        autoGain.setBounds (a.removeFromTop (20).withSizeKeepingCentre (60, 19));
    }
}

void SealEditor::buildBackdrop()
{
    // the static art at twice the size (sharp up to 200 % windows): washi over the whole face, ink mountains in the header
    backdrop = juce::Image (juce::Image::ARGB, gui::kWidth * 2, gui::kHeight * 2, true);
    juce::Graphics g (backdrop);
    g.addTransform (juce::AffineTransform::scale (2.0f));
    g.fillAll (col::bg);
    art::drawWashi (g, { 0.0f, 0.0f, (float) gui::kWidth, (float) gui::kHeight }, 0.045f, 7);
    g.setColour (col::header.withAlpha (0.82f));
    g.fillRect (headerArea.toFloat());
    art::drawInkMountains (g, headerArea.toFloat().withTrimmedLeft (gui::kWidth * 0.58f), 0.95f, 11);
    art::drawInkMountains (g, headerArea.toFloat().withWidth (gui::kWidth * 0.38f).withX (gui::kWidth * 0.3f), 0.5f, 29);
    axell::brand::drawHeaderRule (g, (float) headerArea.getBottom() - 1.0f, 0.0f, (float) gui::kWidth, col::grid);
}

void SealEditor::Content::paint (juce::Graphics& g)
{
    using namespace juce;
    auto& e = owner;
    if (e.backdrop.isValid())
        g.drawImage (e.backdrop, Rectangle<float> (0.0f, 0.0f, (float) gui::kWidth, (float) gui::kHeight));
    else
        g.fillAll (col::bg);
    axell::brand::drawEmblem (g, { 5.0f, 4.0f, 26.0f, 26.0f }, kRed);
    const float titleRight = axell::brand::drawTitle (g, 36.0f, 21.0f, "Forbidden Seal", {}, 11.0f);
    // the seals: 禁 and Musashi's 二天, interlocked, glowing with HEAT
    e.sealArea = { titleRight + 6.0f, 4.0f, 15.0f, 15.0f };
    art::drawSeals (g, e.sealArea, e.glowShown);
    axell::brand::drawVersion (g, e.sealArea.getRight() + 13.0f, 21.0f, AXELL_VERSION_STRING, 11.0f);
    // the sections: a breath of shade, no frame - the ring's kanji names each
    auto panel = [&g] (Rectangle<int> a, Colour accent, const char* kanji, const char* title)
    {
        const auto r = a.toFloat();
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.28f), 0.0f, r.getY(), Colours::black.withAlpha (0.12f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 6.0f);
        g.setGradientFill (ColourGradient (accent.withAlpha (0.5f), r.getX(), 0.0f, accent.withAlpha (0.0f), r.getX() + 0.7f * r.getWidth(), 0.0f, false));
        g.fillRect (r.getX() + 6.0f, r.getY(), 0.7f * r.getWidth(), 0.8f);
        art::drawRingTitle (g, r.getX() + 6.0f, r.getY() + 3.0f, String (CharPointer_UTF8 (kanji)), title, accent, 11.0f);
    };
    panel (e.fireArea, art::kVermilion, "\xe7\x81\xab", "FIRE");     // 火
    panel (e.earthArea, art::kOchre, "\xe5\x9c\xb0", "EARTH");       // 地
    panel (e.waterArea, art::kIndigo, "\xe6\xb0\xb4", "WATER");      // 水
    panel (e.windArea, art::kJade, "\xe9\xa2\xa8", "WIND");          // 風
    panel (e.voidArea, art::kSilver, "\xe7\xa9\xba", "VOID");        // 空
    {
        const char* names[3] = { "LOW", "MID", "HIGH" };
        const int nb = juce::roundToInt (e.proc.apvts.getRawParameterValue ("bands")->load()) + 1;
        for (int b = 0; b < 3; ++b)
        {
            const auto r = e.bandCols[(size_t) b];
            const bool on = nb > 1 && b < nb;
            g.setColour ((on ? art::kIndigo : col::faint).withAlpha (on ? 0.9f : 0.6f));
            g.setFont (ui::caps (6.8f, 1));
            g.drawText (nb == 2 && b == 1 ? juce::String ("HIGH") : juce::String (names[b]), r.getX(), r.getY() - 1, r.getWidth(), 9, Justification::centred);
        }
    }
}

void SealEditor::timerCallback()
{
    const auto p = proc.readParams();
    const float heat = proc.heatDb.load();
    view.set (p, heat);
    drive.setLive (heat, p.punish, proc.dynamicsNow());
    {
        const float glow = juce::jlimit (0.0f, 1.0f, (heat + 48.0f) / 40.0f);
        if (std::abs (glow - glowShown) > 0.02f)
        {
            glowShown += 0.35f * (glow - glowShown);
            content.repaint (sealArea.expanded (50.0f, 26.0f).getSmallestIntegerContainer());
        }
    }
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
                c->setAlpha (on ? 1.0f : 0.4f);
            }
        }
        xLow.setAlpha (nb > 1 ? 1.0f : 0.4f);
        xHigh.setAlpha (nb > 2 ? 1.0f : 0.4f);
        xLow.setEnabled (nb > 1);
        xHigh.setEnabled (nb > 2);
        content.repaint (waterArea);
    }
    const bool listening = proc.learningNow();
    learn.setButtonText (listening ? "LISTENING" : "LEARN");
    learn.setToggleState (listening, juce::dontSendNotification);
    if (proc.learnedText.isNotEmpty())
        learn.setTooltip (juce::String (tip::learn) + "\nLast: " + proc.learnedText + ".");
    inMeter.push (proc.inPeak[0].load(), proc.inPeak[1].load(), 2);
    outMeter.push (proc.outPeak[0].load(), proc.outPeak[1].load(), 2);
    presetName.setButtonText (proc.getPresetName());
    slots.setCurrent (proc.getActiveSlot());
    slotCopy.setButtonText (proc.getActiveSlot() == 0 ? "A > B" : "B > A");
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
