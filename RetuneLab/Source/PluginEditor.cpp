#include "PluginEditor.h"
#include <cmath>

namespace
{
    const juce::Colour kBg     { 0xff14161b };
    const juce::Colour kPanel  { 0xff1b1e25 };
    const juce::Colour kBlob   { 0xff4f7cff };
    const juce::Colour kSelect { 0xffffa94d };
    inline bool isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
}

// ===================================================================== NoteEditor
NoteEditor::NoteEditor (RetuneProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

void NoteEditor::timerCallback()
{
    const int ver = proc.analysisVersion.load();
    if (ver != lastVersion)
    {
        lastVersion = ver;
        analysisCache = proc.getAnalysis();
        undoStack.clear();
        fitView();
    }
    repaint();
}

void NoteEditor::fitView()
{
    auto v = proc.getNotes();
    if (v.empty() || getWidth() < 100) return;
    float lo = 127.f, hi = 0.f;
    for (auto& n : v) { lo = std::min (lo, n.targetMid); hi = std::max (hi, n.targetMid); }
    rowH = juce::jlimit (10.0, 28.0, (double) getHeight() / (double) (hi - lo + 9.f));
    topMidi = (double) hi + 4.0;
    const double secs = std::max (1.0, (double) proc.clipLen.load() / sr());
    pps = juce::jlimit (20.0, 400.0, (double) (getWidth() - kKeyW) / secs);
    scrollSec = 0.0;
}

int NoteEditor::hitNote (juce::Point<float> p, const std::vector<Note>& v, Zone& z) const
{
    for (int i = (int) v.size() - 1; i >= 0; --i)
    {
        const auto& n = v[(size_t) i];
        const float x0 = xFromSample (n.outStart), x1 = xFromSample (n.outEnd());
        const float h = (float) rowH * 0.8f, yc = yFromMidi (n.targetMid);
        juce::Rectangle<float> r (x0, yc - h * 0.5f, std::max (2.f, x1 - x0), h);
        if (r.expanded (0.f, 3.f).contains (p))
        {
            const float edge = std::min (7.f, r.getWidth() / 3.f);
            z = (p.x - r.getX() < edge) ? LeftEdge : (r.getRight() - p.x < edge ? RightEdge : Body);
            return i;
        }
    }
    z = None;
    return -1;
}

void NoteEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    const auto roll = rollArea();

    // pitch rows + keyboard
    const int topN = (int) std::ceil (topMidi + 0.5), botN = (int) std::floor (midiFromY ((float) getHeight()) - 0.5f);
    for (int n = topN; n >= botN; --n)
    {
        if (n < 0 || n > 127) continue;
        const int pc = n % 12;
        const float y0 = yFromMidi ((float) n + 0.5f);
        const bool inScale = ((scaleMask >> pc) & 1) != 0, blk = isBlack (pc);
        juce::Colour c = blk ? juce::Colour (0xff15171c) : juce::Colour (0xff1b1e25);
        if (inScale) c = c.interpolatedWith (juce::Colour (0xff35705f), 0.22f);
        g.setColour (c);
        g.fillRect (roll.getX(), y0, roll.getWidth(), (float) rowH);
        g.setColour (juce::Colours::white.withAlpha (pc == 0 ? 0.20f : 0.05f));
        g.drawHorizontalLine ((int) y0, roll.getX(), roll.getRight());

        juce::Rectangle<float> k (0.f, y0, kKeyW, (float) rowH);
        g.setColour (blk ? juce::Colour (0xff262a33) : juce::Colour (0xffe6e6e6));
        g.fillRect (k.reduced (0.f, 0.5f));
        if (pc == 0)
        {
            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.setFont (juce::FontOptions (11.f));
            g.drawText ("C" + juce::String (n / 12 - 2), k.withTrimmedLeft (6.f), juce::Justification::centredLeft);
        }
    }

    // time grid
    const double visibleSec = roll.getWidth() / pps;
    const double step = pps >= 40.0 ? 1.0 : (pps >= 12.0 ? 5.0 : 15.0);
    g.setFont (juce::FontOptions (10.f));
    for (double t = std::ceil (scrollSec / step) * step; t < scrollSec + visibleSec; t += step)
    {
        const float x = kKeyW + (float) ((t - scrollSec) * pps);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawVerticalLine ((int) x, 0.f, (float) getHeight());
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawText (juce::String (t, 0) + "s", (int) x + 3, 2, 40, 12, juce::Justification::left);
    }

    // notes
    const auto v = proc.getNotes();
    for (const auto& n : v)
    {
        const float x0 = xFromSample (n.outStart), x1 = xFromSample (n.outEnd());
        if (x1 < kKeyW || x0 > (float) getWidth()) continue;
        const float h = (float) rowH * 0.78f, yc = yFromMidi (n.targetMid);
        const juce::Rectangle<float> r (x0, yc - h * 0.5f, std::max (2.f, x1 - x0), h);

        const bool edited = n.outStart != n.srcStart || n.outLen != n.srcLen() || std::abs (n.targetMid - n.detectedMid) > 0.01f;
        if (edited && ! n.muted)
        {
            const float gx0 = xFromSample (n.srcStart), gx1 = xFromSample (n.srcEnd), gy = yFromMidi (n.detectedMid);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.drawRoundedRectangle (gx0, gy - h * 0.5f, std::max (2.f, gx1 - gx0), h, 3.f, 1.f);
        }

        juce::Colour fill = n.muted ? juce::Colours::grey.withAlpha (0.25f) : (n.selected ? kSelect : kBlob).withAlpha (0.88f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, 4.f);
        g.setColour (juce::Colours::white.withAlpha (n.selected ? 0.9f : 0.4f));
        g.drawRoundedRectangle (r, 4.f, 1.f);

        if (! n.muted && ! analysisCache.contour.empty() && n.srcLen() > 0)
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (r.expanded (0.f, 2.f).toNearestInt());
            juce::Path path;
            bool pen = false;
            const int nf = (int) analysisCache.contour.size();
            const int fa = std::max (0, (int) std::ceil ((double) (n.srcStart - analysisCache.frameOffset) / analysisCache.hop));
            const int fb = std::min (nf - 1, (int) std::floor ((double) (n.srcEnd - analysisCache.frameOffset) / analysisCache.hop));
            for (int f = fa; f <= fb; ++f)
            {
                const float c = analysisCache.contour[(size_t) f];
                if (std::isnan (c)) { pen = false; continue; }
                const double s = (double) f * analysisCache.hop + analysisCache.frameOffset;
                const double outS = n.outStart + (s - n.srcStart) * (double) n.outLen / (double) n.srcLen();
                const float x = xFromSample (outS), y = yFromMidi (c + n.targetMid - n.detectedMid);
                if (! pen) { path.startNewSubPath (x, y); pen = true; } else path.lineTo (x, y);
            }
            g.setColour (kBg.withAlpha (0.85f));
            g.strokePath (path, juce::PathStrokeType (1.3f));
        }
    }

    if (tool == Tool::Split && hoverX >= kKeyW)
    {
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawVerticalLine ((int) hoverX, 0.f, (float) getHeight());
    }

    // playhead
    if (proc.clipLen.load() > 0)
    {
        const double s = (double) (proc.hostPos.load() - proc.clipOrigin.load());
        const float x = xFromSample (s);
        if (x >= kKeyW && x <= (float) getWidth())
        {
            g.setColour (juce::Colour (0xffff5c5c));
            g.drawVerticalLine ((int) x, 0.f, (float) getHeight());
        }
    }

    if (v.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (15.f));
        g.drawFittedText ("Click Record, then play the audio in Ableton (it records from the playhead).\n"
                          "Click Record again to stop. Notes appear here for editing.",
                          getLocalBounds().withTrimmedLeft ((int) kKeyW).reduced (40), juce::Justification::centred, 3);
    }
}

