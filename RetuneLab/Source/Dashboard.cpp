#include "Dashboard.h"
#include "PluginEditor.h"
#include <cmath>

using namespace dash;

namespace
{
    constexpr float kPi = 3.14159265358979f;

    float toDb (float g) { return g > 1e-6f ? 20.f * std::log10 (g) : -120.f; }

    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    double parseValue (Fmt f, const juce::String& t)
    {
        const double v = t.retainCharacters ("0123456789.-").getDoubleValue();
        if (f == Fmt::Pct || f == Fmt::Pct200) return v / 100.0;
        if (f == Fmt::Hz && t.containsIgnoreCase ("k")) return v * 1000.0;
        return v;
    }

    // small Phillips screw for the module corners
    void screw (juce::Graphics& g, float x, float y)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (x - 4.5f, y - 3.5f, 9.f, 9.f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9aa1ad), x - 3.f, y - 3.f, juce::Colour (0xff3a3f48), x + 3.f, y + 3.f, false));
        g.fillEllipse (x - 4.f, y - 4.f, 8.f, 8.f);
        g.setColour (juce::Colour (0xff22262d));
        g.drawLine (x - 2.5f, y, x + 2.5f, y, 1.2f);
        g.drawLine (x, y - 2.5f, x, y + 2.5f, 1.2f);
    }

    // segmented LED bar, value 0..1, vertical or horizontal
    void ledBar (juce::Graphics& g, juce::Rectangle<float> r, float v, juce::Colour on, bool vertical, bool rainbow = false)
    {
        const int segs = vertical ? juce::jmax (6, (int) (r.getHeight() / 5.f)) : juce::jmax (6, (int) (r.getWidth() / 5.f));
        for (int i = 0; i < segs; ++i)
        {
            const float f0 = (float) i / (float) segs, f1 = (float) (i + 1) / (float) segs;
            juce::Rectangle<float> s = vertical
                ? juce::Rectangle<float> (r.getX(), r.getBottom() - f1 * r.getHeight() + 1.f, r.getWidth(), r.getHeight() / (float) segs - 1.5f)
                : juce::Rectangle<float> (r.getX() + f0 * r.getWidth(), r.getY(), r.getWidth() / (float) segs - 1.5f, r.getHeight());
            juce::Colour c = on;
            if (rainbow) c = f0 > 0.88f ? juce::Colour (0xffff4d4d) : (f0 > 0.7f ? juce::Colour (0xffffc23d) : juce::Colour (0xff3ddc84));
            const bool lit = f0 < v;
            g.setColour (lit ? c : c.withAlpha (0.09f));
            g.fillRoundedRectangle (s, 1.f);
            if (lit)
            {
                g.setColour (c.withAlpha (0.25f));
                g.fillRoundedRectangle (s.expanded (1.f), 2.f);
            }
        }
    }

    float logX (float hz, juce::Rectangle<float> r)
    {
        const float a = std::log (100.f), b = std::log (20000.f);
        return r.getX() + r.getWidth() * (std::log (juce::jlimit (100.f, 20000.f, hz)) - a) / (b - a);
    }
}

// ===================================================================== module table
const std::vector<ModuleDef>& dash::modules()
{
    static const std::vector<ModuleDef> m {
        { "NOTE GLIDE", "portamento between notes", "glideOn", 0xffff6fae, Vis::Glide, 1,
          { { nullptr, "Time", Fmt::Ms, 0, 0.0, 500.0, 0.0 }, { nullptr, "Smooth", Fmt::Pct, 1, 0.0, 1.0, 0.5 } } },
        { "DRIFT & VIBRATO", "reshape the sung pitch", "shapeOn", 0xffb48cff, Vis::Drift, 1,
          { { nullptr, "Drift", Fmt::Pct200, 2, 0.0, 2.0, 1.0 }, { nullptr, "Vibrato", Fmt::Pct200, 3, 0.0, 2.0, 1.0 } } },
        { "DE-ESSER", "tames harsh S / T / SH", "deessOn", 0xff35d0c0, Vis::DeEss, 1,
          { { "deessThresh", "Thresh", Fmt::Db }, { "deessFreq", "Freq", Fmt::Hz }, { "deessRange", "Range", Fmt::Db } } },
        { "AIR", "presence & sheen", "airOn", 0xff7fd8ff, Vis::Air, 1,
          { { "midAir", "Mid Air", Fmt::Pct }, { "highAir", "High Air", Fmt::Pct } } },
        { "COMPRESSOR", "smooth opto-style levelling", "compOn", 0xffffb547, Vis::Comp, 2,
          { { "compThresh", "Thresh", Fmt::Db }, { "compRatio", "Ratio", Fmt::Ratio }, { "compAttack", "Attack", Fmt::Ms },
            { "compRelease", "Release", Fmt::Ms }, { "compMix", "Mix", Fmt::Pct } } },
        { "CHOIR", "bass / alto / soprano ensemble", "choirOn", 0xffe8c26a, Vis::Choir, 2,
          { { "choirSize", "Size", Fmt::Pct }, { "choirBass", "Bass Share", Fmt::Pct }, { "choirAlto", "Alto Share", Fmt::Pct },
            { "choirSop", "Sop Share", Fmt::Pct }, { "choirMix", "Mix", Fmt::Pct }, { "choirBassVol", "Bass Vol", Fmt::Db },
            { "choirAltoVol", "Alto Vol", Fmt::Db }, { "choirSopVol", "Sop Vol", Fmt::Db }, { "choirRoom", "Hall", Fmt::Pct } },
          "choirHarmony", "Alto harmony" },
        { "DOUBLER", "double-tracked width", "doubleOn", 0xff6be38a, Vis::Doubler, 1,
          { { "doubleWidth", "Width", Fmt::Pct }, { "doubleDetune", "Detune", Fmt::Cents }, { "doubleMix", "Mix", Fmt::Pct } } },
        { "ECHO", "tempo-synced tape echo", "echoOn", 0xffff8a4c, Vis::Echo, 1,
          { { "echoTime", "Time", Fmt::Division }, { "echoFeedback", "Feedback", Fmt::Pct }, { "echoTone", "Tone", Fmt::Hz },
            { "echoMix", "Mix", Fmt::Pct } } },
        { "REVERB", "room & plate space", "verbOn", 0xff5f8bff, Vis::Reverb, 1,
          { { "verbSize", "Size", Fmt::Pct }, { "verbDamp", "Damp", Fmt::Pct }, { "verbWidth", "Width", Fmt::Pct },
            { "verbMix", "Mix", Fmt::Pct } } },
        { "OUTPUT", "levels in / out", nullptr, 0xff9aa4b5, Vis::Output, 1, {} },
    };
    return m;
}

juce::String dash::formatValue (Fmt f, double v)
{
    switch (f)
    {
        case Fmt::Pct:
        case Fmt::Pct200:   return juce::String ((int) std::lround (v * 100.0)) + "%";
        case Fmt::Db:       return juce::String (v, 1) + " dB";
        case Fmt::Hz:       return v >= 1000.0 ? juce::String (v / 1000.0, 1) + " kHz" : juce::String ((int) std::lround (v)) + " Hz";
        case Fmt::Ms:       return juce::String (v, v < 10.0 ? 1 : 0) + " ms";
        case Fmt::Ratio:    return juce::String (v, 1) + ":1";
        case Fmt::Cents:    return juce::String (v, 1) + " ct";
        case Fmt::Division: return vfx::Echo::divisionName (juce::jlimit (0, vfx::Echo::numDivisions() - 1, (int) std::lround (v)));
    }
    return juce::String (v);
}

