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
    class Card;
    class Overview;
    class Breakdown;
    class Page;
}

// The Effects view: lives inside the plugin window. An overview of every effect and its settings, and a page for
// each effect with big knobs, its display and a breakdown of what every control does.
class FxDashboard : public juce::Component, private juce::Timer
{
public:
    FxDashboard (RetuneProcessor&, NoteEditor&);
    ~FxDashboard() override;
    void showOverview();
    void showEffect (int index);                  // index into dash::modules()
    void goBack();                                // effect page -> overview -> close
    bool isOverview() const { return page < 0; }
    std::function<void()> onClose;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    static int numEffects();                      // modules that are effects (for menus)
    static juce::String effectName (int i);
    static juce::String effectOnParam (int i);

private:
    void timerCallback() override;
    static constexpr int kHeadH = 54;

    RetuneProcessor& proc;
    NoteEditor& editor;
    dash::KnobLook look;
    juce::TextButton backBtn { "<  Back" }, closeBtn { "Close" }, prevBtn { "<" }, nextBtn { ">" }, allOff { "All effects off" };
    juce::ComboBox jump;
    juce::Viewport view;
    std::unique_ptr<dash::Overview> overview;
    std::unique_ptr<dash::Page> pageComp;
    int page = -1, slowTick = 0;
};
