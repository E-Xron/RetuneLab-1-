#pragma once
#include <functional>
#include "PluginProcessor.h"

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
    void setScaleMask (int m) { scaleMask = m; repaint(); }
    void undo();
    void snapToScale();
    void selectAll();
    void deleteSelected();
    void fitView();

private:
    enum Zone { None, Body, LeftEdge, RightEdge };
    static constexpr float kKeyW = 56.f;

    void timerCallback() override;
    double sr() const { return proc.getSr(); }
    juce::Rectangle<float> rollArea() const { auto b = getLocalBounds().toFloat(); b.removeFromLeft (kKeyW); return b; }
    float xFromSample (double s) const { return kKeyW + (float) ((s / sr() - scrollSec) * pps); }
    double sampleFromX (float x) const { return ((x - kKeyW) / pps + scrollSec) * sr(); }
    float yFromMidi (float m) const { return (float) ((topMidi - m) * rowH); }
    float midiFromY (float y) const { return (float) (topMidi - y / rowH); }

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
    int scaleMask = 2741;                         // C major
    double scrollSec = 0.0, pps = 100.0, rowH = 16.0, topMidi = 84.0;
    float hoverX = -1.f;

    std::vector<std::vector<Note>> undoStack;
    std::vector<Note> dragStart;
    int dragIdx = -1;
    Zone dragZone = None;
    bool didDrag = false;
};

class RetuneEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RetuneEditor (RetuneProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateScale();

    RetuneProcessor& proc;
    NoteEditor noteEditor;
    juce::TextButton recBtn { "Record" }, editBtn { "Edit" }, splitBtn { "Split" },
                     undoBtn { "Undo" }, snapBtn { "Snap to scale" }, clearBtn { "Clear" };
    juce::ComboBox keyBox, scaleBox;
    juce::Label statusLabel;
};
