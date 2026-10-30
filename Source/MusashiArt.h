#pragma once
// Axell Forbidden Seal - the interface's art, all drawn in code (no images): washi paper, sumi-e ink mountains, a brush
// ensō, and the seals. The motifs follow Miyamoto Musashi: his painting seal 二天 (Niten, "two heavens" - his art name
// and the name of his school), and the five rings of his Go Rin no Sho (地 earth, 水 water, 火 fire, 風 wind, 空 void),
// which name the panels. Every shape here is our own drawing; nothing is traced from an existing seal or painting.
#include <axell/gui/BrandTitle.h>
#include <axell/gui/LookAndFeel.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <random>

namespace art
{
inline const juce::Colour kVermilion (0xffd8402f), kInk (0xff0d0809), kBone (0xffeadfcc), kMist (0xff8c8aa0);
// the five rings' colours, from the traditional palette: fire vermilion, earth ochre, water indigo (aizome), wind jade, void
// silver; gold for fittings
inline const juce::Colour kOchre (0xffc8924a), kIndigo (0xff6f8fc4), kJade (0xff8fbfa8), kSilver (0xffc4bdb2), kGold (0xffc9a35a);

// a small, deterministic random source (the art is the same on every open)
struct Rng
{
    std::mt19937 g;
    explicit Rng (unsigned seed) : g (seed) {}
    float operator() (float lo, float hi) { return std::uniform_real_distribution<float> (lo, hi) (g); }
};

// washi: the long, pale fibres of mulberry paper, a few darker flecks, laid very faintly over the background
inline void drawWashi (juce::Graphics& g, juce::Rectangle<float> r, float alpha, unsigned seed = 7)
{
    using namespace juce;
    Rng rnd (seed);
    const int fibres = (int) (r.getWidth() * r.getHeight() / 900.0f);
    for (int i = 0; i < fibres; ++i)
    {
        const float x = rnd (r.getX(), r.getRight()), y = rnd (r.getY(), r.getBottom());
        const float len = rnd (8.0f, 46.0f), ang = rnd (-0.5f, 0.5f) + (rnd (0.0f, 1.0f) < 0.5f ? 0.0f : MathConstants<float>::pi * 0.5f);
        Path p;
        p.startNewSubPath (x, y);
        p.quadraticTo (x + 0.5f * len * std::cos (ang) + rnd (-4.0f, 4.0f), y + 0.5f * len * std::sin (ang) + rnd (-4.0f, 4.0f),
                       x + len * std::cos (ang), y + len * std::sin (ang));
        g.setColour (kBone.withAlpha (alpha * rnd (0.35f, 1.0f)));
        g.strokePath (p, PathStrokeType (rnd (0.35f, 0.9f), PathStrokeType::curved, PathStrokeType::rounded));
    }
    for (int i = 0; i < fibres / 6; ++i)
    {
        g.setColour (Colours::black.withAlpha (alpha * rnd (0.6f, 1.6f)));
        const float s = rnd (0.6f, 1.8f);
        g.fillEllipse (rnd (r.getX(), r.getRight()), rnd (r.getY(), r.getBottom()), s, s);
    }
}

// sumi-e mountains: ridges of thinned ink, the far ones paler, each fading into mist at its foot
inline void drawInkMountains (juce::Graphics& g, juce::Rectangle<float> r, float alpha, unsigned seed = 11)
{
    using namespace juce;
    Rng rnd (seed);
    for (int layer = 0; layer < 3; ++layer)
    {
        const float depth = (float) layer / 2.0f;   // 0 far .. 1 near
        const float base = r.getY() + r.getHeight() * (0.42f + 0.22f * depth);
        Path p;
        p.startNewSubPath (r.getX(), r.getBottom());
        float y = base;
        const int n = 48;
        float peakPhase = rnd (0.0f, 6.28f);
        for (int i = 0; i <= n; ++i)
        {
            const float t = (float) i / n, x = r.getX() + t * r.getWidth();
            const float ridge = 0.5f + 0.5f * std::sin (peakPhase + t * (5.0f + 3.0f * depth)) * std::sin (1.3f + t * (2.1f + depth));
            y = base - r.getHeight() * (0.18f + 0.30f * (1.0f - depth)) * ridge + rnd (-1.5f, 1.5f);
            // the range rises out of the ground and sinks back into it: no edge at either end
            const float w = std::pow (jmax (0.0f, std::sin (MathConstants<float>::pi * t)), 0.7f);   // sin (pi) rounds below 0: NaN
            p.lineTo (x, r.getBottom() - (r.getBottom() - y) * w);
        }
        p.lineTo (r.getRight(), r.getBottom());
        p.closeSubPath();
        const auto ink = kMist.interpolatedWith (kInk, 0.35f + 0.5f * depth);
        g.setGradientFill (ColourGradient (ink.withAlpha (alpha * (0.55f + 0.45f * depth)), 0.0f, base - r.getHeight() * 0.4f,
                                           ink.withAlpha (0.0f), 0.0f, r.getBottom(), false));
        g.fillPath (p);
    }
}

// a brush ensō: one stroke around the circle, heavy where the brush lands, thinning and breaking into dry streaks
// before it closes (the open gap at the top right)
inline void drawEnso (juce::Graphics& g, juce::Point<float> c, float radius, float weight, juce::Colour ink, unsigned seed = 3)
{
    using namespace juce;
    Rng rnd (seed);
    const float start = -2.25f, sweep = 2.0f * MathConstants<float>::pi * 0.91f;   // starts lower left, leaves a gap
    const int n = 220;
    auto width = [&] (float t) { return weight * (0.35f + 1.15f * std::sin (MathConstants<float>::pi * std::pow (t, 0.65f)) * (1.0f - 0.55f * t)); };
    auto rad = [&] (float t) { return radius * (1.0f + 0.018f * std::sin (t * 9.0f + 0.7f) + 0.012f * std::sin (t * 23.0f)); };
    // the body
    Path body;
    for (int i = 0; i <= n; ++i)
    {
        const float t = (float) i / n, a = start + sweep * t, rr = rad (t) + 0.5f * width (t);
        const auto pt = c + Point<float> (std::cos (a), std::sin (a)) * rr;
        i == 0 ? body.startNewSubPath (pt) : body.lineTo (pt);
    }
    for (int i = n; i >= 0; --i)
    {
        const float t = (float) i / n, a = start + sweep * t, rr = rad (t) - 0.5f * width (t);
        body.lineTo (c + Point<float> (std::cos (a), std::sin (a)) * rr);
    }
    body.closeSubPath();
    g.setColour (ink.withAlpha (ink.getFloatAlpha() * 0.25f));
    g.strokePath (body, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));   // the ink's bleed
    g.setColour (ink);
    g.fillPath (body);
    // dry brush: hairline gaps along the stroke's tail, where the brush runs out of ink
    for (int k = 0; k < 7; ++k)
    {
        const float off = rnd (-0.42f, 0.42f), t0 = rnd (0.45f, 0.75f);
        Path streak;
        for (int i = 0; i <= 40; ++i)
        {
            const float t = t0 + (1.0f - t0) * (float) i / 40.0f, a = start + sweep * t;
            const auto pt = c + Point<float> (std::cos (a), std::sin (a)) * (rad (t) + off * width (t));
            i == 0 ? streak.startNewSubPath (pt) : streak.lineTo (pt);
        }
        g.setColour (Colour (0xff15100f).withAlpha (0.85f));
        g.strokePath (streak, PathStrokeType (rnd (0.5f, 1.3f)));
    }
}

