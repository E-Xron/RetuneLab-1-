#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace Theme
{
    const juce::Colour bg      { 0xff0b0d12 };
    const juce::Colour panel   { 0xff12151c };
    const juce::Colour panel2  { 0xff1b1f28 };
    const juce::Colour border  { 0xff262b36 };
    const juce::Colour text    { 0xffeef1f6 };
    const juce::Colour dim     { 0xff8a93a3 };
    const juce::Colour accent  { 0xff6c8cff };
    const juce::Colour accent2 { 0xff38d6c4 };
    const juce::Colour warm    { 0xffffa24d };
    const juce::Colour red     { 0xffff4f5e };
}

class ModernLAF : public juce::LookAndFeel_V4
{
public:
    ModernLAF()
    {
        using namespace Theme;
        setColour (juce::ResizableWindow::backgroundColourId, panel);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::backgroundColourId, panel2);
        setColour (juce::PopupMenu::backgroundColourId, panel2);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::Label::textColourId, text);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (13.f, juce::Font::bold)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (13.f, juce::Font::bold)); }
    juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (14.f)); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hl, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const float rad = 7.f;
        const bool cl = b.isConnectedOnLeft(), cr = b.isConnectedOnRight();
        juce::Path shape;
        shape.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), rad, rad, ! cl, ! cr, ! cl, ! cr);

        if (b.getProperties()["record"])                          // record: dark pill with a red lamp
        {
            g.setColour (on ? Theme::red.withAlpha (0.18f) : Theme::panel2.brighter (hl ? 0.08f : 0.f));
            g.fillPath (shape);
            g.setColour (on ? Theme::red.withAlpha (0.7f) : Theme::border);
            g.strokePath (shape, juce::PathStrokeType (1.f));
            const float d = 10.f, cx = r.getX() + 16.f, cy = r.getCentreY();
            if (on)
                for (int i = 3; i >= 1; --i)
                {
                    g.setColour (Theme::red.withAlpha (0.12f * (float) i));
                    g.fillEllipse (cx - d * 0.5f - (float) (4 - i) * 2.f, cy - d * 0.5f - (float) (4 - i) * 2.f, d + (float) (4 - i) * 4.f, d + (float) (4 - i) * 4.f);
                }
            g.setColour (on ? Theme::red : Theme::red.withAlpha (0.65f));
            g.fillEllipse (cx - d * 0.5f, cy - d * 0.5f, d, d);
            return;
        }

        juce::Colour top = on ? b.findColour (juce::TextButton::buttonOnColourId) : Theme::panel2;
        if (hl)   top = top.brighter (0.10f);
        if (down) top = top.darker (0.15f);
        g.setGradientFill (juce::ColourGradient (top.brighter (0.06f), 0.f, r.getY(), top.darker (0.08f), 0.f, r.getBottom(), false));
        g.fillPath (shape);
        g.setColour (on ? top.brighter (0.35f).withAlpha (0.8f) : Theme::border);
        g.strokePath (shape, juce::PathStrokeType (1.f));
        g.setColour (juce::Colours::white.withAlpha (on ? 0.18f : 0.05f));      // top sheen
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + (cl ? 0.f : rad), r.getRight() - (cr ? 0.f : rad));
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        auto r = b.getLocalBounds();
        if (b.getProperties()["record"]) r.removeFromLeft (22);
        g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                      .withMultipliedAlpha (b.isEnabled() ? 1.f : 0.5f));
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.drawText (b.getButtonText(), r, juce::Justification::centred);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        auto r = juce::Rectangle<float> (0.f, 0.f, (float) w, (float) h).reduced (0.5f);
        g.setGradientFill (juce::ColourGradient (Theme::panel2.brighter (0.05f), 0.f, 0.f, Theme::panel2.darker (0.08f), 0.f, (float) h, false));
        g.fillRoundedRectangle (r, 7.f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (r, 7.f, 1.f);
        juce::Path p;
        const float cx = (float) w - 14.f, cy = (float) h * 0.5f;
        p.addTriangle (cx - 4.f, cy - 2.f, cx + 4.f, cy - 2.f, cx, cy + 3.f);
        g.setColour (Theme::dim);
        g.fillPath (p);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float a0, float a1, juce::Slider&) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (8.f);
        const float rad = std::min (b.getWidth(), b.getHeight()) * 0.5f, lw = 6.f;
        const auto c = b.getCentre();
        const float ang = a0 + pos * (a1 - a0), r2 = rad - lw * 0.5f;

        juce::Path track, val;
        track.addCentredArc (c.x, c.y, r2, r2, 0.f, a0, a1, true);
        val.addCentredArc   (c.x, c.y, r2, r2, 0.f, a0, ang, true);
        g.setColour (Theme::border);
        g.strokePath (track, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Theme::accent);
        g.strokePath (val, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white);
        g.fillEllipse (c.x + r2 * std::sin (ang) - 4.f, c.y - r2 * std::cos (ang) - 4.f, 8.f, 8.f);
    }
};