// ===================================================================== knob look
void KnobLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const juce::Colour c = s.findColour (juce::Slider::rotarySliderFillColourId);
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f - 2.f;
    const auto ctr = b.getCentre();
    const float ang = a0 + pos * (a1 - a0);
    const bool enabled = s.isEnabled();

    // printed scale
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (a1 - a0) * (float) i / 10.f;
        const float r0 = r - 1.f, r1 = r + (i % 5 == 0 ? 2.5f : 1.f);
        g.setColour (juce::Colours::white.withAlpha (i % 5 == 0 ? 0.35f : 0.18f));
        g.drawLine (ctr.x + r0 * std::sin (a), ctr.y - r0 * std::cos (a), ctr.x + r1 * std::sin (a), ctr.y - r1 * std::cos (a), 1.f);
    }

    // value ring
    const float rr = r - 5.f;
    juce::Path track, val;
    track.addCentredArc (ctr.x, ctr.y, rr, rr, 0.f, a0, a1, true);
    val.addCentredArc (ctr.x, ctr.y, rr, rr, 0.f, a0, ang, true);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.strokePath (track, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (enabled)
    {
        g.setColour (c.withAlpha (0.22f));
        g.strokePath (val, juce::PathStrokeType (7.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (enabled ? c : c.withSaturation (0.1f).withAlpha (0.5f));
    g.strokePath (val, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // body: drop shadow, knurled skirt, brushed cap
    const float kr = rr - 6.f;
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillEllipse (ctr.x - kr - (float) i * 0.5f, ctr.y - kr + (float) i * 0.9f, 2.f * kr + (float) i, 2.f * kr + (float) i);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3f49), ctr.x, ctr.y - kr, juce::Colour (0xff121419), ctr.x, ctr.y + kr, false));
    g.fillEllipse (ctr.x - kr, ctr.y - kr, 2.f * kr, 2.f * kr);
    const int grips = 40;
    for (int i = 0; i < grips; ++i)
    {
        const float a = ang + 2.f * kPi * (float) i / (float) grips;
        const float g0 = kr - 2.6f, g1 = kr - 0.4f;
        g.setColour (i % 2 == 0 ? juce::Colours::white.withAlpha (0.10f) : juce::Colours::black.withAlpha (0.35f));
        g.drawLine (ctr.x + g0 * std::sin (a), ctr.y - g0 * std::cos (a), ctr.x + g1 * std::sin (a), ctr.y - g1 * std::cos (a), 1.f);
    }
    const float cr = kr * 0.74f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe6e9ee), ctr.x - cr * 0.6f, ctr.y - cr * 0.8f,
                                             juce::Colour (0xff6d7480), ctr.x + cr * 0.7f, ctr.y + cr * 0.9f, true));
    g.fillEllipse (ctr.x - cr, ctr.y - cr, 2.f * cr, 2.f * cr);
    for (int i = 1; i <= 5; ++i)                                   // brushed rings
    {
        const float q = cr * (float) i / 5.5f;
        g.setColour (juce::Colours::white.withAlpha (i % 2 ? 0.07f : 0.0f));
        g.drawEllipse (ctr.x - q, ctr.y - q, 2.f * q, 2.f * q, 0.6f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.55f));          // top rim highlight
    juce::Path rim;
    rim.addCentredArc (ctr.x, ctr.y, cr - 0.5f, cr - 0.5f, 0.f, -2.2f, -0.4f, true);
    g.strokePath (rim, juce::PathStrokeType (1.f));
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (ctr.x - cr, ctr.y - cr, 2.f * cr, 2.f * cr, 0.8f);

    // pointer
    const float p0 = cr * 0.25f, p1 = cr * 0.92f;
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawLine (ctr.x + p0 * std::sin (ang), ctr.y - p0 * std::cos (ang) + 0.8f, ctr.x + p1 * std::sin (ang), ctr.y - p1 * std::cos (ang) + 0.8f, 3.2f);
    g.setColour (enabled ? c.brighter (0.2f) : juce::Colours::grey);
    g.drawLine (ctr.x + p0 * std::sin (ang), ctr.y - p0 * std::cos (ang), ctr.x + p1 * std::sin (ang), ctr.y - p1 * std::cos (ang), 2.2f);
}

juce::Label* KnobLook::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::FontOptions (11.5f));
    l->setColour (juce::Label::textColourId, Theme::text.withAlpha (0.9f));
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::TextEditor::backgroundColourId, Theme::panel2);
    l->setColour (juce::TextEditor::textColourId, Theme::text);
    l->setColour (juce::TextEditor::highlightColourId, Theme::accent.withAlpha (0.4f));
    return l;
}

void KnobLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hl, bool)
{
    const juce::Colour c = b.findColour (juce::ToggleButton::tickColourId);
    const bool on = b.getToggleState();
    auto r = b.getLocalBounds().toFloat();

    if (b.getProperties()["power"])                                // illuminated power button
    {
        const float d = std::min (r.getWidth(), r.getHeight()) - 4.f;
        auto k = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
        if (on)
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (c.withAlpha (0.10f * (float) i));
                g.fillEllipse (k.expanded ((float) (4 - i) * 2.f));
            }
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c313a), k.getX(), k.getY(), juce::Colour (0xff111318), k.getRight(), k.getBottom(), false));
        g.fillEllipse (k);
        g.setColour (on ? c : juce::Colour (0xff59606c).withAlpha (hl ? 1.f : 0.8f));
        const auto cc = k.getCentre();
        const float pr = d * 0.27f;
        juce::Path arc;
        arc.addCentredArc (cc.x, cc.y, pr, pr, 0.f, 0.6f, 2.f * kPi - 0.6f, true);
        g.strokePath (arc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawLine (cc.x, cc.y - pr - 1.5f, cc.x, cc.y - pr * 0.15f, 1.8f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (k, 1.f);
        return;
    }

    // slide switch + label
    auto sw = r.removeFromLeft (30.f).withSizeKeepingCentre (28.f, 15.f);
    g.setColour (on ? c.withAlpha (0.85f) : juce::Colour (0xff2a2f38));
    g.fillRoundedRectangle (sw, 7.5f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (sw, 7.5f, 1.f);
    const float kx = on ? sw.getRight() - 13.f : sw.getX() + 2.f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff1f3f6), kx, sw.getY(), juce::Colour (0xffa8afba), kx, sw.getBottom(), false));
    g.fillEllipse (kx, sw.getY() + 2.f, 11.f, 11.f);
    g.setColour (Theme::text.withAlpha (hl ? 1.f : 0.85f));
    g.setFont (juce::FontOptions (12.f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (6.f), juce::Justification::centredLeft);
}

