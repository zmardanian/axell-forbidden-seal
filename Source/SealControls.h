#pragma once
// Axell Forbidden Seal 4.0 - the controls, all drawn in code: the KATANA (DRIVE: the blade drawn out of its scabbard,
// heating from steel to red as it is pushed, its temper line rippling with what the circuit makes), the ENSŌ (every
// other continuous control: a brush circle that closes as the value rises), the STAMP (switches: a hanko seal, inked when
// on), the TABS (a choice: brush-underlined words). Every control: drag (Shift: fine), mouse wheel, double-click to the
// default, right-click to type a value, the host's automation and undo gestures.
#include "MusashiArt.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <functional>
#include <memory>

namespace ctl
{
using namespace juce;

// the common slider behaviour (binding, fine drag, reset, typed values)
class InkSlider : public Slider
{
public:
    InkSlider (const String& caption, Colour accent, SliderStyle s) : Slider (s, NoTextBox), name (caption), tint (accent)
    {
        setVelocityModeParameters (0.4, 1, 0.02, true, ModifierKeys::shiftModifier);   // Shift: fine
        setScrollWheelEnabled (true);
        setPaintingIsUnclipped (true);
    }
    void bind (AudioProcessorValueTreeState& apvts, const String& id, const String& tip)
    {
        if (auto* p = apvts.getParameter (id))
        {
            param = p;
            attach = std::make_unique<SliderParameterAttachment> (*p, *this, apvts.undoManager);
            setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()));
        }
        setTooltip (tip + "\n(drag; Shift: fine; double-click: default; right-click: type a value)");
    }
    void setCaption (const String& c) { name = c; repaint(); }
    String valueText() { return param != nullptr ? param->getCurrentValueAsText() + suffix() : getTextFromValue (getValue()); }
    float proportion() { return (float) valueToProportionOfLength (getValue()); }
    void mouseDown (const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            typeValue();
            return;
        }
        Slider::mouseDown (e);
    }

protected:
    String suffix() const
    {
        if (param == nullptr) return {};
        const auto l = param->getLabel();
        return l.isNotEmpty() && ! param->getCurrentValueAsText().endsWith (l) ? " " + l : String();
    }
    void typeValue()
    {
        auto* w = new AlertWindow (name, "Type a value", MessageBoxIconType::NoIcon);
        w->addTextEditor ("v", param != nullptr ? param->getCurrentValueAsText() : String (getValue()));
        w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        SafePointer<InkSlider> safe (this);
        w->enterModalState (true, ModalCallbackFunction::create ([safe, w] (int r) {
            if (safe != nullptr && r == 1 && safe->param != nullptr)
            {
                const float v = safe->param->getValueForText (w->getTextEditorContents ("v"));
                safe->setValue (safe->param->convertFrom0to1 (v), sendNotificationSync);
            }
        }), true);
    }
    String name;
    Colour tint;
    RangedAudioParameter* param = nullptr;
    std::unique_ptr<SliderParameterAttachment> attach;
};