// ---------------------------------------------------------------- mouse
void NoteEditor::pushUndo (const std::vector<Note>& v)
{
    undoStack.push_back (v);
    if (undoStack.size() > 60) undoStack.erase (undoStack.begin());
}

void NoteEditor::mouseMove (const juce::MouseEvent& e)
{
    hoverX = e.position.x;
    auto v = proc.getNotes();
    Zone z;
    const int idx = hitNote (e.position, v, z);
    if (tool == Tool::Split)           setMouseCursor (juce::MouseCursor::CrosshairCursor);
    else if (idx >= 0 && z != Body)    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (idx >= 0)                 setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else                               setMouseCursor (juce::MouseCursor::NormalCursor);
    if (tool == Tool::Split) repaint();
}

void NoteEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    auto v = proc.getNotes();
    Zone z;
    const int idx = hitNote (e.position, v, z);
    const int clickSample = (int) sampleFromX (e.position.x);

    if (e.mods.isPopupMenu()) { if (idx >= 0) showContextMenu (idx, clickSample); return; }

    if (tool == Tool::Split)
    {
        if (idx >= 0)
        {
            pushUndo (v);
            if (splitAt (v, idx, clickSample)) commit (v); else undoStack.pop_back();
        }
        return;
    }

    dragIdx = -1;
    didDrag = false;
    if (idx < 0)
    {
        if (! e.mods.isCommandDown()) { for (auto& n : v) n.selected = false; proc.setNotes (v); }
        return;
    }
    if (e.mods.isCommandDown()) v[(size_t) idx].selected = ! v[(size_t) idx].selected;
    else if (! v[(size_t) idx].selected) { for (auto& n : v) n.selected = false; v[(size_t) idx].selected = true; }
    proc.setNotes (v);
    if (! v[(size_t) idx].selected) return;

    pushUndo (v);
    dragStart = v;
    dragIdx = idx;
    dragZone = z;
}

void NoteEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIdx < 0 || dragIdx >= (int) dragStart.size()) return;
    if (! didDrag && e.getDistanceFromDragStart() < 3) return;
    didDrag = true;

    const int minLen = (int) (0.03 * sr());
    double dxPx = e.getDistanceFromDragStartX(), dyPx = e.getDistanceFromDragStartY();
    auto v = dragStart;

    if (dragZone == Body)
    {
        if (e.mods.isAltDown()) { if (std::abs (dxPx) > std::abs (dyPx)) dyPx = 0; else dxPx = 0; }
        const int dS = (int) std::lround (dxPx / pps * sr());
        const float dSemi = (float) (-dyPx / rowH);
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (! v[i].selected) continue;
            v[i].outStart = std::max (0, dragStart[i].outStart + dS);
            float t = dragStart[i].targetMid + dSemi;
            if (! e.mods.isShiftDown() && std::abs (dyPx) > 3.0) t = std::round (t);   // snap to semitone; Shift = fine
            v[i].targetMid = t;
        }
    }
    else
    {
        const int dS = (int) std::lround (dxPx / pps * sr());
        auto& n = v[(size_t) dragIdx];
        const auto& s0 = dragStart[(size_t) dragIdx];
        if (dragZone == LeftEdge)
        {
            const int end = s0.outEnd();
            const int ns = juce::jlimit (0, std::max (0, end - minLen), s0.outStart + dS);
            n.outStart = ns; n.outLen = end - ns;
        }
        else n.outLen = std::max (minLen, s0.outLen + dS);
    }
    proc.setNotes (v);
}

void NoteEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragIdx < 0) return;
    if (didDrag) proc.requestRender();
    else if (! undoStack.empty()) undoStack.pop_back();      // plain click: no edit happened
    dragIdx = -1;
}

void NoteEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown())
    {
        const double tAt = (e.position.x - kKeyW) / pps + scrollSec;
        pps = juce::jlimit (5.0, 2000.0, pps * std::exp ((double) w.deltaY * 0.6));
        scrollSec = tAt - (e.position.x - kKeyW) / pps;
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        scrollSec -= (double) (w.deltaX != 0.f ? w.deltaX : w.deltaY) * (300.0 / pps);
    else
        topMidi = juce::jlimit (24.0, 127.0, topMidi + (double) w.deltaY * 4.0);
    scrollSec = std::max (0.0, scrollSec);
    repaint();
}

bool NoteEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelected(); return true; }
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'Z') { undo(); return true; }
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'A') { selectAll(); return true; }
    return false;
}

// ---------------------------------------------------------------- edit operations
bool NoteEditor::splitAt (std::vector<Note>& v, int idx, int outSample)
{
    const Note n = v[(size_t) idx];
    const int minLen = (int) (0.03 * sr());
    if (outSample < n.outStart + minLen || outSample > n.outEnd() - minLen) return false;
    const double frac = (double) (outSample - n.outStart) / (double) n.outLen;
    const int srcSplit = n.srcStart + (int) std::lround (frac * n.srcLen());
    if (srcSplit - n.srcStart < 128 || n.srcEnd - srcSplit < 128) return false;

    const float offset = n.targetMid - n.detectedMid;
    Note A = n, B = n;
    A.srcEnd = srcSplit;  A.outLen = outSample - n.outStart;
    B.srcStart = srcSplit; B.outStart = outSample; B.outLen = n.outEnd() - outSample;
    auto fix = [&] (Note& x)
    {
        const float m = ClipEngine::medianPitch (analysisCache, x.srcStart, x.srcEnd);
        if (! std::isnan (m)) x.detectedMid = m;
        x.targetMid = x.detectedMid + offset;
    };
    fix (A); fix (B);
    v[(size_t) idx] = A;
    v.insert (v.begin() + idx + 1, B);
    return true;
}