// ===================================================================== module
class dash::Module : public juce::Component
{
public:
    Module (RetuneProcessor& p, NoteEditor& e, int index, KnobLook& look) : proc (p), editor (e), def (modules()[(size_t) index])
    {
        const juce::Colour c = col (def.colour);
        if (def.onId != nullptr)
        {
            power.getProperties().set ("power", true);
            power.setColour (juce::ToggleButton::tickColourId, c);
            power.setTooltip ("On / off");
            addAndMakeVisible (power);
            powerAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, def.onId, power);
        }
        for (const auto& k : def.knobs)
        {
            auto* s = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow));
            s->setLookAndFeel (&look);
            s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 16);
            s->setRotaryParameters (kPi * 1.25f, kPi * 2.75f, true);
            s->setColour (juce::Slider::rotarySliderFillColourId, c);
            s->setColour (juce::Slider::textBoxTextColourId, Theme::text);
            s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            s->setMouseDragSensitivity (220);
            addAndMakeVisible (s);
            auto* l = labels.add (new juce::Label ({}, juce::String (k.label).toUpperCase()));
            l->setJustificationType (juce::Justification::centred);
            l->setFont (juce::FontOptions (10.5f, juce::Font::bold));
            l->setColour (juce::Label::textColourId, Theme::dim);
            l->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (l);
            fields.push_back (k.id == nullptr ? k.field : -1);

            if (k.id != nullptr)
            {
                atts.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, k.id, *s));
                s->onDragStart = [this] { powerOn(); };
                if (auto* prm = proc.apvts.getParameter (k.id))
                    s->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
            }
            else
            {
                const int field = k.field;
                s->setRange (k.lo, k.hi, 0.0);
                s->setDoubleClickReturnValue (true, k.def);
                s->onDragStart = [this] { powerOn(); dragging = true; editor.beginShapeEdit(); };
                s->onDragEnd   = [this] { dragging = false; editor.endShapeEdit(); };
                s->onValueChange = [this, s, field]
                {
                    if (dragging) { editor.setShape (field, (float) s->getValue()); return; }
                    powerOn();
                    editor.beginShapeEdit();               // typed value or double-click reset
                    editor.setShape (field, (float) s->getValue());
                    editor.endShapeEdit();
                };
            }
            const Fmt f = k.fmt;
            s->textFromValueFunction = [f] (double v) { return formatValue (f, v); };
            s->valueFromTextFunction = [f] (const juce::String& t) { return parseValue (f, t); };
            s->updateText();
        }
        if (def.toggleId != nullptr)
        {
            extra = std::make_unique<juce::ToggleButton> (def.toggleLabel);
            extra->setLookAndFeel (&look);
            extra->setColour (juce::ToggleButton::tickColourId, c);
            addAndMakeVisible (*extra);
            extraAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, def.toggleId, *extra);
        }
        power.setLookAndFeel (&look);
        for (int i = 0; i < knobs.size(); ++i)                    // per-note knobs start at the notes' current values
            if (fields[(size_t) i] >= 0)
                knobs[i]->setValue (editor.shapeValue (fields[(size_t) i], noteTargets), juce::dontSendNotification);
    }

    ~Module() override
    {
        powerAtt.reset(); extraAtt.reset(); atts.clear();
        for (auto* s : knobs) s->setLookAndFeel (nullptr);
        power.setLookAndFeel (nullptr);
        if (extra) extra->setLookAndFeel (nullptr);
    }

    void flash() { glow = 1.f; }
    void setMaxCell (int w) { maxCell = w; resized(); }
    int numKnobs() const { return knobs.size(); }
    juce::String knobText (int i) const { return knobs[i]->getTextFromValue (knobs[i]->getValue()); }
    bool extraOn() const { return extra != nullptr && extra->getToggleState(); }

    void tick()
    {
        auto fall = [] (float& m, float v, float rate) { m = v > m ? v : std::max (v, m - rate); };
        fall (mDeess, proc.deessReduction(), 0.6f);
        fall (mComp, proc.compReduction(), 0.5f);
        fall (mCompIn, std::max (-60.f, toDb (proc.compInLevel())), 1.2f);
        fall (mCompOut, std::max (-60.f, toDb (proc.compOutLevel())), 1.2f);
        for (int s = 0; s < 3; ++s) fall (mChoir[s], std::max (-60.f, toDb (proc.choirLevel (s))), 1.0f);
        for (int c = 0; c < 2; ++c)
        {
            fall (mIn[c], std::max (-60.f, toDb (proc.inLevel[c].load())), 1.2f);
            fall (mOut[c], std::max (-60.f, toDb (proc.outLevel[c].load())), 1.2f);
            if (mOut[c] > peakHold[c]) { peakHold[c] = mOut[c]; holdTicks[c] = 45; }
            else if (--holdTicks[c] < 0) peakHold[c] = std::max (-60.f, peakHold[c] - 0.8f);
        }
        needle += (std::min (20.f, mComp) - needle) * 0.25f;     // VU ballistics
        if (! dragging)
        {
            int targets = 0;
            for (int i = 0; i < knobs.size(); ++i)
                if (fields[(size_t) i] >= 0)
                {
                    const float v = editor.shapeValue (fields[(size_t) i], targets);
                    if (targets > 0) knobs[i]->setValue (v, juce::dontSendNotification);   // no notes yet: keep the knob where it is
                }
            noteTargets = targets;
        }
        glow = std::max (0.f, glow - 0.03f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const juce::Colour c = col (def.colour);
        auto r = getLocalBounds().toFloat().reduced (1.f);
        const bool on = isOn();

        // faceplate
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r.translated (0.f, 3.f), 12.f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff20242d).interpolatedWith (c, 0.06f), 0.f, r.getY(),
                                                 juce::Colour (0xff14171d), 0.f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 12.f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 12.f, r.getRight() - 12.f);
        g.setColour (glow > 0.f ? c.withAlpha (0.3f + 0.6f * glow) : c.withAlpha (on ? 0.32f : 0.12f));
        g.drawRoundedRectangle (r, 12.f, glow > 0.f ? 2.f : 1.f);
        g.setColour (c.withAlpha (on ? 0.9f : 0.25f));            // colour stripe
        g.fillRoundedRectangle (r.getX() + 14.f, r.getY() + 11.f, 3.f, 18.f, 1.5f);
        screw (g, r.getX() + 9.f, r.getBottom() - 9.f);
        screw (g, r.getRight() - 9.f, r.getBottom() - 9.f);

        g.setColour (Theme::text.withAlpha (on ? 1.f : 0.6f));
        g.setFont (juce::FontOptions (14.f, juce::Font::bold));
        g.drawText (def.name, (int) r.getX() + 24, (int) r.getY() + 8, 240, 18, juce::Justification::centredLeft);
        g.setColour (Theme::dim.withAlpha (0.85f));
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (def.subtitle, (int) r.getX() + 24, (int) r.getY() + 25, 260, 12, juce::Justification::centredLeft);

        // display glass
        auto sc = screen.toFloat();
        g.setColour (juce::Colour (0xff07090c));
        g.fillRoundedRectangle (sc, 7.f);
        g.setGradientFill (juce::ColourGradient (c.withAlpha (on ? 0.10f : 0.03f), sc.getCentreX(), sc.getBottom(),
                                                 juce::Colours::transparentBlack, sc.getCentreX(), sc.getY(), false));
        g.fillRoundedRectangle (sc, 7.f);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (screen.reduced (1));
            paintVis (g, sc.reduced (8.f, 6.f), c, on);
        }
        g.setColour (juce::Colours::white.withAlpha (0.05f));      // glass reflection + bezel
        g.fillRoundedRectangle (sc.withHeight (sc.getHeight() * 0.38f).reduced (2.f, 2.f), 6.f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (sc, 7.f, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (sc.expanded (1.f), 8.f, 1.f);
        if (! on && def.onId != nullptr)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (sc, 7.f);
            g.setColour (Theme::dim.withAlpha (0.7f));
            g.setFont (juce::FontOptions (10.f, juce::Font::bold));
            g.drawText ("BYPASSED", screen, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10);
        auto head = r.removeFromTop (32);
        power.setBounds (head.removeFromRight (30).withSizeKeepingCentre (28, 28));
        if (extra != nullptr) extra->setBounds (head.removeFromRight (130).withSizeKeepingCentre (130, 22));
        r.removeFromTop (6);
        const int knobRows = def.knobs.size() > 5 ? 2 : (def.knobs.empty() ? 0 : 1);
        const int rowH = knobRows == 0 ? 0 : juce::jlimit (84, maxCell + 20, (r.getHeight() - 150) / knobRows);
        screen = r.removeFromTop (r.getHeight() - knobRows * rowH - (knobRows > 0 ? 8 : 0));
        r.removeFromTop (knobRows > 0 ? 8 : 0);
        const int n = knobs.size();
        const int perRow = knobRows == 2 ? (n + 1) / 2 : n;
        for (int i = 0; i < n; ++i)
        {
            const int row = i / juce::jmax (1, perRow), colI = i % juce::jmax (1, perRow);
            const int inRow = row == 0 ? juce::jmin (perRow, n) : n - perRow;
            const int cw = juce::jmin (maxCell, r.getWidth() / juce::jmax (1, perRow));
            const int x0 = r.getX() + (r.getWidth() - inRow * cw) / 2;
            juce::Rectangle<int> cell (x0 + colI * cw, r.getY() + row * rowH, cw, rowH);
            labels[i]->setBounds (cell.removeFromTop (13));
            knobs[i]->setBounds (cell);
        }
    }

private:
    bool isOn() const
    {
        if (def.onId == nullptr) return true;
        return proc.apvts.getRawParameterValue (def.onId)->load() > 0.5f;
    }
    void powerOn()                     // touching a bypassed effect's knob switches the effect on
    {
        if (def.onId != nullptr)
            if (auto* prm = proc.apvts.getParameter (def.onId))
                if (prm->getValue() < 0.5f) prm->setValueNotifyingHost (1.f);
    }
    float pv (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    void gridLines (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour c)
    {
        g.setColour (c.withAlpha (0.07f));
        for (int i = 1; i < 6; ++i) g.drawHorizontalLine ((int) (a.getY() + a.getHeight() * (float) i / 6.f), a.getX(), a.getRight());
        for (int i = 1; i < 10; ++i) g.drawVerticalLine ((int) (a.getX() + a.getWidth() * (float) i / 10.f), a.getY(), a.getBottom());
    }

    void glowPath (juce::Graphics& g, const juce::Path& p, juce::Colour c, float w = 2.f)
    {
        g.setColour (c.withAlpha (0.18f));
        g.strokePath (p, juce::PathStrokeType (w * 3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void lcdText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Colour c, float size, juce::Justification j)
    {
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::bold));
        g.setColour (c.withAlpha (0.25f));
        g.drawText (t, r.translated (0.f, 0.5f).expanded (1.f), j);
        g.setColour (c);
        g.drawText (t, r, j);
    }

    void paintVis (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour c, bool on)
    {
        const juce::Colour ink = on ? c : c.withSaturation (0.15f).withAlpha (0.6f);
        switch (def.vis)
        {
            case Vis::Glide:
            {
                gridLines (g, a, c);
                const float ms = knobs[0]->getValue() > 0.0 ? (float) knobs[0]->getValue() : 0.f;
                const float sm = (float) knobs[1]->getValue();
                const float yLo = a.getY() + a.getHeight() * 0.78f, yHi = a.getY() + a.getHeight() * 0.25f;
                const float xMid = a.getX() + a.getWidth() * 0.32f;
                const float glideW = a.getWidth() * 0.55f * ms / 500.f;
                juce::Path p;
                p.startNewSubPath (a.getX(), yLo);
                p.lineTo (xMid, yLo);
                for (int i = 1; i <= 40; ++i)
                {
                    const float u = (float) i / 40.f;
                    const float sh = (1.f - sm) * u + sm * u * u * (3.f - 2.f * u);
                    p.lineTo (xMid + glideW * u, yLo + (yHi - yLo) * sh);
                }
                p.lineTo (a.getRight(), yHi);
                g.setColour (ink.withAlpha (0.25f));
                g.fillRoundedRectangle (a.getX(), yLo - 6.f, xMid - a.getX(), 12.f, 6.f);
                g.fillRoundedRectangle (xMid, yHi - 6.f, a.getRight() - xMid, 12.f, 6.f);
                glowPath (g, p, ink);
                lcdText (g, ms < 0.5f ? "JUMP" : juce::String ((int) std::lround (ms)) + " ms", a.removeFromTop (16.f), ink, 13.f, juce::Justification::topRight);
                paintTargets (g, a, ink);
                break;
            }
            case Vis::Drift:
            {
                gridLines (g, a, c);
                const float dr = (float) knobs[0]->getValue(), vb = (float) knobs[1]->getValue();
                const float mid = a.getCentreY(), amp = a.getHeight() * 0.16f;
                juce::Path orig, res;
                for (int i = 0; i <= 160; ++i)
                {
                    const float u = (float) i / 160.f, x = a.getX() + a.getWidth() * u;
                    const float slow = std::sin (u * 2.f * kPi * 0.9f + 0.6f) * 1.1f, fast = std::sin (u * 2.f * kPi * 7.f) * 0.8f;
                    const float y0 = mid - amp * (slow + fast), y1 = mid - amp * (slow * dr + fast * vb);
                    if (i == 0) { orig.startNewSubPath (x, y0); res.startNewSubPath (x, y1); }
                    else { orig.lineTo (x, y0); res.lineTo (x, y1); }
                }
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.strokePath (orig, juce::PathStrokeType (1.f));
                glowPath (g, res, ink);
                lcdText (g, "DRIFT " + formatValue (Fmt::Pct, dr) + "   VIB " + formatValue (Fmt::Pct, vb), a.removeFromTop (16.f), ink, 11.f, juce::Justification::topRight);
                paintTargets (g, a, ink);
                break;
            }
            case Vis::DeEss:
            {
                auto meter = a.removeFromRight (16.f);
                a.removeFromRight (10.f);
                gridLines (g, a, c);
                const float f = pv ("deessFreq"), depth = juce::jlimit (0.f, 1.f, mDeess / 24.f);
                const float fx = logX (f, a), y0 = a.getY() + a.getHeight() * 0.35f;
                juce::Path p;
                for (int i = 0; i <= 120; ++i)
                {
                    const float x = a.getX() + a.getWidth() * (float) i / 120.f;
                    const float t = juce::jlimit (0.f, 1.f, (x - fx) / 18.f + 0.5f);
                    const float y = y0 + (a.getHeight() * 0.55f) * depth * (t * t * (3.f - 2.f * t));
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.setColour (ink.withAlpha (0.25f));
                g.drawVerticalLine ((int) fx, a.getY(), a.getBottom());
                glowPath (g, p, ink);
                lcdText (g, formatValue (Fmt::Hz, f), juce::Rectangle<float> (fx + 4.f, a.getBottom() - 16.f, 70.f, 14.f), ink, 11.f, juce::Justification::left);
                lcdText (g, "GR " + juce::String (mDeess, 1) + " dB", a.removeFromTop (16.f), ink, 12.f, juce::Justification::topLeft);
                ledBar (g, meter, depth, juce::Colour (0xffffb547), true);
                break;
            }
            case Vis::Air:
            {
                gridLines (g, a, c);
                const float m = pv ("midAir"), hgh = pv ("highAir");
                juce::Path p;
                for (int i = 0; i <= 120; ++i)
                {
                    const float x = a.getX() + a.getWidth() * (float) i / 120.f;
                    const float hz = 100.f * std::pow (200.f, (float) i / 120.f);
                    const float bump = m * 8.f * std::exp (-std::pow (std::log (hz / 4500.f), 2.f) / 0.5f);
                    const float shelf = hgh * 11.f / (1.f + std::pow (6000.f / hz, 2.f));
                    const float y = a.getY() + a.getHeight() * 0.72f - (bump + shelf) / 20.f * a.getHeight() * 0.7f;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                juce::Path fill (p);
                fill.lineTo (a.getRight(), a.getBottom()); fill.lineTo (a.getX(), a.getBottom()); fill.closeSubPath();
                g.setGradientFill (juce::ColourGradient (ink.withAlpha (0.35f), 0.f, a.getY(), ink.withAlpha (0.0f), 0.f, a.getBottom(), false));
                g.fillPath (fill);
                glowPath (g, p, ink);
                g.setColour (Theme::dim.withAlpha (0.6f));
                g.setFont (juce::FontOptions (9.f));
                for (float hz : { 1000.f, 5000.f, 10000.f })
                    g.drawText (hz >= 1000.f ? juce::String ((int) (hz / 1000.f)) + "k" : juce::String ((int) hz), juce::Rectangle<float> (logX (hz, a) - 10.f, a.getBottom() - 11.f, 20.f, 10.f), juce::Justification::centred);
                break;
            }
            case Vis::Comp:
            {
                auto vu = a.removeFromLeft (a.getWidth() * 0.62f).reduced (2.f, 0.f);
                a.removeFromLeft (14.f);
                paintVU (g, vu, on);
                auto inM = a.removeFromLeft (a.getWidth() * 0.5f).reduced (8.f, 14.f);
                auto outM = a.reduced (8.f, 14.f);
                ledBar (g, inM.withTrimmedBottom (2.f), juce::jlimit (0.f, 1.f, (mCompIn + 60.f) / 60.f), juce::Colours::green, true, true);
                ledBar (g, outM.withTrimmedBottom (2.f), juce::jlimit (0.f, 1.f, (mCompOut + 60.f) / 60.f), juce::Colours::green, true, true);
                g.setColour (Theme::dim);
                g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                g.drawText ("IN", inM.withY (inM.getBottom() + 1.f).withHeight (12.f), juce::Justification::centred);
                g.drawText ("OUT", outM.withY (outM.getBottom() + 1.f).withHeight (12.f), juce::Justification::centred);
                break;
            }
            case Vis::Choir:   paintChoir (g, a, ink, on); break;
            case Vis::Doubler:
            {
                const float w = pv ("doubleWidth"), det = pv ("doubleDetune"), mx = pv ("doubleMix");
                const float cy = a.getCentreY() + 4.f, cx = a.getCentreX(), half = a.getWidth() * 0.42f;
                g.setColour (ink.withAlpha (0.12f));
                g.drawHorizontalLine ((int) cy, a.getX(), a.getRight());
                g.setColour (Theme::dim.withAlpha (0.6f));
                g.setFont (juce::FontOptions (10.f, juce::Font::bold));
                g.drawText ("L", a.withWidth (14.f), juce::Justification::centredLeft);
                g.drawText ("R", a.withTrimmedLeft (a.getWidth() - 14.f), juce::Justification::centredRight);
                auto dot = [&] (float x, float rad, float alpha, bool lead)
                {
                    for (int i = 3; i >= 1; --i)
                    {
                        g.setColour ((lead ? juce::Colours::white : ink).withAlpha (alpha * 0.1f * (float) i));
                        g.fillEllipse (x - rad - (float) i * 3.f, cy - rad - (float) i * 3.f, 2.f * (rad + (float) i * 3.f), 2.f * (rad + (float) i * 3.f));
                    }
                    g.setColour ((lead ? juce::Colours::white : ink).withAlpha (alpha));
                    g.fillEllipse (x - rad, cy - rad, 2.f * rad, 2.f * rad);
                };
                const float rad = 5.f + det * 0.25f;
                dot (cx - half * w, rad, 0.35f + 0.6f * mx, false);
                dot (cx + half * w, rad, 0.35f + 0.6f * mx, false);
                dot (cx, 6.f, 0.95f, true);
                lcdText (g, juce::String ((int) std::lround (w * 100.f)) + "% WIDE", a.removeFromTop (16.f), ink, 11.f, juce::Justification::topRight);
                break;
            }
            case Vis::Echo:
            {
                const int div = (int) std::lround (pv ("echoTime"));
                static const float beats[] = { 0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.f };
                const double bpm = proc.hostBpm.load();
                const float ms = (float) (60000.0 / (bpm > 20.0 ? bpm : 120.0) * beats[juce::jlimit (0, 5, div)]);
                const float fb = pv ("echoFeedback"), mx = pv ("echoMix");
                auto top = a.removeFromTop (20.f);
                lcdText (g, formatValue (Fmt::Division, div), top, ink, 15.f, juce::Justification::left);
                lcdText (g, juce::String ((int) std::lround (ms)) + " ms", top, ink, 12.f, juce::Justification::right);
                a.removeFromTop (6.f);
                const float span = 2000.f;
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.fillRect (a.getX(), a.getY(), 3.f, a.getHeight());
                float amp = mx * 1.4f;
                for (int k = 1; k < 24; ++k)
                {
                    const float x = a.getX() + a.getWidth() * (ms * (float) k) / span;
                    if (x > a.getRight() || amp < 0.02f) break;
                    const float hgt = a.getHeight() * juce::jmin (1.f, amp);
                    g.setColour (ink.withAlpha (0.2f));
                    g.fillRect (x - 2.f, a.getBottom() - hgt - 1.f, 7.f, hgt + 2.f);
                    g.setColour (ink);
                    g.fillRect (x, a.getBottom() - hgt, 3.f, hgt);
                    amp *= fb;
                }
                break;
            }
            case Vis::Reverb:
            {
                gridLines (g, a, c);
                const float sz = pv ("verbSize"), dmp = pv ("verbDamp"), wd = pv ("verbWidth"), mx = pv ("verbMix");
                const float decay = 0.4f + 3.2f * sz;
                juce::Path p;
                p.startNewSubPath (a.getX(), a.getBottom());
                for (int i = 0; i <= 100; ++i)
                {
                    const float t = 4.f * (float) i / 100.f;
                    const float env = (0.25f + 0.75f * mx) * std::exp (-t * 3.f / decay) * (1.f - 0.15f * std::sin ((float) i * 1.7f) * (1.f - dmp));
                    p.lineTo (a.getX() + a.getWidth() * (float) i / 100.f, a.getBottom() - a.getHeight() * 0.85f * env);
                }
                p.lineTo (a.getRight(), a.getBottom());
                p.closeSubPath();
                const juce::Colour tail = ink.interpolatedWith (juce::Colour (0xff2b3870), dmp * 0.7f);
                g.setGradientFill (juce::ColourGradient (ink.withAlpha (0.65f), a.getX(), 0.f, tail.withAlpha (0.15f), a.getRight(), 0.f, false));
                g.fillPath (p);
                lcdText (g, juce::String (decay, 1) + " s   " + juce::String ((int) std::lround (wd * 100.f)) + "% W", a.removeFromTop (16.f), ink, 11.f, juce::Justification::topRight);
                break;
            }
            case Vis::Output:
            {
                const char* names[4] = { "IN L", "IN R", "OUT L", "OUT R" };
                const float vals[4] = { mIn[0], mIn[1], mOut[0], mOut[1] };
                const float rowH = a.getHeight() / 4.f;
                for (int i = 0; i < 4; ++i)
                {
                    auto row = a.withY (a.getY() + rowH * (float) i).withHeight (rowH).reduced (0.f, 4.f);
                    g.setColour (Theme::dim);
                    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                    g.drawText (names[i], row.removeFromLeft (38.f), juce::Justification::centredLeft);
                    ledBar (g, row, juce::jlimit (0.f, 1.f, (vals[i] + 60.f) / 60.f), juce::Colours::green, false, true);
                    if (i >= 2)
                    {
                        const float px = row.getX() + row.getWidth() * juce::jlimit (0.f, 1.f, (peakHold[i - 2] + 60.f) / 60.f);
                        g.setColour (juce::Colours::white.withAlpha (0.8f));
                        g.fillRect (px - 1.f, row.getY(), 2.f, row.getHeight());
                    }
                }
                break;
            }
        }
    }

    void paintTargets (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour ink)
    {
        bool any = false;
        for (const auto& n : proc.getNotes()) any = any || n.selected;
        const juce::String t = noteTargets == 0 ? juce::String ("NO NOTES YET")
                             : (any ? juce::String (noteTargets) + " SELECTED" : "ALL " + juce::String (noteTargets) + " NOTES");
        g.setColour (ink.withAlpha (0.75f));
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (t, a.removeFromBottom (12.f), juce::Justification::bottomLeft);
    }

    void paintVU (juce::Graphics& g, juce::Rectangle<float> r, bool on)
    {
        // classic cream VU face, reading gain reduction (0 at the right, -20 dB at the left)
        r = r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight() / 0.6f), std::min (r.getHeight(), r.getWidth() * 0.6f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (on ? 0xfff6ecd0 : 0xffb8b2a2), r.getCentreX(), r.getY(),
                                                 juce::Colour (on ? 0xffe2cf9c : 0xff8f8a7d), r.getCentreX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.f);
        if (on)
        {
            g.setColour (juce::Colour (0xffffd27a).withAlpha (0.25f));    // backlight
            g.fillEllipse (r.withSizeKeepingCentre (r.getWidth() * 0.8f, r.getHeight() * 0.9f).translated (0.f, r.getHeight() * 0.25f));
        }
        const juce::Point<float> pivot (r.getCentreX(), r.getBottom() + r.getHeight() * 0.35f);
        const float rad = r.getHeight() * 1.05f;
        const float aL = -0.78f, aR = 0.78f;
        auto angFor = [&] (float gr) { return aR - (aL * -1.f + aR) * juce::jlimit (0.f, 1.f, gr / 20.f); };
        juce::Path scale;
        scale.addCentredArc (pivot.x, pivot.y, rad, rad, 0.f, aL, aR, true);
        g.setColour (juce::Colour (0xff2a2622));
        g.strokePath (scale, juce::PathStrokeType (1.2f));
        juce::Path red;
        red.addCentredArc (pivot.x, pivot.y, rad, rad, 0.f, angFor (20.f), angFor (10.f), true);
        g.setColour (juce::Colour (0xffc2362b));
        g.strokePath (red, juce::PathStrokeType (3.f));
        g.setFont (juce::FontOptions (9.f, juce::Font::bold));
        for (float d : { 0.f, 2.f, 4.f, 6.f, 10.f, 15.f, 20.f })
        {
            const float an = angFor (d);
            const float x0 = pivot.x + rad * std::sin (an), y0 = pivot.y - rad * std::cos (an);
            const float x1 = pivot.x + (rad + 6.f) * std::sin (an), y1 = pivot.y - (rad + 6.f) * std::cos (an);
            g.setColour (d >= 10.f ? juce::Colour (0xffc2362b) : juce::Colour (0xff2a2622));
            g.drawLine (x0, y0, x1, y1, 1.2f);
            g.drawText (d == 0.f ? "0" : "-" + juce::String ((int) d),
                        juce::Rectangle<float> (pivot.x + (rad + 14.f) * std::sin (an) - 12.f, pivot.y - (rad + 14.f) * std::cos (an) - 6.f, 24.f, 12.f),
                        juce::Justification::centred);
        }
        g.setColour (juce::Colour (0xff2a2622).withAlpha (0.8f));
        g.setFont (juce::FontOptions (10.f, juce::Font::bold));
        g.drawText ("GAIN REDUCTION  dB", r.withTrimmedTop (r.getHeight() * 0.58f).withHeight (14.f), juce::Justification::centred);
        // needle
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (r.toNearestInt());
        const float an = angFor (needle);
        g.setColour (juce::Colours::black.withAlpha (0.2f));
        g.drawLine (pivot.x + 2.f, pivot.y, pivot.x + 2.f + (rad + 4.f) * std::sin (an), pivot.y - (rad + 4.f) * std::cos (an) + 2.f, 2.f);
        g.setColour (juce::Colour (0xff1b1712));
        g.drawLine (pivot.x, pivot.y, pivot.x + (rad + 4.f) * std::sin (an), pivot.y - (rad + 4.f) * std::cos (an), 1.6f);
        g.setColour (juce::Colours::black.withAlpha (0.15f));
        g.fillRect (r.withTop (r.getBottom() - 10.f));
    }

    void paintChoir (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour ink, bool on)
    {
        const auto spec = proc.choirSpec();
        const int total = spec.singers[0] + spec.singers[1] + spec.singers[2];
        auto info = a.removeFromRight (120.f);
        lcdText (g, juce::String (total) + " SINGERS", info.removeFromTop (18.f), ink, 13.f, juce::Justification::topRight);
        g.setColour (Theme::dim);
        g.setFont (juce::FontOptions (10.f));
        g.drawText (spec.harmony ? "altos in harmony" : "altos on melody", info.removeFromTop (14.f), juce::Justification::topRight);
        if (proc.status.load() == RetuneProcessor::Rendering)
            lcdText (g, "RENDERING", info.removeFromTop (16.f), ink.withAlpha (0.8f), 10.f, juce::Justification::topRight);

        // the stage: three risers, back to front: basses, altos, sopranos
        static const char* names[3] = { "BASS", "ALTO", "SOPRANO" };
        static const juce::uint32 tint[3] = { 0xff8f7cff, 0xffe8c26a, 0xffff8fb1 };
        const float rowH = a.getHeight() / 3.f;
        for (int s = 0; s < 3; ++s)
        {
            auto row = a.withY (a.getY() + rowH * (float) s).withHeight (rowH);
            auto lab = row.removeFromLeft (64.f);
            auto meter = row.removeFromRight (70.f).reduced (6.f, rowH * 0.32f);
            const juce::Colour tc = on ? juce::Colour (tint[s]) : juce::Colour (tint[s]).withSaturation (0.15f).withAlpha (0.5f);
            g.setColour (tc);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText (juce::String (names[s]) + "  " + juce::String (spec.singers[s]), lab, juce::Justification::centredLeft);
            const float lvl = juce::jlimit (0.f, 1.f, (mChoir[s] + 50.f) / 50.f);
            const int n = spec.singers[s];
            const float slot = juce::jmin (22.f, row.getWidth() / (float) juce::jmax (1, n));
            const float x0 = row.getX() + (row.getWidth() - slot * (float) n) * 0.5f;
            for (int k = 0; k < n; ++k)                             // singers: head + shoulders, lit by their section's level
            {
                const float cx = x0 + slot * ((float) k + 0.5f), base = row.getBottom() - 2.f;
                const float hr = juce::jmin (5.f, slot * 0.24f);
                const float bounce = std::sin ((float) k * 1.9f + (float) s) * lvl * 1.5f;
                const juce::Colour bc = tc.withAlpha (0.35f + 0.65f * lvl);
                juce::Path body;
                body.addRoundedRectangle (cx - hr * 1.7f, base - hr * 2.1f, hr * 3.4f, hr * 2.1f, hr, hr, true, true, false, false);
                g.setColour (bc.withMultipliedBrightness (0.7f));
                g.fillPath (body);
                g.setColour (bc);
                g.fillEllipse (cx - hr, base - hr * 2.1f - hr * 2.1f - bounce, hr * 2.f, hr * 2.f);
            }
            ledBar (g, meter, lvl, tc, false);
        }
    }

    RetuneProcessor& proc;
    NoteEditor& editor;
    const ModuleDef& def;
    juce::ToggleButton power;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt;
    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> atts;
    std::unique_ptr<juce::ToggleButton> extra;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> extraAtt;
    std::vector<int> fields;
    bool dragging = false;
    int noteTargets = 0, maxCell = 92;
    juce::Rectangle<int> screen;
    float mDeess = 0.f, mComp = 0.f, mCompIn = -60.f, mCompOut = -60.f, needle = 0.f, glow = 0.f;
    float mChoir[3] = { -60.f, -60.f, -60.f }, mIn[2] = { -60.f, -60.f }, mOut[2] = { -60.f, -60.f };
    float peakHold[2] = { -60.f, -60.f };
    int holdTicks[2] = { 0, 0 };
};

// ===================================================================== descriptions
namespace
{
    const char* moduleDesc (int i)
    {
        static const char* d[] = {
            "Slides the pitch from the previous note into each note instead of jumping. Works on the selected notes, or on every note when none are selected.",
            "Reshapes the pitch inside each note: the slow wander (drift) and the fast wobble (vibrato). Works on the selected notes, or on every note when none are selected.",
            "Turns down harsh S, T and SH sounds only while they happen, leaving the rest of the voice untouched.",
            "Adds presence and breathy top end so the vocal sits on top of the mix.",
            "Evens out loud and quiet syllables like a smooth opto compressor, with automatic make-up gain.",
            "Turns the vocal into a choir: basses an octave down, altos a third below in the current key, sopranos on the melody, all placed in a hall.",
            "Adds two slightly detuned, delayed copies left and right for a wide, double-tracked sound.",
            "Tempo-synced echoes that get darker with every repeat, like a tape delay.",
            "Places the vocal in a room or plate space.",
            "Input and output levels with peak hold. Use it to check the effects are not clipping.",
        };
        return d[juce::jlimit (0, 9, i)];
    }

    const char* knobHelp (int m, int k)
    {
        static const std::vector<std::vector<const char*>> h {
            { "How long the slide into each note takes. 0 ms jumps straight to the note.",
              "Shape of the slide: low is a straight line, high eases in and out like an S-curve." },
            { "Slow pitch wander inside the note. 0% flat, 100% as sung, 200% exaggerated.",
              "Vibrato depth. 0% removes it, 100% as sung, 200% doubles it." },
            { "Level where de-essing starts. Lower catches more S sounds.",
              "Where the S sound lives. Most voices sit between 5 and 8 kHz.",
              "The most the S band is ever turned down." },
            { "Presence around 3-6 kHz. Brings the voice forward.",
              "Shelf above 10 kHz. Adds breath and sheen." },
            { "Level above which the vocal gets levelled. Lower means more compression.",
              "How hard loud parts are pulled down. 2:1 is gentle, 6:1 is firm.",
              "How fast it reacts to a loud syllable.",
              "How fast it lets go afterwards.",
              "Blend of compressed and dry voice. Below 100% is parallel compression." },
            { "Number of singers, from 3 to 16.",
              "Share of the singers in the bass section.",
              "Share of the singers in the alto section.",
              "Share of the singers in the soprano section.",
              "Choir level against the lead vocal.",
              "Volume of the bass section.",
              "Volume of the alto section.",
              "Volume of the soprano section.",
              "Size of the hall the choir sings in." },
            { "How far left and right the doubles sit.",
              "Pitch difference of the doubles, in cents. More sounds thicker.",
              "Level of the doubles." },
            { "Echo length, synced to the song tempo.",
              "How many repeats. High values ring out for a long time.",
              "Lower values make each repeat darker.",
              "Level of the echoes." },
            { "Room size and how long the tail lasts.",
              "Darkens the tail like soft walls.",
              "Stereo spread of the reverb.",
              "Level of the reverb." },
            {},
        };
        if (m < 0 || m >= (int) h.size() || k < 0 || k >= (int) h[(size_t) m].size()) return "";
        return h[(size_t) m][(size_t) k];
    }

    // current value of a module's knob, formatted, read straight from the parameters / notes
    juce::String knobValueText (RetuneProcessor& proc, NoteEditor& ed, const KnobDef& k)
    {
        if (k.id != nullptr)
        {
            if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (k.id)))
                return formatValue (k.fmt, prm->convertFrom0to1 (prm->getValue()));
            return {};
        }
        int t = 0;
        const float v = ed.shapeValue (k.field, t);
        return t > 0 ? formatValue (k.fmt, v) : juce::String ("-");
    }

    bool moduleOn (RetuneProcessor& proc, const ModuleDef& d)
    {
        return d.onId == nullptr || proc.apvts.getRawParameterValue (d.onId)->load() > 0.5f;
    }

    juce::String effectTitle (int i)
    {
        const juce::String n = modules()[(size_t) i].name;
        return n.substring (0, 1) + n.substring (1).toLowerCase();
    }

    const int kChain[] = { 2, 4, 3, 5, 6, 7, 8 };                 // processing order of the vocal effects
}

