#pragma once
#include "PluginProcessor.h"
#include "Look.h"

class NoteEditor;

// The Effects dashboard: every effect as its own hardware-style module with a power switch, machined knobs and a
// live display (meters, curves, the choir on stage...). A dropdown at the top jumps to any module.
namespace dash
{
    enum class Fmt { Pct, Pct200, Db, Hz, Ms, Ratio, Cents, Division };
    enum class Vis { Glide, Drift, DeEss, Air, Comp, Choir, Doubler, Echo, Reverb, Output };

    struct KnobDef
    {
        const char* id;            // parameter id, or nullptr for a per-note field
        const char* label;
        Fmt fmt;
        int field = -1;            // per-note field (see NoteEditor::setShape)
        double lo = 0.0, hi = 1.0, def = 0.0;
    };

    struct ModuleDef
    {
        const char* name;
        const char* subtitle;
        const char* onId;          // nullptr: no power switch (the output module)
        juce::uint32 colour;
        Vis vis;
        int span;                  // grid columns
        std::vector<KnobDef> knobs;
        const char* toggleId = nullptr;
        const char* toggleLabel = nullptr;
    };

    const std::vector<ModuleDef>& modules();
    juce::String formatValue (Fmt, double);

    // Machined aluminium knob with a knurled skirt, a value ring in the module's colour and a printed scale.
    class KnobLook : public ModernLAF
    {
    public:
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool hl, bool down) override;
    };

    class Module;
}

class FxDashboard : public juce::Component, private juce::Timer
{
public:
    FxDashboard (RetuneProcessor&, NoteEditor&);
    ~FxDashboard() override;
    void focusModule (int index);                 // index into dash::modules()
    void paint (juce::Graphics&) override;
    void resized() override;

    static int numEffects();                      // modules that are effects (for menus)
    static juce::String effectName (int i);
    static juce::String effectOnParam (int i);

private:
    void timerCallback() override;

    RetuneProcessor& proc;
    NoteEditor& editor;
    dash::KnobLook look;
    juce::ComboBox jump;
    juce::TextButton allOff { "All effects off" };
    juce::Viewport view;
    juce::Component content;
    juce::OwnedArray<dash::Module> mods;
    int highlight = -1, highlightTicks = 0;
};