// a brush arc (an ensō drawn part of the way): from `start` (radians) over `sweep` (signed); the brush lands heavy and
// thins toward its end, a few dry streaks in the tail; a tiny sweep is a drop of ink
inline void drawEnsoArc (juce::Graphics& g, juce::Point<float> c, float radius, float weight, juce::Colour ink, float start, float sweep)
{
    using namespace juce;
    const float span = std::abs (sweep), dir = sweep < 0.0f ? -1.0f : 1.0f;
    if (span < 0.12f)
    {
        const auto p = c + Point<float> (std::cos (start), std::sin (start)) * radius;
        g.setColour (ink);
        g.fillEllipse (Rectangle<float> (1.1f * weight, 1.1f * weight).withCentre (p));
        return;
    }
    const int n = jmax (12, (int) (span * 40.0f));
    auto width = [&] (float t) { return weight * (0.45f + 0.75f * std::sin (MathConstants<float>::pi * std::pow (jmax (0.02f, t), 0.55f) * 0.5f + 0.5f) * (1.0f - 0.6f * t)); };
    auto rad = [&] (float t) { return radius * (1.0f + 0.02f * std::sin (t * span * 3.0f + 0.7f)); };
    Path body;
    for (int i = 0; i <= n; ++i)
    {
        const float t = (float) i / n, a = start + dir * span * t;
        const auto pt = c + Point<float> (std::cos (a), std::sin (a)) * (rad (t) + 0.5f * width (t));
        i == 0 ? body.startNewSubPath (pt) : body.lineTo (pt);
    }
    for (int i = n; i >= 0; --i)
    {
        const float t = (float) i / n, a = start + dir * span * t;
        body.lineTo (c + Point<float> (std::cos (a), std::sin (a)) * (rad (t) - 0.5f * width (t)));
    }
    body.closeSubPath();
    g.setColour (ink.withMultipliedAlpha (0.3f));
    g.strokePath (body, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (ink);
    g.fillPath (body);
    if (span > 0.8f)
        for (int k = 0; k < 3; ++k)
        {
            const float off = -0.3f + 0.3f * k, t0 = 0.62f + 0.08f * k;
            Path streak;
            for (int i = 0; i <= 20; ++i)
            {
                const float t = t0 + (1.0f - t0) * (float) i / 20.0f, a = start + dir * span * t;
                const auto pt = c + Point<float> (std::cos (a), std::sin (a)) * (rad (t) + off * width (t));
                i == 0 ? streak.startNewSubPath (pt) : streak.lineTo (pt);
            }
            g.setColour (Colour (0xff120d0d).withAlpha (0.75f));
            g.strokePath (streak, PathStrokeType (0.6f));
        }
}

// a vertical seal with two kanji stacked (the classic layout of a painter's name seal), cut out in bone
inline void drawTallSeal (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& top, const juce::String& bottom)
{
    using namespace juce;
    const float s = r.getWidth();
    Path p;
    p.addRoundedRectangle (r, 0.1f * s);
    DropShadow (Colours::black.withAlpha (0.5f), (int) (0.25f * s), { 0, (int) (0.06f * s) + 1 }).drawForPath (g, p);
    g.setGradientFill (ColourGradient (kVermilion.brighter (0.1f), r.getX(), r.getY(), kVermilion.darker (0.4f), r.getRight(), r.getBottom(), false));
    g.fillPath (p);
    static const float flecks[][3] = { { 0.1f, 0.03f, 0.08f }, { 0.9f, 0.2f, 0.06f }, { 0.05f, 0.55f, 0.07f }, { 0.7f, 0.97f, 0.08f }, { 0.95f, 0.75f, 0.05f } };
    g.setColour (Colours::black.withAlpha (0.25f));
    for (auto& f : flecks)
        g.fillEllipse (r.getX() + f[0] * s - 0.5f * f[2] * s, r.getY() + f[1] * r.getHeight() - 0.5f * f[2] * s, f[2] * s, f[2] * s);
    g.setColour (kBone.withAlpha (0.5f));
    g.drawRoundedRectangle (r.reduced (0.09f * s), 0.07f * s, jmax (0.8f, 0.045f * s));
    auto cell = r.reduced (0.14f * s);
    for (int i = 0; i < 2; ++i)
    {
        auto box = i == 0 ? cell.removeFromTop (cell.getHeight() * 0.5f) : cell;
        auto k = axell::brand::brushPath (i == 0 ? top : bottom, 100.0f);
        const auto b = k.getBounds();
        const float sc = 0.86f * jmin (box.getWidth() / b.getWidth(), box.getHeight() / b.getHeight());
        k.applyTransform (AffineTransform::translation (-b.getCentreX(), -b.getCentreY()).scaled (sc).translated (box.getCentreX(), box.getCentreY()));
        g.setColour (kBone);
        axell::brand::fillInk (g, k, 0.02f * s);
    }
}

// the two seals interlocked: 禁 square, 二天 tall, the second stamped over the first's corner at a slant; `glow` 0..1
// (HEAT) lights them from behind like embers
inline void drawSeals (juce::Graphics& g, juce::Rectangle<float> square, float glow)
{
    using namespace juce;
    const float s = square.getWidth();
    if (glow > 0.01f)
    {
        const auto c = square.getCentre().translated (0.45f * s, 0.15f * s);
        g.setGradientFill (ColourGradient (kVermilion.withAlpha (0.55f * glow), c.x, c.y, kVermilion.withAlpha (0.0f), c.x + 1.9f * s, c.y, true));
        g.fillEllipse (Rectangle<float> (3.8f * s, 3.0f * s).withCentre (c));
    }
    axell::brand::drawSeal (g, square, String (CharPointer_UTF8 ("\xe7\xa6\x81")));   // 禁
    Graphics::ScopedSaveState save (g);
    const auto tall = Rectangle<float> (0.72f * s, 1.32f * s).withPosition (square.getRight() - 0.3f * s, square.getY() + 0.18f * s);
    g.addTransform (AffineTransform::rotation (0.12f, tall.getCentreX(), tall.getCentreY()));
    drawTallSeal (g, tall, String (CharPointer_UTF8 ("\xe4\xba\x8c")), String (CharPointer_UTF8 ("\xe5\xa4\xa9")));   // 二 天
}

// a panel's title: the ring's kanji in brush ink, a short brush stroke under it, the English name in small caps
inline void drawRingTitle (juce::Graphics& g, float x, float y, const juce::String& kanji, const juce::String& name, juce::Colour accent,
                           float size = 15.0f)
{
    using namespace juce;
    auto k = axell::brand::brushPath (kanji, size);
    const auto b = k.getBounds();
    k.applyTransform (AffineTransform::translation (x - b.getX(), y + size - b.getBottom()));
    g.setColour (accent.interpolatedWith (kBone, 0.45f).withAlpha (0.95f));
    axell::brand::fillInk (g, k, 0.023f * size);
    Path stroke;
    const float sx = x + b.getWidth() + 0.5f * size, my = y + 0.6f * size;
    stroke.startNewSubPath (sx, my);
    stroke.quadraticTo (sx + 0.9f * size, my - 0.1f * size, sx + 1.9f * size, my + 0.04f * size);
    g.setColour (accent.withAlpha (0.6f));
    g.strokePath (stroke, PathStrokeType (0.1f * size, PathStrokeType::curved, PathStrokeType::rounded));
    g.setFont (ui::caps (0.56f * size, 1));
    g.setColour (kBone.withAlpha (0.55f));
    g.drawText (name, Rectangle<float> (sx + 2.3f * size, my - 0.45f * size, 260.0f, 0.9f * size), Justification::centredLeft);
}
} // namespace art