// ===================================================================== overview card
class dash::Card : public juce::Component
{
public:
    Card (RetuneProcessor& p, NoteEditor& e, int i, KnobLook& look) : proc (p), editor (e), index (i), def (modules()[(size_t) i])
    {
        if (def.onId != nullptr)
        {
            power.getProperties().set ("power", true);
            power.setColour (juce::ToggleButton::tickColourId, col (def.colour));
            power.setLookAndFeel (&look);
            power.setTooltip ("On / off");
            addAndMakeVisible (power);
            powerAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, def.onId, power);
        }
        openBtn.setButtonText ("Open  >");
        openBtn.onClick = [this] { if (onOpen) onOpen (index); };
        addAndMakeVisible (openBtn);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    ~Card() override { powerAtt.reset(); power.setLookAndFeel (nullptr); }

    std::function<void (int)> onOpen;

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override  { if (e.mouseWasClicked() && onOpen) onOpen (index); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 12);
        power.setBounds (r.getX() + 6, r.getY(), 28, 28);
        openBtn.setBounds (r.getRight() - 78, r.getY() + 1, 78, 26);
    }

    void paint (juce::Graphics& g) override
    {
        const juce::Colour c = col (def.colour);
        const bool on = moduleOn (proc, def);
        auto r = getLocalBounds().toFloat().reduced (1.f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (r.translated (0.f, 2.f), 10.f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d212a).interpolatedWith (c, hover ? 0.08f : 0.04f), 0.f, r.getY(),
                                                 juce::Colour (0xff15181e), 0.f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 10.f);
        g.setColour (c.withAlpha (hover ? 0.6f : (on ? 0.3f : 0.12f)));
        g.drawRoundedRectangle (r, 10.f, 1.f);
        g.setColour (c.withAlpha (on ? 0.95f : 0.25f));
        g.fillRoundedRectangle (r.getX(), r.getY() + 14.f, 3.f, r.getHeight() - 28.f, 1.5f);

        auto in = getLocalBounds().reduced (14, 12);
        const int textX = in.getX() + (def.onId != nullptr ? 44 : 8);
        const int textW = in.getRight() - 90 - textX;
        g.setColour (Theme::text.withAlpha (on ? 1.f : 0.65f));
        g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
        g.drawText (def.name, textX, in.getY(), textW, 16, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.setColour (on ? c : Theme::dim.withAlpha (0.7f));
        g.drawText (def.onId == nullptr ? juce::String ("METERS") : (on ? juce::String ("ON") : juce::String ("BYPASSED")) + "   " + live(),
                    textX, in.getY() + 16, textW, 13, juce::Justification::centredLeft);

        g.setColour (Theme::dim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (moduleDesc (index), in.getX() + 8, in.getY() + 36, in.getWidth() - 16, 30, juce::Justification::topLeft, 2, 1.f);

        // settings breakdown chips
        float x = (float) in.getX() + 8.f;
        const float y = (float) in.getBottom() - 22.f;
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        int shown = 0;
        for (const auto& k : def.knobs)
        {
            const juce::String lab = juce::String (k.label).toUpperCase(), val = knobValueText (proc, editor, k);
            const float lw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), lab), vw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), val);
            const float w = lw + vw + 24.f;
            if (x + w > (float) in.getRight() - 30.f) break;
            g.setColour (juce::Colour (0xff0b0d11));
            g.fillRoundedRectangle (x, y, w, 20.f, 5.f);
            g.setColour (Theme::dim);
            g.drawText (lab, juce::Rectangle<float> (x + 8.f, y, lw + 2.f, 20.f), juce::Justification::centredLeft);
            g.setColour (on ? Theme::text : Theme::dim);
            g.drawText (val, juce::Rectangle<float> (x + 14.f + lw, y, vw + 4.f, 20.f), juce::Justification::centredLeft);
            x += w + 6.f;
            ++shown;
        }
        if (shown < (int) def.knobs.size())
        {
            g.setColour (Theme::dim);
            g.drawText ("+" + juce::String ((int) def.knobs.size() - shown), juce::Rectangle<float> (x, y, 30.f, 20.f), juce::Justification::centredLeft);
        }
        if (def.vis == Vis::Output)
        {
            auto m = juce::Rectangle<float> ((float) in.getX() + 8.f, y + 2.f, (float) in.getWidth() - 16.f, 7.f);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float lv = std::max (-60.f, toDb (proc.outLevel[ch].load()));
                ledBar (g, m.translated (0.f, ch * 10.f), juce::jlimit (0.f, 1.f, (lv + 60.f) / 60.f), juce::Colours::green, false, true);
            }
        }
    }