bool NoteEditor::mergeNext (std::vector<Note>& v, int idx)
{
    int best = -1;
    for (int i = 0; i < (int) v.size(); ++i)
        if (i != idx && v[(size_t) i].srcStart >= v[(size_t) idx].srcEnd - 1
            && (best < 0 || v[(size_t) i].srcStart < v[(size_t) best].srcStart))
            best = i;
    if (best < 0) return false;

    Note& a = v[(size_t) idx];
    const Note b = v[(size_t) best];
    const float offset = a.targetMid - a.detectedMid;
    a.srcEnd = b.srcEnd;
    a.outLen = std::max (a.outEnd(), b.outEnd()) - a.outStart;
    const float med = ClipEngine::medianPitch (analysisCache, a.srcStart, a.srcEnd);
    if (! std::isnan (med)) { a.detectedMid = med; a.targetMid = med + offset; }
    v.erase (v.begin() + best);
    return true;
}

float NoteEditor::nearestInScale (float t) const
{
    if (scaleMask == 0) return t;
    float best = t, bestD = 1e9f;
    const int c = (int) std::round (t);
    for (int n = c - 7; n <= c + 7; ++n)
    {
        if (! ((scaleMask >> (((n % 12) + 12) % 12)) & 1)) continue;
        const float d = std::abs ((float) n - t);
        if (d < bestD) { bestD = d; best = (float) n; }
    }
    return best;
}

void NoteEditor::showContextMenu (int idx, int splitSample)
{
    auto v = proc.getNotes();
    if (idx >= (int) v.size()) return;
    if (! v[(size_t) idx].selected) { for (auto& n : v) n.selected = false; v[(size_t) idx].selected = true; proc.setNotes (v); }

    juce::PopupMenu m;
    m.addItem (1, "Split here");
    m.addItem (2, "Merge with next note");
    m.addSeparator();
    m.addItem (3, "Snap to nearest semitone");
    m.addItem (4, "Snap to scale");
    m.addItem (5, "Reset note(s)");
    m.addItem (6, v[(size_t) idx].muted ? "Restore note" : "Delete note");

    juce::Component::SafePointer<NoteEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, idx, splitSample] (int r)
    {
        if (safe != nullptr && r != 0) safe->runMenuAction (r, idx, splitSample);
    });
}

void NoteEditor::runMenuAction (int r, int idx, int splitSample)
{
    auto v = proc.getNotes();
    if (idx < 0 || idx >= (int) v.size()) return;
    pushUndo (v);
    switch (r)
    {
        case 1: if (! splitAt (v, idx, splitSample)) { undoStack.pop_back(); return; } break;
        case 2: if (! mergeNext (v, idx))            { undoStack.pop_back(); return; } break;
        case 3: for (auto& n : v) if (n.selected) n.targetMid = std::round (n.targetMid); break;
        case 4: for (auto& n : v) if (n.selected) n.targetMid = nearestInScale (n.targetMid); break;
        case 5: for (auto& n : v) if (n.selected) { n.outStart = n.srcStart; n.outLen = n.srcLen(); n.targetMid = n.detectedMid; n.muted = false; } break;
        case 6: { const bool m = ! v[(size_t) idx].muted; for (auto& n : v) if (n.selected) n.muted = m; break; }
        default: break;
    }
    commit (v);
}

void NoteEditor::snapToScale()
{
    auto v = proc.getNotes();
    if (v.empty()) return;
    pushUndo (v);
    bool any = false;
    for (auto& n : v) any = any || n.selected;
    for (auto& n : v) if (! any || n.selected) n.targetMid = nearestInScale (n.targetMid);
    commit (v);
}

void NoteEditor::selectAll()
{
    auto v = proc.getNotes();
    for (auto& n : v) n.selected = true;
    proc.setNotes (v);
}

void NoteEditor::deleteSelected()
{
    auto v = proc.getNotes();
    pushUndo (v);
    bool any = false;
    for (auto& n : v) if (n.selected) { n.muted = true; any = true; }
    if (any) commit (v); else undoStack.pop_back();
}

void NoteEditor::undo()
{
    if (undoStack.empty()) return;
    commit (undoStack.back());
    undoStack.pop_back();
}