// the ENSŌ: a brush circle drawn as far as the value goes (a dot of ink at the minimum, nearly closed at the maximum),
// a faint pencil circle where it will go; the value in the middle, the name under it
class Enso : public InkSlider
{
public:
    Enso (const String& caption, Colour accent, float diameter = 56.0f)
        : InkSlider (caption, accent, RotaryVerticalDrag), dia (diameter)
    {
        setMouseDragSensitivity (180);
    }
    // bipolar controls (TONE, DYNAMICS) draw from the top, both ways
    void setBipolar (bool b) { bipolar = b; repaint(); }
    void paint (Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float d = jmin (dia, b.getWidth() - 4.0f, b.getHeight() - 13.0f);
        const auto c = Point<float> (b.getCentreX(), b.getY() + 2.0f + 0.5f * d);
        const float r = 0.5f * d - 4.0f, t = proportion();
        const bool hot = isMouseOverOrDragging();
        // the pencil guide
        g.setColour (art::kBone.withAlpha (hot ? 0.16f : 0.09f));
        g.drawEllipse (Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c), 1.0f);
        // the stroke
        const float w = jmax (2.2f, 0.15f * d);
        if (bipolar)
        {
            const float s = 2.0f * t - 1.0f;   // -1 .. 1
            art::drawEnsoArc (g, c, r, w, tint.withAlpha (isEnabled() ? 0.95f : 0.3f), -MathConstants<float>::halfPi, s * 0.92f * MathConstants<float>::pi);
        }
        else
            art::drawEnsoArc (g, c, r, w, tint.withAlpha (isEnabled() ? 0.95f : 0.3f), -MathConstants<float>::halfPi - 0.35f, jmax (0.02f, t) * 1.84f * MathConstants<float>::pi);
        // the value inside, the name under
        g.setColour (art::kBone.withAlpha (isEnabled() ? 0.95f : 0.4f));
        const float vf = jlimit (7.0f, 11.0f, 0.235f * d);
        g.setFont (ui::font (vf, Font::bold));
        auto txt = valueText();
        if (d < 31.0f)   // a small ensō: the value without its unit (the caption says what it is)
            txt = txt.replace (" kHz", "k").replace (" Hz", "").replace (" dB", "").replace (" %", "%");
        g.drawFittedText (txt, Rectangle<int> ((int) (c.x - r + 1), (int) (c.y - 0.6f * vf), (int) (2 * r - 2), (int) (1.2f * vf + 1)), Justification::centred, 1, 0.7f);
        const float cf = jlimit (6.8f, 9.0f, 0.2f * d + 0.4f);
        g.setFont (ui::caps (cf, 1));
        g.setColour (art::kBone.withAlpha (isEnabled() ? (hot ? 0.9f : 0.6f) : 0.3f));
        g.drawText (name, Rectangle<float> (b.getX() - 6.0f, c.y + 0.5f * d + 1.0f, b.getWidth() + 12.0f, cf + 4.0f), Justification::centred);
    }

private:
    float dia;
    bool bipolar = false;
};