private:
    juce::String live() const
    {
        switch (def.vis)
        {
            case Vis::DeEss:  return "GR " + juce::String (proc.deessReduction(), 1) + " dB";
            case Vis::Comp:   return "GR " + juce::String (proc.compReduction(), 1) + " dB";
            case Vis::Choir:
            {
                const auto s = proc.choirSpec();
                return juce::String (s.singers[0] + s.singers[1] + s.singers[2]) + " singers";
            }
            case Vis::Glide:
            case Vis::Drift:
            {
                int t = 0;
                editor.shapeValue (0, t);
                return t == 0 ? juce::String ("no notes yet") : juce::String (t) + " notes";
            }
            default: return {};
        }
    }

    RetuneProcessor& proc;
    NoteEditor& editor;
    const int index;
    const ModuleDef& def;
    juce::ToggleButton power;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt;
    juce::TextButton openBtn;
    bool hover = false;
};

// ===================================================================== overview page
class dash::Overview : public juce::Component
{
public:
    Overview (RetuneProcessor& p, NoteEditor& e, KnobLook& look) : proc (p)
    {
        for (int i = 0; i < (int) modules().size(); ++i)
        {
            auto* c = cards.add (new Card (p, e, i, look));
            c->onOpen = [this] (int k) { if (onOpen) onOpen (k); };
            addAndMakeVisible (c);
        }
    }

