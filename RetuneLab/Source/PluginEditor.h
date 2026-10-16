#pragma once
#include <functional>
#include "PluginProcessor.h"
#include "Look.h"

// ---- scale tables shared by the note editor and the toolbar
inline const juce::StringArray& scaleNames()
{
    static const juce::StringArray n { "Major", "Minor", "Chromatic", "Major Pentatonic", "Minor Pentatonic", "Blues" };
    return n;
}
inline int scaleMaskFor (int root, int scale)
{
    static const std::vector<std::vector<int>> deg {
        { 0,2,4,5,7,9,11 }, { 0,2,3,5,7,8,10 }, { 0,1,2,3,4,5,6,7,8,9,10,11 },
        { 0,2,4,7,9 }, { 0,3,5,7,10 }, { 0,3,5,6,7,10 } };
    int mask = 0;
    for (int d : deg[(size_t) juce::jlimit (0, (int) deg.size() - 1, scale)]) mask |= 1 << ((d + root) % 12);
    return mask;
}

class NoteEditor : public juce::Component, private juce::Timer
{
public:
    enum class Tool { Edit, Split };

    explicit NoteEditor (RetuneProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverX = -1.f; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void setTool (Tool t) { tool = t; repaint(); }
    void snapAll (bool toScale);                  // every note, regardless of selection
    void perform (Act);
    void undo();
    void snapToScale();
    void snapSemitone();
    void selectAll();
    void deleteSelected();
    void mergeSelected();
    void resetSelected();
    void splitAtPlayhead();
    void fitView();

    std::function<void (Act)> onExtraAction;       // record / tool switching, handled by the editor

private:
    enum Zone { None, Body, LeftEdge, RightEdge };
    static constexpr float kKeyW = 56.f, kRuler = 16.f;

    void timerCallback() override;
    double sr() const { return proc.getSr(); }
    juce::Rectangle<float> rollArea() const { auto b = getLocalBounds().toFloat(); b.removeFromLeft (kKeyW); return b; }
    float xFromSample (double s) const { return kKeyW + (float) ((s / sr() - scrollSec) * pps); }
    double sampleFromX (float x) const { return ((x - kKeyW) / pps + scrollSec) * sr(); }
    float yFromMidi (float m) const { return (float) ((topMidi - m) * rowH); }
    float midiFromY (float y) const { return (float) (topMidi - y / rowH); }
    juce::Rectangle<float> noteRect (const Note&) const;

    int  hitNote (juce::Point<float>, const std::vector<Note>&, Zone&) const;
    void pushUndo (const std::vector<Note>&);
    bool splitAt (std::vector<Note>&, int idx, int outSample);
    bool mergeNext (std::vector<Note>&, int idx);
    float nearestInScale (float t) const;
    void showContextMenu (int idx, int splitSample);
    void runMenuAction (int result, int idx, int splitSample);
    void commit (std::vector<Note> v) { proc.setNotes (std::move (v)); proc.requestRender(); }

    RetuneProcessor& proc;
    Tool tool = Tool::Edit;
    ClipAnalysis analysisCache;
    int lastVersion = -1;
    bool keyDrag = false, keyPaintState = true;   // painting allowed notes on the keyboard strip
    int pitchClassAtY (float y) const { return ((((int) std::floor (midiFromY (y) + 0.5f)) % 12) + 12) % 12; }
    double scrollSec = 0.0, pps = 100.0, rowH = 16.0, topMidi = 84.0;
    float hoverX = -1.f;

    std::vector<std::vector<Note>> undoStack;
    std::vector<Note> dragStart;
    int dragIdx = -1;
    Zone dragZone = None;
    bool didDrag = false;

    bool marquee = false;                         // drag-to-select rectangle
    juce::Point<float> marqueeStart;
    juce::Rectangle<float> marqueeRect;
    std::vector<bool> marqueeBase;
};

// ---- small floating windows -------------------------------------------------------------
class ToolWindow : public juce::DocumentWindow
{
public:
    explicit ToolWindow (const juce::String& name)
        : juce::DocumentWindow (name, Theme::panel, juce::DocumentWindow::closeButton) {}
    void closeButtonPressed() override { setVisible (false); }
};

class AirPanel : public juce::Component
{
public:
    explicit AirPanel (RetuneProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    juce::Slider mid, high;
    juce::Label midL, highL;
    juce::ToggleButton on { "Enabled" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aMid, aHigh;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aOn;
};

class HotkeyPanel : public juce::Component
{
public:
    explicit HotkeyPanel (RetuneProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
private:
    void refresh();
    RetuneProcessor& proc;
    std::array<juce::TextButton, (size_t) HotkeyMap::N> btns;
    std::array<juce::Label, (size_t) HotkeyMap::N> labels;
    juce::TextButton resetBtn { "Reset to defaults" };
    int capturing = -1;
};

class RetuneEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RetuneEditor (RetuneProcessor&);
    ~RetuneEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& k) override { return noteEditor.keyPressed (k); }

private:
    void timerCallback() override;
    void updateScale();
    void showAir();
    void showHotkeys();
    void toggleRecord();
    void selectTool (bool split);

    ModernLAF laf;                                   // must outlive everything below
    RetuneProcessor& proc;
    NoteEditor noteEditor;
    juce::TextButton recBtn { "Record" }, editBtn { "Edit" }, splitBtn { "Split" },
                     undoBtn { "Undo" }, snapBtn { "Snap all: Scale" }, snapSemiBtn { "Snap all: Semitone" }, clearBtn { "Clear" },
                     airBtn { "Air" }, keysBtn { "Hotkeys" };
    juce::ComboBox keyBox, scaleBox;
    juce::Label statusLabel;
    std::unique_ptr<ToolWindow> airWin, keyWin;
};