// ===================================================================== RetuneEditor
static const juce::StringArray kScaleNames { "Major", "Minor", "Chromatic", "Major Pentatonic", "Minor Pentatonic", "Blues" };
static const std::vector<std::vector<int>> kScaleDegrees {
    { 0,2,4,5,7,9,11 }, { 0,2,3,5,7,8,10 }, { 0,1,2,3,4,5,6,7,8,9,10,11 },
    { 0,2,4,7,9 }, { 0,3,5,7,10 }, { 0,3,5,6,7,10 } };

RetuneEditor::RetuneEditor (RetuneProcessor& p)
    : AudioProcessorEditor (&p), proc (p), noteEditor (p)
{
    addAndMakeVisible (noteEditor);

    recBtn.setClickingTogglesState (true);
    recBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd23c3c));
    recBtn.onClick = [this] { proc.setRecording (recBtn.getToggleState()); };

    editBtn.setClickingTogglesState (true);  editBtn.setRadioGroupId (1);  editBtn.setToggleState (true, juce::dontSendNotification);
    splitBtn.setClickingTogglesState (true); splitBtn.setRadioGroupId (1);
    editBtn.onClick  = [this] { noteEditor.setTool (NoteEditor::Tool::Edit); };
    splitBtn.onClick = [this] { noteEditor.setTool (NoteEditor::Tool::Split); };
    undoBtn.onClick  = [this] { noteEditor.undo(); };
    snapBtn.onClick  = [this] { noteEditor.snapToScale(); };
    clearBtn.onClick = [this] { proc.clearClip(); };

    keyBox.addItemList ({ "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 1);
    scaleBox.addItemList (kScaleNames, 1);
    keyBox.setSelectedItemIndex (0, juce::dontSendNotification);
    scaleBox.setSelectedItemIndex (0, juce::dontSendNotification);
    keyBox.onChange = scaleBox.onChange = [this] { updateScale(); };

    for (auto* c : std::initializer_list<juce::Component*> { &recBtn, &editBtn, &splitBtn, &undoBtn, &snapBtn, &clearBtn, &keyBox, &scaleBox, &statusLabel })
        addAndMakeVisible (c);
    statusLabel.setJustificationType (juce::Justification::centredRight);

    setResizable (true, true);
    setResizeLimits (760, 460, 1800, 1100);
    setSize (1000, 640);
    startTimerHz (10);
}

void RetuneEditor::updateScale()
{
    const int key = keyBox.getSelectedItemIndex();
    const int sc = juce::jlimit (0, (int) kScaleDegrees.size() - 1, scaleBox.getSelectedItemIndex());
    int mask = 0;
    for (int d : kScaleDegrees[(size_t) sc]) mask |= 1 << ((d + key) % 12);
    noteEditor.setScaleMask (mask);
}

void RetuneEditor::timerCallback()
{
    recBtn.setToggleState (proc.recordArmed.load(), juce::dontSendNotification);
    juce::String s;
    if (proc.recordArmed.load())
        s = proc.hostPlaying.load() ? "Recording..." : "Armed - press play in Ableton";
    else switch (proc.status.load())
    {
        case RetuneProcessor::Analysing: s = "Analysing..."; break;
        case RetuneProcessor::Rendering: s = "Rendering..."; break;
        case RetuneProcessor::Ready:     s = "Ready - edits play back with the transport"; break;
        default:                         s = "Empty"; break;
    }
    statusLabel.setText (s, juce::dontSendNotification);
}

void RetuneEditor::paint (juce::Graphics& g) { g.fillAll (kPanel); }

void RetuneEditor::resized()
{
    auto b = getLocalBounds();
    auto bar = b.removeFromTop (44).reduced (8, 8);
    recBtn.setBounds   (bar.removeFromLeft (90));   bar.removeFromLeft (14);
    editBtn.setBounds  (bar.removeFromLeft (60));
    splitBtn.setBounds (bar.removeFromLeft (60));   bar.removeFromLeft (14);
    undoBtn.setBounds  (bar.removeFromLeft (60));   bar.removeFromLeft (14);
    keyBox.setBounds   (bar.removeFromLeft (64));   bar.removeFromLeft (6);
    scaleBox.setBounds (bar.removeFromLeft (140));  bar.removeFromLeft (6);
    snapBtn.setBounds  (bar.removeFromLeft (110));  bar.removeFromLeft (14);
    clearBtn.setBounds (bar.removeFromLeft (60));
    statusLabel.setBounds (bar);
    noteEditor.setBounds (b);
}