    std::function<void (int)> onOpen;

    int layout (int width)                          // lays the cards out, returns the height needed
    {
        const int gap = 12, pad = 16, cardH = 128;
        const int colW = (width - 2 * pad - gap) / 2;
        int y = pad;
        chainArea = { pad, y, width - 2 * pad, 58 };
        y += 58 + 18;
        head1 = y; y += 24;
        for (int i = 0; i < 2; ++i) cards[i]->setBounds (pad + i * (colW + gap), y, colW, cardH);
        y += cardH + 18;
        head2 = y; y += 24;
        const int order[] = { 2, 4, 3, 5, 6, 7, 8, 9 };
        for (int k = 0; k < 8; ++k)
            cards[order[k]]->setBounds (pad + (k % 2) * (colW + gap), y + (k / 2) * (cardH + gap), colW, cardH);
        y += 4 * (cardH + gap) + pad;
        chipRects.clear();
        return y;
    }

    void paint (juce::Graphics& g) override
    {
        // signal flow: clip (with the note tools) -> effects in order -> out. Click a stage to open it.
        auto a = chainArea.toFloat();
        g.setColour (juce::Colour (0xff0e1116));
        g.fillRoundedRectangle (a, 10.f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (a, 10.f, 1.f);
        g.setColour (Theme::dim);
        g.setFont (juce::FontOptions (10.f, juce::Font::bold));
        g.drawText ("SIGNAL FLOW", a.withHeight (16.f).translated (14.f, 4.f), juce::Justification::centredLeft);

        struct Stage { juce::String name; int module; bool on; juce::Colour c; };
        std::vector<Stage> st;
        const bool notesOn = moduleOn (proc, modules()[0]) || moduleOn (proc, modules()[1]);
        st.push_back ({ "CLIP", 0, notesOn, Theme::accent });
        for (int m : kChain) st.push_back ({ modules()[(size_t) m].name, m, moduleOn (proc, modules()[(size_t) m]), col (modules()[(size_t) m].colour) });
        st.push_back ({ "OUT", 9, true, Theme::dim });

        auto row = a.reduced (12.f, 0.f).withTrimmedTop (22.f).withTrimmedBottom (8.f);
        const float arrow = 16.f, w = (row.getWidth() - arrow * (float) (st.size() - 1)) / (float) st.size();
        chipRects.clear();
        g.setFont (juce::FontOptions (10.f, juce::Font::bold));
        for (size_t i = 0; i < st.size(); ++i)
        {
            auto r = juce::Rectangle<float> (row.getX() + (float) i * (w + arrow), row.getY(), w, row.getHeight());
            chipRects.push_back ({ r.toNearestInt(), st[i].module });
            g.setColour (st[i].on ? st[i].c.withAlpha (0.18f) : juce::Colour (0xff171a20));
            g.fillRoundedRectangle (r, 6.f);
            g.setColour (st[i].on ? st[i].c : Theme::border);
            g.drawRoundedRectangle (r, 6.f, 1.f);
            g.setColour (st[i].on ? Theme::text : Theme::dim.withAlpha (0.6f));
            g.drawFittedText (st[i].name, r.toNearestInt().reduced (3, 0), juce::Justification::centred, 1, 0.7f);
            if (i + 1 < st.size())
            {
                const float ax = r.getRight() + 3.f, ay = r.getCentreY();
                juce::Path p;
                p.addTriangle (ax + arrow - 6.f, ay, ax + arrow - 11.f, ay - 4.f, ax + arrow - 11.f, ay + 4.f);
                g.setColour (Theme::dim.withAlpha (0.6f));
                g.drawLine (ax, ay, ax + arrow - 9.f, ay, 1.2f);
                g.fillPath (p);
            }
        }

        g.setColour (Theme::dim);
        g.setFont (juce::FontOptions (11.f, juce::Font::bold));
        g.drawText ("NOTE TOOLS  -  change the notes themselves", 18, head1, 500, 20, juce::Justification::centredLeft);
        g.drawText ("VOCAL EFFECTS  -  in processing order", 18, head2, 500, 20, juce::Justification::centredLeft);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        for (auto& c : chipRects)
            if (c.first.contains (e.getPosition()) && onOpen) { onOpen (c.second); return; }
    }

    void refresh() { for (auto* c : cards) c->repaint(); repaint (chainArea); }

private:
    RetuneProcessor& proc;
    juce::OwnedArray<Card> cards;
    juce::Rectangle<int> chainArea;
    int head1 = 0, head2 = 0;
    std::vector<std::pair<juce::Rectangle<int>, int>> chipRects;
};

// ===================================================================== settings breakdown (effect page, right side)
class dash::Breakdown : public juce::Component
{
public:
    Breakdown (Module& m, int i) : mod (m), index (i), def (modules()[(size_t) i]) { setInterceptsMouseClicks (false, false); }

