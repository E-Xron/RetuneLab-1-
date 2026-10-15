#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace Theme
{
    const juce::Colour bg      { 0xff0e1014 };
    const juce::Colour panel   { 0xff15181e };
    const juce::Colour panel2  { 0xff1d2128 };
    const juce::Colour border  { 0xff2c323c };
    const juce::Colour text    { 0xffe8ebf0 };
    const juce::Colour dim     { 0xff8b93a1 };
    const juce::Colour accent  { 0xff5b8cff };
    const juce::Colour warm    { 0xffffa24d };
    const juce::Colour red     { 0xffff5a5f };
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

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (13.f)); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hl, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        juce::Colour base = on ? b.findColour (juce::TextButton::buttonOnColourId) : Theme::panel2;
        if (hl)   base = base.brighter (0.12f);
        if (down) base = base.darker (0.15f);
        g.setColour (base);
        g.fillRoundedRectangle (r, 6.f);
        g.setColour (on ? base.brighter (0.3f) : Theme::border);
        g.drawRoundedRectangle (r, 6.f, 1.f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        auto r = juce::Rectangle<float> (0.f, 0.f, (float) w, (float) h).reduced (0.5f);
        g.setColour (Theme::panel2);
        g.fillRoundedRectangle (r, 6.f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (r, 6.f, 1.f);
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