// the KATANA: the hilt at the left, the blade drawn out to the right as far as the value goes, the rest of it in the
// lacquered scabbard. The drawn steel heats with the value (steel - straw - red, white-hot when punished); the temper line
// (hamon) ripples with HEAT
class Katana : public InkSlider
{
public:
    explicit Katana (const String& caption) : InkSlider (caption, art::kVermilion, LinearHorizontal)
    {
        setSliderSnapsToMousePosition (false);
        setMouseDragSensitivity (360);
    }
    void setLive (float heatDb, bool punished, float dynDb)
    {
        const float h = jlimit (0.0f, 1.0f, (heatDb + 48.0f) / 44.0f);
        if (std::abs (h - heat) > 0.01f || punished != punish || std::abs (dynDb - dyn) > 0.2f)
        {
            heat = h;
            punish = punished;
            dyn = dynDb;
            repaint();
        }
    }
    void paint (Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float midY = b.getCentreY() + 0.1f * b.getHeight(), bh = jmin (19.0f, 0.27f * b.getHeight());
        const float hiltL = b.getX() + 4.0f, hiltR = hiltL + 0.17f * b.getWidth(), tsubaX = hiltR + 5.0f;
        const float tip = b.getRight() - 6.0f, len = tip - tsubaX - 10.0f;
        const float t = proportion(), drawn = tsubaX + 10.0f + t * (len - 34.0f) + 6.0f;   // the scabbard's mouth
        // the hilt: wrapped in silk (diamonds), a pommel cap
        Rectangle<float> hilt (hiltL, midY - 0.55f * bh, hiltR - hiltL, 1.1f * bh);
        g.setColour (Colour (0xff1a1414));
        g.fillRoundedRectangle (hilt, 3.0f);
        g.setColour (art::kVermilion.darker (0.5f).withAlpha (0.95f));
        for (float x = hilt.getX() + 6.0f; x < hilt.getRight() - 6.0f; x += 9.0f)
        {
            Path dmd;
            dmd.addTriangle (x, hilt.getCentreY(), x + 4.5f, hilt.getY() + 2.0f, x + 9.0f, hilt.getCentreY());
            dmd.addTriangle (x, hilt.getCentreY(), x + 4.5f, hilt.getBottom() - 2.0f, x + 9.0f, hilt.getCentreY());
            g.fillPath (dmd);
        }
        g.setColour (Colour (0xffb8934a));
        g.fillRoundedRectangle (hilt.withWidth (5.0f), 2.0f);
        // the guard (tsuba): a dark iron disc with a gold rim
        g.setColour (Colour (0xff2a2622));
        g.fillEllipse (Rectangle<float> (10.0f, 2.2f * bh).withCentre ({ tsubaX + 2.0f, midY }));
        g.setColour (Colour (0xffb8934a).withAlpha (0.8f));
        g.drawEllipse (Rectangle<float> (10.0f, 2.2f * bh).withCentre ({ tsubaX + 2.0f, midY }), 1.2f);
        // the blade: from the guard to the tip (curved a little: sori), the drawn part shown, the rest under the scabbard
        auto bladePath = [&] (float x0, float x1) {
            Path p;
            const int n = 40;
            for (int i = 0; i <= n; ++i)
            {
                const float x = x0 + (x1 - x0) * i / n, u = (x - tsubaX) / (tip - tsubaX);
                const float spine = midY - 0.42f * bh - 3.0f * u * u, thick = 0.84f * bh * (u > 0.93f ? (1.0f - u) / 0.07f : 1.0f);
                i == 0 ? p.startNewSubPath (x, spine) : p.lineTo (x, spine);
                (void) thick;
            }
            for (int i = n; i >= 0; --i)
            {
                const float x = x0 + (x1 - x0) * i / n, u = (x - tsubaX) / (tip - tsubaX);
                const float spine = midY - 0.42f * bh - 3.0f * u * u, thick = 0.84f * bh * (u > 0.93f ? jmax (0.0f, (1.0f - u) / 0.07f) : 1.0f);
                p.lineTo (x, spine + thick);
            }
            p.closeSubPath();
            return p;
        };
        const Colour steel (0xffc9ccd2), straw (0xffe8c06a), red (0xffe0413a), white (0xfffff1d6);
        const float heatT = jlimit (0.0f, 1.0f, 0.55f * t + 0.6f * heat);
        Colour hotC = heatT < 0.5f ? steel.interpolatedWith (straw, heatT * 2.0f) : straw.interpolatedWith (red, (heatT - 0.5f) * 2.0f);
        if (punish)
            hotC = hotC.interpolatedWith (white, 0.55f);
        const auto exposed = bladePath (tsubaX + 4.0f, jmin (drawn, tip));
        if (heatT > 0.2f || punish)
        {
            g.setColour (hotC.withAlpha (0.22f * (punish ? 1.6f : heatT)));
            g.strokePath (exposed, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        g.setGradientFill (ColourGradient (steel.darker (0.1f), tsubaX, midY - bh, hotC, drawn, midY, false));
        g.fillPath (exposed);
        {
            // the groove along the back (bo-hi), and the polished edge's light
            Path hi;
            const float x1 = jmin (drawn, tip) - 8.0f;
            for (float x = tsubaX + 12.0f; x <= x1; x += 3.0f)
            {
                const float u = (x - tsubaX) / (tip - tsubaX), y = midY - 0.42f * bh - 3.0f * u * u + 0.22f * bh;
                x <= tsubaX + 12.5f ? hi.startNewSubPath (x, y) : hi.lineTo (x, y);
            }
            g.setColour (Colours::black.withAlpha (0.22f));
            g.strokePath (hi, PathStrokeType (1.6f));
        }
        // the temper line (hamon): a wave along the edge; its amplitude follows HEAT
        {
            Path hamon;
            const float amp = 1.0f + 3.2f * heat, x1 = jmin (drawn, tip) - 4.0f;
            for (float x = tsubaX + 6.0f; x <= x1; x += 2.0f)
            {
                const float u = (x - tsubaX) / (tip - tsubaX);
                const float y = midY - 0.42f * bh - 3.0f * u * u + 0.52f * bh + amp * std::sin (x * 0.42f) * (0.6f + 0.4f * std::sin (x * 0.11f));
                x <= tsubaX + 6.5f ? hamon.startNewSubPath (x, y) : hamon.lineTo (x, y);
            }
            g.setColour (Colours::white.withAlpha (0.55f));
            g.strokePath (hamon, PathStrokeType (1.1f));
        }
        // the scabbard (saya): black lacquer with a gold mouth and tip, the value inlaid in gold
        Rectangle<float> saya (drawn - 6.0f, midY - 0.62f * bh - 3.0f, tip + 4.0f - drawn + 6.0f, 1.24f * bh + 4.0f);
        if (saya.getWidth() > 4.0f)
        {
            ColourGradient lac (Colour (0xff3a1416), 0.0f, saya.getY(), Colour (0xff0b0909), 0.0f, saya.getBottom(), false);
            lac.addColour (0.35, Colour (0xff1c0f10));
            g.setGradientFill (lac);
            g.fillRoundedRectangle (saya, 0.5f * saya.getHeight());
            g.setColour (Colours::white.withAlpha (0.09f));
            g.drawLine (saya.getX() + 10.0f, saya.getY() + 3.0f, saya.getRight() - 10.0f, saya.getY() + 3.0f, 1.2f);
            g.setColour (Colour (0xffb8934a));
            g.fillRect (saya.withWidth (5.0f));                                        // the mouth (koiguchi)
            g.fillRoundedRectangle (saya.withTrimmedLeft (saya.getWidth() - 9.0f), 4.0f);   // the end cap (kojiri)
            if (saya.getWidth() > 60.0f)
            {
                const float kx = saya.getX() + 26.0f;
                g.fillEllipse (kx - 3.5f, saya.getBottom() - 2.0f, 7.0f, 6.0f);           // the cord's knob (kurikata)
                Path cord;                                                              // the cord (sageo), loosely hanging
                cord.startNewSubPath (kx, saya.getBottom() + 3.0f);
                cord.cubicTo (kx + 16.0f, saya.getBottom() + 12.0f, kx + 46.0f, saya.getBottom() + 10.0f, kx + 62.0f, saya.getBottom() + 4.0f);
                g.setColour (art::kVermilion.withAlpha (0.85f));
                g.strokePath (cord, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
            }
        }
        // DYNAMICS: where the drive is right now (a mark on the blade's back)
        if (std::abs (dyn) > 0.3f)
        {
            const float range = (float) (getMaximum() - getMinimum());
            const float x = jlimit (tsubaX + 8.0f, tip - 8.0f, drawn + (float) (dyn / range) * (len - 34.0f));
            g.setColour (art::kBone.withAlpha (0.8f));
            g.fillEllipse (x - 2.5f, midY - 0.42f * bh - 9.0f, 5.0f, 5.0f);
        }
        // the name and the value above the blade
        const float tf = jlimit (7.5f, 10.0f, 0.17f * b.getHeight());
        g.setFont (ui::caps (tf, 1));
        g.setColour (art::kBone.withAlpha (isMouseOverOrDragging() ? 0.95f : 0.7f));
        g.drawText (name, Rectangle<float> (hiltL, b.getY(), 120.0f, tf + 4.0f), Justification::centredLeft);
        g.setFont (ui::font (1.35f * tf, Font::bold));
        g.setColour (hotC.brighter (0.2f));
        g.drawText (valueText(), Rectangle<float> (b.getRight() - 150.0f, b.getY() - 2.0f, 146.0f, 1.35f * tf + 5.0f), Justification::centredRight);
    }

private:
    float heat = 0.0f, dyn = 0.0f;
    bool punish = false;
};

// the STAMP: a switch as a hanko seal - vermilion and inked when on, a pale impression when off
class Stamp : public ToggleButton
{
public:
    Stamp (const String& caption, Colour ink = art::kVermilion) : ToggleButton (caption), tint (ink) { setPaintingIsUnclipped (true); }
    void bind (AudioProcessorValueTreeState& apvts, const String& id, const String& tip)
    {
        if (auto* p = apvts.getParameter (id))
            attach = std::make_unique<ButtonParameterAttachment> (*p, *this, apvts.undoManager);
        setTooltip (tip);
    }
    void paintButton (Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const bool on = getToggleState();
        if (down)
            r = r.reduced (1.0f);
        Path p;
        p.addRoundedRectangle (r, 3.0f);
        if (on)
        {
            // inked: always the seal's vermilion (the section's colour only tints the pale impression when off)
            g.setGradientFill (ColourGradient (art::kVermilion.brighter (0.1f), r.getX(), r.getY(), art::kVermilion.darker (0.35f), r.getRight(), r.getBottom(), false));
            g.fillPath (p);
            g.setColour (Colours::black.withAlpha (0.25f));
            g.fillEllipse (r.getX() + 0.8f * r.getWidth(), r.getY() + 1.5f, 3.0f, 3.0f);
            g.setColour (art::kBone.withAlpha (0.55f));
            g.drawRoundedRectangle (r.reduced (2.5f), 2.0f, 0.8f);
        }
        else
        {
            // a pale impression: a little ink, the seal's double border
            g.setColour (tint.withAlpha (over ? 0.14f : 0.07f));
            g.fillPath (p);
            g.setColour (tint.withAlpha (over ? 0.65f : 0.4f));
            g.strokePath (p, PathStrokeType (1.2f));
            g.setColour (tint.withAlpha (over ? 0.35f : 0.2f));
            g.drawRoundedRectangle (r.reduced (2.5f), 2.0f, 0.7f);
        }
        g.setFont (ui::caps (jlimit (7.0f, 9.5f, 0.4f * r.getHeight()), 1));
        g.setColour (on ? art::kBone : art::kBone.withAlpha (over ? 0.8f : 0.5f));
        g.drawText (getButtonText(), r, Justification::centred);
    }

private:
    Colour tint;
    std::unique_ptr<ButtonParameterAttachment> attach;
};

// TABS: a choice parameter as words; the chosen one in bone with a vermilion brush stroke under it
class Tabs : public Component, public SettableTooltipClient
{
public:
    Tabs (StringArray labels, Colour ink = art::kVermilion) : items (std::move (labels)), tint (ink) {}
    void bind (RangedAudioParameter& p, const String& tip, int offset = 0)
    {
        base = offset;
        attach = std::make_unique<ParameterAttachment> (p, [this] (float v) { current = roundToInt (v) - base; repaint(); });
        attach->sendInitialUpdate();
        setTooltip (tip);
    }
    void paint (Graphics& g) override
    {
        const float w = (float) getWidth() / (float) items.size();
        for (int i = 0; i < items.size(); ++i)
        {
            const Rectangle<float> r (i * w, 0.0f, w, (float) getHeight());
            const bool on = i == current, over = i == hover;
            g.setFont (ui::caps (jlimit (7.0f, 10.5f, 0.45f * (float) getHeight()), 1));
            g.setColour (on ? art::kBone : art::kBone.withAlpha (over ? 0.75f : 0.45f));
            g.drawText (items[i], r.withTrimmedBottom (6.0f), Justification::centred);
            if (on)
            {
                Path s;
                const float y = r.getBottom() - 4.0f, x0 = r.getX() + 0.2f * w, x1 = r.getRight() - 0.2f * w;
                s.startNewSubPath (x0, y + 1.0f);
                s.quadraticTo (0.5f * (x0 + x1), y - 1.2f, x1, y + 0.5f);
                g.setColour (tint);
                g.strokePath (s, PathStrokeType (jmax (1.6f, 0.09f * (float) getHeight()), PathStrokeType::curved, PathStrokeType::rounded));
            }
        }
    }
    void mouseMove (const MouseEvent& e) override { setHover (indexAt (e.x)); }
    void mouseExit (const MouseEvent&) override { setHover (-1); }
    void mouseDown (const MouseEvent& e) override
    {
        if (attach != nullptr)
            attach->setValueAsCompleteGesture ((float) (indexAt (e.x) + base));
        else if (onSelect)
            onSelect (indexAt (e.x));
    }
    void setCurrent (int i) { if (i != current) { current = i; repaint(); } }
    std::function<void (int)> onSelect;

private:
    int indexAt (int x) const { return jlimit (0, items.size() - 1, x * items.size() / jmax (1, getWidth())); }
    void setHover (int h) { if (h != hover) { hover = h; repaint(); } }
    StringArray items;
    Colour tint;
    int current = 0, hover = -1, base = 0;
    std::unique_ptr<ParameterAttachment> attach;
};
// the remaining stock widgets in the seal's taste: a dropdown is a word with a fine underline and a small caret, a text
// button is a word (the preset arrows: brush chevrons)
class SealLook : public ui::LookAndFeel
{
public:
    void drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box) override
    {
        const float a = box.isEnabled() ? 1.0f : 0.35f;
        g.setColour (art::kBone.withAlpha (0.18f * a));
        g.drawHorizontalLine (h - 2, 2.0f, (float) w - 2.0f);
        Path caret;
        const float cx = (float) w - 7.0f, cy = 0.5f * (float) h;
        caret.addTriangle (cx - 3.0f, cy - 1.5f, cx + 3.0f, cy - 1.5f, cx, cy + 2.0f);
        g.setColour (art::kBone.withAlpha (0.55f * a));
        g.fillPath (caret);
    }
    Font getComboBoxFont (ComboBox& b) override { return ui::caps (jlimit (7.0f, 10.0f, 0.5f * (float) b.getHeight()), 1); }
    void positionComboBoxText (ComboBox& box, Label& label) override
    {
        label.setBounds (2, 0, box.getWidth() - 14, box.getHeight());
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (Justification::centred);
        label.setColour (Label::textColourId, art::kBone.withAlpha (box.isEnabled() ? 0.85f : 0.35f));
    }
    void drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down) override
    {
        if (b.getButtonText() == "<" || b.getButtonText() == ">")
        {
            Path p;
            const auto r = b.getLocalBounds().toFloat().reduced (0.3f * (float) b.getHeight());
            const bool left = b.getButtonText() == "<";
            p.startNewSubPath (left ? r.getRight() : r.getX(), r.getY());
            p.quadraticTo (left ? r.getX() + 1.0f : r.getRight() - 1.0f, r.getCentreY() - 1.0f, left ? r.getX() : r.getRight(), r.getCentreY());
            p.quadraticTo (left ? r.getX() + 1.5f : r.getRight() - 1.5f, r.getCentreY() + 1.5f, left ? r.getRight() : r.getX(), r.getBottom());
            g.setColour (art::kBone.withAlpha (down ? 1.0f : over ? 0.85f : 0.5f));
            g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
            return;
        }
        if (over || down)
        {
            g.setColour (art::kBone.withAlpha (down ? 0.1f : 0.05f));
            g.fillRoundedRectangle (b.getLocalBounds().toFloat(), 3.0f);
        }
    }
    void drawButtonText (Graphics& g, TextButton& b, bool over, bool) override
    {
        if (b.getButtonText() == "<" || b.getButtonText() == ">")
            return;
        g.setFont (b.getHeight() > 18 ? ui::font (jmin (12.5f, 0.55f * (float) b.getHeight()), Font::plain) : ui::caps (0.45f * (float) b.getHeight(), 1));
        g.setColour (art::kBone.withAlpha (over ? 1.0f : 0.82f));
        g.drawText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), Justification::centred);
        if (b.getHeight() > 18)
        {
            g.setColour (art::kGold.withAlpha (0.35f));
            g.drawHorizontalLine (b.getHeight() - 2, 6.0f, (float) b.getWidth() - 6.0f);
        }
    }
};