    int preferredHeight (int width) const
    {
        juce::ignoreUnused (width);
        return 100 + (int) def.knobs.size() * 46 + (def.toggleId != nullptr ? 46 : 0);
    }

    void paint (juce::Graphics& g) override
    {
        const juce::Colour c = col (def.colour);
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff12151b));
        g.fillRoundedRectangle (r, 12.f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 12.f, 1.f);

        auto in = getLocalBounds().reduced (18, 14);
        g.setColour (Theme::dim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("WHAT IT DOES", in.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (Theme::text.withAlpha (0.9f));
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (moduleDesc (index), in.removeFromTop (46), juce::Justification::topLeft, 3, 1.f);
        in.removeFromTop (8);
        if (def.knobs.empty()) return;
        g.setColour (Theme::dim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("SETTINGS", in.removeFromTop (18), juce::Justification::centredLeft);

        auto row = [&] (const juce::String& name, const juce::String& val, const juce::String& help)
        {
            auto rr = in.removeFromTop (46);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (rr.withHeight (1));
            auto top = rr.withTrimmedTop (6).removeFromTop (16);
            g.setColour (c);
            g.setFont (juce::FontOptions (12.f, juce::Font::bold));
            g.drawText (name, top, juce::Justification::centredLeft);
            lcd (g, val, top, c);
            g.setColour (Theme::dim);
            g.setFont (juce::FontOptions (11.f));
            g.drawFittedText (help, rr.withTrimmedTop (22).removeFromTop (24), juce::Justification::topLeft, 2, 1.f);
        };
        for (int k = 0; k < mod.numKnobs(); ++k)
            row (def.knobs[(size_t) k].label, mod.knobText (k), knobHelp (index, k));
        if (def.toggleId != nullptr)
            row (def.toggleLabel, mod.extraOn() ? "ON" : "OFF", "On: altos sing a third below the melody in the current key. Off: altos sing the melody.");
    }

private:
    static void lcd (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Colour c)
    {
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.f, juce::Font::bold));
        g.setColour (c.brighter (0.3f));
        g.drawText (t, r, juce::Justification::centredRight);
    }

    Module& mod;
    const int index;
    const ModuleDef& def;
};

// ===================================================================== effect page
class dash::Page : public juce::Component
{
public:
    Page (RetuneProcessor& p, NoteEditor& e, int i, KnobLook& look) : module (p, e, i, look), breakdown (module, i)
    {
        module.setMaxCell (112);
        addAndMakeVisible (module);
        addAndMakeVisible (breakdown);
    }

    int layout (int width, int minHeight)
    {
        const int pad = 16, gap = 14;
        const int bw = juce::jlimit (300, 400, width / 3);
        const int h = juce::jmax (minHeight, breakdown.preferredHeight (bw) + 2 * pad, 470);
        module.setBounds (pad, pad, width - 2 * pad - gap - bw, h - 2 * pad);
        breakdown.setBounds (width - pad - bw, pad, bw, h - 2 * pad);
        return h;
    }

    void tick() { module.tick(); breakdown.repaint(); }

    Module module;
    Breakdown breakdown;
};

// ===================================================================== dashboard
int FxDashboard::numEffects() { return (int) modules().size() - 1; }
juce::String FxDashboard::effectName (int i) { return effectTitle (i); }
juce::String FxDashboard::effectOnParam (int i) { return modules()[(size_t) i].onId; }

FxDashboard::FxDashboard (RetuneProcessor& p, NoteEditor& e) : proc (p), editor (e)
{
    setLookAndFeel (&look);
    setWantsKeyboardFocus (false);

    for (int i = 0; i < (int) modules().size(); ++i) jump.addItem (effectTitle (i), i + 1);
    jump.setTextWhenNothingSelected ("Go to effect...");
    jump.onChange = [this] { if (jump.getSelectedItemIndex() >= 0) showEffect (jump.getSelectedItemIndex()); };

    backBtn.onClick  = [this] { goBack(); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    prevBtn.onClick  = [this] { showEffect ((page + (int) modules().size() - 1) % (int) modules().size()); };
    nextBtn.onClick  = [this] { showEffect ((page + 1) % (int) modules().size()); };
    prevBtn.setConnectedEdges (juce::Button::ConnectedOnRight);
    nextBtn.setConnectedEdges (juce::Button::ConnectedOnLeft);
    prevBtn.setTooltip ("Previous effect");
    nextBtn.setTooltip ("Next effect");
    closeBtn.setTooltip ("Back to the notes (Esc)");
    allOff.onClick = [this]
    {
        for (int i = 2; i < numEffects(); ++i)
            if (auto* prm = proc.apvts.getParameter (effectOnParam (i)))
                prm->setValueNotifyingHost (0.f);
    };
    for (auto* c : std::initializer_list<juce::Component*> { &backBtn, &closeBtn, &prevBtn, &nextBtn, &jump, &allOff, &view })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (c);
    }

    overview = std::make_unique<dash::Overview> (proc, editor, look);
    overview->onOpen = [this] (int i) { showEffect (i); };
    view.setScrollBarsShown (true, false);
    view.setScrollBarThickness (8);
    showOverview();
    startTimerHz (30);
}

FxDashboard::~FxDashboard()
{
    view.setViewedComponent (nullptr, false);
    pageComp.reset();
    overview.reset();
    setLookAndFeel (nullptr);
}

void FxDashboard::showOverview()
{
    page = -1;
    view.setViewedComponent (overview.get(), false);
    pageComp.reset();
    jump.setSelectedItemIndex (-1, juce::dontSendNotification);
    backBtn.setButtonText ("<  Back to notes");
    resized();
    view.setViewPosition (0, 0);
    repaint();
}

void FxDashboard::showEffect (int index)
{
    if (index < 0 || index >= (int) modules().size()) return;
    page = index;
    auto np = std::make_unique<dash::Page> (proc, editor, index, look);
    view.setViewedComponent (np.get(), false);
    pageComp = std::move (np);
    jump.setSelectedItemIndex (index, juce::dontSendNotification);
    backBtn.setButtonText ("<  All effects");
    resized();
    view.setViewPosition (0, 0);
    repaint();
}

void FxDashboard::goBack()
{
    if (page >= 0) showOverview();
    else if (onClose) onClose();
}

void FxDashboard::timerCallback()
{
    if (! isShowing()) return;
    if (pageComp != nullptr) pageComp->tick();
    else if (++slowTick % 3 == 0) overview->refresh();
}

void FxDashboard::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0f1218), 0.f, 0.f, juce::Colour (0xff0a0c10), 0.f, (float) getHeight(), false));
    g.fillAll();
    auto top = getLocalBounds().removeFromTop (kHeadH);
    g.setColour (juce::Colour (0xff12151b));
    g.fillRect (top);
    g.setColour (Theme::border);
    g.fillRect (top.removeFromBottom (1));

    const int tx = backBtn.getRight() + 18;
    const int tw = (page >= 0 ? prevBtn.getX() : jump.getX()) - tx - 10;
    if (page < 0)
    {
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (17.f, juce::Font::bold));
        g.drawText ("Effects", tx, 0, 100, kHeadH, juce::Justification::centredLeft);
        g.setColour (Theme::dim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ("Click any effect to open it. Power buttons switch effects on and off.", tx + 86, 0, juce::jmax (0, tw - 86), kHeadH, juce::Justification::centredLeft);
    }
    else
    {
        const auto& d = dash::modules()[(size_t) page];
        g.setColour (juce::Colour (d.colour));
        g.fillRoundedRectangle ((float) tx, (float) kHeadH * 0.5f - 9.f, 3.f, 18.f, 1.5f);
        g.setColour (Theme::dim);
        g.setFont (juce::FontOptions (12.f));
        g.drawText ("Effects  /", tx + 12, 0, 70, kHeadH, juce::Justification::centredLeft);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (17.f, juce::Font::bold));
        g.drawText (d.name, tx + 80, 0, juce::jmax (0, tw - 80), kHeadH, juce::Justification::centredLeft);
    }
}

void FxDashboard::resized()
{
    auto b = getLocalBounds();
    auto top = b.removeFromTop (kHeadH).reduced (14, 11);
    backBtn.setBounds (top.removeFromLeft (page >= 0 ? 118 : 140));
    const bool onPage = page >= 0;
    closeBtn.setVisible (true);
    closeBtn.setBounds (top.removeFromRight (84));
    top.removeFromRight (10);
    allOff.setVisible (! onPage);
    if (! onPage) { allOff.setBounds (top.removeFromRight (130)); top.removeFromRight (10); }
    jump.setBounds (top.removeFromRight (180));
    top.removeFromRight (10);
    prevBtn.setVisible (onPage);
    nextBtn.setVisible (onPage);
    if (onPage)
    {
        nextBtn.setBounds (top.removeFromRight (34));
        prevBtn.setBounds (top.removeFromRight (34));
    }
    view.setBounds (b);

    const int w = view.getWidth() - view.getScrollBarThickness();
    if (page < 0 && overview != nullptr) overview->setSize (w, overview->layout (w));
    if (pageComp != nullptr) pageComp->setSize (w, pageComp->layout (w, view.getHeight()));
}

bool FxDashboard::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { goBack(); return true; }
    return false;
}