// IN / OUT: two fine strokes of ink per meter, gold rising to vermilion near full scale, a held peak; the held maximum in dB
class InkMeter : public Component, public SettableTooltipClient
{
public:
    explicit InkMeter (String caption) : cap (std::move (caption)) {}
    void push (float l, float r, int nch)
    {
        const float v[2] = { l, nch > 1 ? r : l };
        for (int c = 0; c < 2; ++c)
        {
            const float db = 20.0f * std::log10 (jmax (1.0e-6f, v[c]));
            level[c] = jmax (db, level[c] - 1.6f);
            if (db >= hold[c]) { hold[c] = db; holdFrames[c] = 45; }
            else if (--holdFrames[c] < 0) hold[c] = jmax (-100.0f, hold[c] - 1.0f);
            maxHold = jmax (maxHold, db);
        }
        repaint();
    }
    void mouseDown (const MouseEvent&) override { maxHold = -100.0f; repaint(); }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float tf = jlimit (6.5f, 8.5f, 0.32f * b.getWidth());
        g.setFont (ui::caps (tf, 1));
        g.setColour (art::kBone.withAlpha (0.5f));
        g.drawText (cap, b.removeFromTop (tf + 3.0f), Justification::centred);
        auto num = b.removeFromBottom (tf + 4.0f);
        auto bars = b.reduced (0.0f, 2.0f);
        const float bw = jmin (4.0f, 0.22f * bars.getWidth()), gap = 0.18f * bars.getWidth();
        auto toY = [&] (float db) { return bars.getBottom() - bars.getHeight() * jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        for (int c = 0; c < 2; ++c)
        {
            const float x = bars.getCentreX() + (c == 0 ? -gap - bw : gap) * 0.5f + (c == 0 ? -0.5f * bw : 0.5f * bw) - 0.5f * bw;
            const Rectangle<float> track (x, bars.getY(), bw, bars.getHeight());
            g.setColour (art::kBone.withAlpha (0.06f));
            g.fillRoundedRectangle (track, 0.5f * bw);
            const float y = toY (level[c]);
            g.setGradientFill (ColourGradient (art::kGold.withAlpha (0.9f), 0.0f, bars.getBottom(), art::kVermilion, 0.0f, bars.getY(), false));
            g.fillRoundedRectangle (track.withTop (y), 0.5f * bw);
            g.setColour (art::kBone.withAlpha (0.8f));
            g.fillRect (x, toY (hold[c]) - 0.5f, bw, 1.2f);
        }
        g.setColour (art::kBone.withAlpha (0.18f));
        g.drawHorizontalLine ((int) toY (0.0f), bars.getX(), bars.getRight());
        g.setFont (ui::font (tf + 0.5f, Font::bold));
        g.setColour (maxHold > -0.1f ? art::kVermilion : art::kBone.withAlpha (0.7f));
        g.drawText (maxHold > -99.0f ? String (maxHold, 1) : String ("-"), num, Justification::centred);
    }

private:
    String cap;
    float level[2] { -100.0f, -100.0f }, hold[2] { -100.0f, -100.0f }, maxHold = -100.0f;
    int holdFrames[2] {};
};
} // namespace ctl
