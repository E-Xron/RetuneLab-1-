#include "PluginEditor.h"
#include <cmath>

namespace
{
    inline bool isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

    juce::String midiName (float m)
    {
        static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        const int n = juce::jlimit (0, 127, (int) std::lround (m));
        return juce::String (names[n % 12]) + juce::String (n / 12 - 2);
    }

    // The pitch a snap should start from: what was actually sung, unless the user moved it far on purpose.
    inline void resetNote (Note& n)
    {
        n.outStart = n.srcStart; n.outLen = n.srcLen(); n.targetMid = n.detectedMid; n.muted = false;
        n.glideMs = 0.f; n.glideSmooth = 0.5f; n.drift = 1.f; n.vibrato = 1.f;
    }

    inline float snapBase (const Note& n)
    {
        return std::abs (n.targetMid - n.detectedMid) > 0.5f ? n.targetMid : n.detectedMid;
    }
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
        if (proc.keyPending.exchange (false))                 // fresh recording: work out the key from what was sung
        {
            const auto nv = proc.getNotes();
            if (! nv.empty())
            {
                int root = 0; bool minor = false;
                ClipEngine::detectKey (nv, root, minor);
                proc.keyRoot = root;
                proc.keyScale = minor ? 1 : 0;
                proc.noteMask = scaleMaskFor (root, minor ? 1 : 0);
            }
        }
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

juce::Rectangle<float> NoteEditor::noteRect (const Note& n) const
{
    const float x0 = xFromSample (n.outStart), x1 = xFromSample (n.outEnd());
    const float h = (float) rowH * 0.78f, yc = yFromMidi (n.targetMid);
    return { x0, yc - h * 0.5f, std::max (2.f, x1 - x0), h };
}

int NoteEditor::hitNote (juce::Point<float> p, const std::vector<Note>& v, Zone& z) const
{
    for (int i = (int) v.size() - 1; i >= 0; --i)
    {
        const auto r = noteRect (v[(size_t) i]);
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
    using namespace Theme;
    g.fillAll (bg);
    const auto roll = rollArea();
    const int scaleMask = proc.noteMask.load();

    // ---- pitch rows + keyboard
    const int topN = (int) std::ceil (topMidi + 0.5), botN = (int) std::floor (midiFromY ((float) getHeight()) - 0.5f);
    for (int n = topN; n >= botN; --n)
    {
        if (n < 0 || n > 127) continue;
        const int pc = n % 12;
        const float y0 = std::floor (yFromMidi ((float) n + 0.5f));
        const bool inScale = ((scaleMask >> pc) & 1) != 0, blk = isBlack (pc);

        juce::Colour c = blk ? juce::Colour (0xff0f1116) : juce::Colour (0xff141720);
        if (inScale) c = c.interpolatedWith (accent, blk ? 0.07f : 0.11f);
        g.setColour (c);
        g.fillRect (roll.getX(), y0, roll.getWidth(), (float) rowH);
        g.setColour (juce::Colours::white.withAlpha (pc == 0 ? 0.14f : 0.04f));
        g.fillRect (roll.getX(), y0 + (float) rowH - 1.f, roll.getWidth(), 1.f);

        juce::Rectangle<float> k (0.f, y0, kKeyW, (float) rowH);
        juce::Colour kc = blk ? juce::Colour (0xff1b1f27) : juce::Colour (0xffdfe3ea);
        if (inScale) kc = kc.interpolatedWith (accent, blk ? 0.5f : 0.42f);      // active notes light up on the keyboard
        g.setColour (kc);
        g.fillRect (k.withTrimmedBottom (1.f));
        if (inScale)
        {
            g.setColour (juce::Colours::white);
            g.fillEllipse (k.getRight() - 11.f, k.getCentreY() - 2.5f, 5.f, 5.f);
        }
        if (pc == 0)
        {
            g.setColour (juce::Colour (0xff20242c));
            g.setFont (juce::FontOptions (11.f, juce::Font::bold));
            g.drawText ("C" + juce::String (n / 12 - 2), k.withTrimmedLeft (7.f), juce::Justification::centredLeft);
        }
    }
    g.setColour (border);
    g.fillRect (kKeyW - 1.f, 0.f, 1.f, (float) getHeight());

    // ---- time ruler + grid
    const double visibleSec = roll.getWidth() / pps;
    const double step = pps >= 40.0 ? 1.0 : (pps >= 12.0 ? 5.0 : 15.0);
    for (double t = std::ceil (scrollSec / step) * step; t < scrollSec + visibleSec; t += step)
    {
        const float x = std::floor (kKeyW + (float) ((t - scrollSec) * pps));
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect (x, kRuler, 1.f, (float) getHeight());
    }
    g.setColour (bg.withAlpha (0.88f));
    g.fillRect (roll.getX(), 0.f, roll.getWidth(), kRuler);
    g.setFont (juce::FontOptions (10.f));
    for (double t = std::ceil (scrollSec / step) * step; t < scrollSec + visibleSec; t += step)
    {
        const float x = std::floor (kKeyW + (float) ((t - scrollSec) * pps));
        g.setColour (dim);
        g.drawText (juce::String (t, 0) + "s", (int) x + 4, 2, 40, 12, juce::Justification::left);
        g.fillRect (x, kRuler - 4.f, 1.f, 4.f);
    }

    // ---- notes
    const auto v = proc.getNotes();
    const auto slow = ClipEngine::slowContour (analysisCache, v, sr());
    const RenderOptions ro = proc.renderOptions();
    for (int ni = 0; ni < (int) v.size(); ++ni)
    {
        const auto& n = v[(size_t) ni];
        const auto r = noteRect (n);
        if (r.getRight() < kKeyW || r.getX() > (float) getWidth()) continue;

        const int prev = ClipEngine::glideFrom (v, ni, sr());
        const bool edited = ClipEngine::isEdited (v, ni, prev, ro);
        if (edited && ! n.muted)
        {
            const float gx0 = xFromSample (n.srcStart), gx1 = xFromSample (n.srcEnd), gy = yFromMidi (n.detectedMid);
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.drawRoundedRectangle (gx0, gy - r.getHeight() * 0.5f, std::max (2.f, gx1 - gx0), r.getHeight(), 4.f, 1.f);
        }

        if (n.muted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (r, 5.f);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.drawRoundedRectangle (r, 5.f, 1.f);
            continue;
        }

        const juce::Colour c1 = n.selected ? warm : accent;
        if (n.selected)
        {
            g.setColour (warm.withAlpha (0.22f));
            g.fillRoundedRectangle (r.expanded (3.f), 7.f);
        }
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (0.f, 1.5f), 5.f);
        g.setGradientFill (juce::ColourGradient (c1.brighter (0.25f), r.getX(), r.getY(),
                                                 c1.darker (0.22f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.f);
        g.setColour (juce::Colours::white.withAlpha (n.selected ? 0.9f : 0.28f));
        g.drawRoundedRectangle (r, 5.f, n.selected ? 1.5f : 1.f);

        if (r.getWidth() > 14.f)                                  // stretch grips
        {
            g.setColour (juce::Colours::white.withAlpha (0.32f));
            g.fillRoundedRectangle (r.getX() + 2.5f, r.getY() + 3.f, 2.f, r.getHeight() - 6.f, 1.f);
            g.fillRoundedRectangle (r.getRight() - 4.5f, r.getY() + 3.f, 2.f, r.getHeight() - 6.f, 1.f);
        }

        if (! analysisCache.contour.empty() && n.srcLen() > 0 && n.outLen > 0)   // pitch curve as it will play
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
                const double sh = ClipEngine::shiftAt (v, ni, prev, analysisCache, slow, outS, sr(), ro);
                const float x = xFromSample (outS), y = yFromMidi ((float) (c + sh));
                if (! pen) { path.startNewSubPath (x, y); pen = true; } else path.lineTo (x, y);
            }
            g.setColour (juce::Colours::white.withAlpha (0.78f));
            g.strokePath (path, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        if (r.getWidth() > 46.f && rowH >= 13.0)                    // note name
        {
            g.setColour (juce::Colours::white.withAlpha (0.92f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (midiName (n.targetMid), r.withTrimmedLeft (8.f), juce::Justification::topLeft);
        }
    }

    if (marquee)
    {
        g.setColour (accent.withAlpha (0.14f));
        g.fillRect (marqueeRect);
        g.setColour (accent.withAlpha (0.85f));
        g.drawRect (marqueeRect, 1.f);
    }

    if (tool == Tool::Split && hoverX >= kKeyW)
    {
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.fillRect (std::floor (hoverX), kRuler, 1.f, (float) getHeight());
    }

    // ---- playhead
    if (proc.clipLen.load() > 0)
    {
        const double s = (double) (proc.hostPos.load() - proc.clipOrigin.load());
        const float x = std::floor (xFromSample (s));
        if (x >= kKeyW && x <= (float) getWidth())
        {
            g.setColour (red);
            g.fillRect (x, 0.f, 1.f, (float) getHeight());
            juce::Path tri;
            tri.addTriangle (x - 5.f, 0.f, x + 6.f, 0.f, x + 0.5f, 8.f);
            g.fillPath (tri);
        }
    }

    if (v.empty())
    {
        g.setColour (dim);
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
    if (e.position.x < kKeyW) { setMouseCursor (juce::MouseCursor::PointingHandCursor); return; }
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
    if (e.position.x < kKeyW && ! e.mods.isPopupMenu())      // keyboard strip: switch pitch classes on / off
    {
        const int pc = pitchClassAtY (e.position.y);
        const int m = proc.noteMask.load();
        keyPaintState = ((m >> pc) & 1) == 0;
        keyDrag = true;
        proc.noteMask = keyPaintState ? (m | (1 << pc)) : (m & ~(1 << pc));
        repaint();
        return;
    }
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
    marquee = false;
    if (idx < 0)                                         // empty space: start a selection rectangle
    {
        marquee = true;
        marqueeStart = e.position;
        marqueeRect = juce::Rectangle<float> (e.position, e.position);
        marqueeBase.assign (v.size(), false);
        if (e.mods.isCommandDown() || e.mods.isShiftDown())
            for (size_t i = 0; i < v.size(); ++i) marqueeBase[i] = v[i].selected;
        else
        {
            for (auto& n : v) n.selected = false;
            proc.setNotes (v);
        }
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
    if (keyDrag)
    {
        const int pc = pitchClassAtY (e.position.y);
        const int m = proc.noteMask.load();
        proc.noteMask = keyPaintState ? (m | (1 << pc)) : (m & ~(1 << pc));
        repaint();
        return;
    }
    if (marquee)
    {
        marqueeRect = juce::Rectangle<float> (marqueeStart, e.position);
        auto v = proc.getNotes();
        for (size_t i = 0; i < v.size(); ++i)
        {
            const bool base = i < marqueeBase.size() && marqueeBase[i];
            v[i].selected = base || noteRect (v[i]).intersects (marqueeRect);
        }
        proc.setNotes (v);
        repaint();
        return;
    }

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
    if (keyDrag) { keyDrag = false; return; }
    if (marquee) { marquee = false; repaint(); return; }
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
    const Act a = proc.hotkeys.match (k);
    if (a != Act::None) { perform (a); return true; }
    if (k == juce::KeyPress::backspaceKey) { deleteSelected(); return true; }   // always works as an alias
    return false;
}

void NoteEditor::perform (Act a)
{
    switch (a)
    {
        case Act::Undo:            undo(); break;
        case Act::SelectAll:       selectAll(); break;
        case Act::Delete:          deleteSelected(); break;
        case Act::SnapScale:       snapToScale(); break;
        case Act::SnapSemitone:    snapSemitone(); break;
        case Act::Merge:           mergeSelected(); break;
        case Act::Reset:           resetSelected(); break;
        case Act::SplitAtPlayhead: splitAtPlayhead(); break;
        case Act::FitView:         fitView(); break;
        default: if (onExtraAction) onExtraAction (a); break;
    }
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
    const int scaleMask = proc.noteMask.load();
    if (scaleMask == 0) return t;
    float best = t, bestD = 1e9f;
    const int c = (int) std::round (t);
    for (int n = c - 7; n <= c + 7; ++n)
    {
        if (! ((scaleMask >> (((n % 12) + 12) % 12)) & 1)) continue;
        const float d = std::abs ((float) n - t);
        if (d < bestD - 1e-4f) { bestD = d; best = (float) n; }   // on an exact tie the lower note wins
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
        case 3: for (auto& n : v) if (n.selected) n.targetMid = std::round (snapBase (n)); break;
        case 4: for (auto& n : v) if (n.selected) n.targetMid = nearestInScale (snapBase (n)); break;
        case 5: for (auto& n : v) if (n.selected) resetNote (n); break;
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
    for (auto& n : v) if (! any || n.selected) n.targetMid = nearestInScale (snapBase (n));
    commit (v);
}

void NoteEditor::snapSemitone()
{
    auto v = proc.getNotes();
    if (v.empty()) return;
    pushUndo (v);
    bool any = false;
    for (auto& n : v) any = any || n.selected;
    for (auto& n : v) if (! any || n.selected) n.targetMid = std::round (snapBase (n));
    commit (v);
}

void NoteEditor::snapAll (bool toScale)
{
    auto v = proc.getNotes();
    if (v.empty()) return;
    pushUndo (v);
    for (auto& n : v) n.targetMid = toScale ? nearestInScale (snapBase (n)) : std::round (snapBase (n));
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
    bool any = false, anyLive = false;
    for (auto& n : v) if (n.selected) { any = true; anyLive = anyLive || ! n.muted; }
    for (auto& n : v) if (n.selected) n.muted = anyLive;      // delete, or restore if everything selected is already deleted
    if (any) commit (v); else undoStack.pop_back();
}

void NoteEditor::mergeSelected()
{
    auto v = proc.getNotes();
    int idx = -1;
    for (int i = 0; i < (int) v.size(); ++i) if (v[(size_t) i].selected) { idx = i; break; }
    if (idx < 0) return;
    pushUndo (v);
    if (mergeNext (v, idx)) commit (v); else undoStack.pop_back();
}

void NoteEditor::resetSelected()
{
    auto v = proc.getNotes();
    if (v.empty()) return;
    pushUndo (v);
    bool any = false;
    for (auto& n : v) any = any || n.selected;
    for (auto& n : v)
        if (! any || n.selected) resetNote (n);
    commit (v);
}

void NoteEditor::splitAtPlayhead()
{
    auto v = proc.getNotes();
    const int pos = (int) (proc.hostPos.load() - proc.clipOrigin.load());
    bool any = false;
    for (auto& n : v) any = any || n.selected;
    for (int i = 0; i < (int) v.size(); ++i)
    {
        if (any && ! v[(size_t) i].selected) continue;
        if (pos > v[(size_t) i].outStart && pos < v[(size_t) i].outEnd())
        {
            pushUndo (v);
            if (splitAt (v, i, pos)) commit (v); else undoStack.pop_back();
            return;
        }
    }
}

void NoteEditor::beginShapeEdit()
{
    pushUndo (proc.getNotes());
}

void NoteEditor::setShape (int field, float value)
{
    auto v = proc.getNotes();
    bool any = false;
    for (auto& n : v) any = any || n.selected;
    for (auto& n : v)
    {
        if (any && ! n.selected) continue;
        if (field == 0) n.glideMs = value;
        else if (field == 1) n.glideSmooth = value;
        else if (field == 2) n.drift = value;
        else n.vibrato = value;
    }
    proc.setNotes (std::move (v));
    repaint();
}

void NoteEditor::endShapeEdit()
{
    proc.requestRender();
}

float NoteEditor::shapeValue (int field, int& targets) const
{
    const auto v = proc.getNotes();
    bool any = false;
    for (auto& n : v) any = any || n.selected;
    targets = 0;
    float val = field == 0 ? 0.f : (field == 1 ? 0.5f : 1.f);
    for (auto& n : v)
    {
        if (any && ! n.selected) continue;
        if (targets++ == 0) val = field == 0 ? n.glideMs : (field == 1 ? n.glideSmooth : (field == 2 ? n.drift : n.vibrato));
    }
    return val;
}

void NoteEditor::undo()
{
    if (undoStack.empty()) { proc.undoClear(); return; }      // nothing to step back: bring back a cleared clip
    commit (undoStack.back());
    undoStack.pop_back();
}

// ===================================================================== Effects window
namespace
{
    enum class Fmt { Pct, Db, Hz, Ms, Ratio, Cents, Pct200 };

    struct Knob
    {
        const char* id;            // parameter id, or nullptr for a per-note field
        const char* label;
        Fmt fmt;
        int field = -1;            // per-note field (see NoteEditor::setShape)
        double lo = 0.0, hi = 1.0, def = 0.0;
    };

    struct Page
    {
        const char* name;
        const char* onId;
        const char* desc;
        std::vector<Knob> knobs;
        const char* choiceId = nullptr;
        const char* choiceLabel = nullptr;
    };

    const std::vector<Page>& pages()
    {
        static const std::vector<Page> p {
            { "Note Glide", "glideOn",
              "Portamento into each note from the one before it. Applies to the selected notes (all notes if none are selected).",
              { { nullptr, "Glide Time", Fmt::Ms, 0, 0.0, 500.0, 0.0 }, { nullptr, "Smoothness", Fmt::Pct, 1, 0.0, 1.0, 0.5 } } },
            { "Drift & Vibrato", "shapeOn",
              "Scale the slow pitch drift and the vibrato as sung: 0% flattens it, 100% leaves it, 200% exaggerates it. Applies to the selected notes (all if none).",
              { { nullptr, "Drift", Fmt::Pct200, 2, 0.0, 2.0, 1.0 }, { nullptr, "Vibrato", Fmt::Pct200, 3, 0.0, 2.0, 1.0 } } },
            { "De-Esser", "deessOn",
              "Turns down harsh S, T and SH sounds without dulling the rest of the voice.",
              { { "deessThresh", "Threshold", Fmt::Db }, { "deessFreq", "Frequency", Fmt::Hz }, { "deessRange", "Max Reduction", Fmt::Db } } },
            { "Compressor", "compOn",
              "Smooth, natural-sounding vocal levelling with automatic make-up gain.",
              { { "compThresh", "Threshold", Fmt::Db }, { "compRatio", "Ratio", Fmt::Ratio }, { "compAttack", "Attack", Fmt::Ms },
                { "compRelease", "Release", Fmt::Ms }, { "compMix", "Mix", Fmt::Pct } } },
            { "Air", "airOn",
              "Adds high-frequency sheen and presence.",
              { { "midAir", "Mid Air", Fmt::Pct }, { "highAir", "High Air", Fmt::Pct } } },
            { "Choir", "choirOn",
              "Turns the vocal into a choir: bass an octave down, alto at pitch, soprano an octave up. The shares set how many singers each section gets.",
              { { "choirSize", "Size", Fmt::Pct }, { "choirBass", "Bass Share", Fmt::Pct }, { "choirAlto", "Alto Share", Fmt::Pct },
                { "choirSop", "Soprano Share", Fmt::Pct }, { "choirMix", "Mix", Fmt::Pct }, { "choirBassVol", "Bass Volume", Fmt::Db },
                { "choirAltoVol", "Alto Volume", Fmt::Db }, { "choirSopVol", "Soprano Volume", Fmt::Db } } },
            { "Doubler", "doubleOn",
              "Double-tracked vocal: two detuned, slightly late copies spread left and right.",
              { { "doubleWidth", "Width", Fmt::Pct }, { "doubleDetune", "Detune", Fmt::Cents }, { "doubleMix", "Mix", Fmt::Pct } } },
            { "Echo", "echoOn",
              "Tempo-synced echo with a darker repeat on each pass.",
              { { "echoFeedback", "Feedback", Fmt::Pct }, { "echoTone", "Tone", Fmt::Hz }, { "echoMix", "Mix", Fmt::Pct } },
              "echoTime", "Time" },
            { "Reverb", "verbOn",
              "Room / plate style reverb to place the vocal in a space.",
              { { "verbSize", "Size", Fmt::Pct }, { "verbDamp", "Damping", Fmt::Pct }, { "verbWidth", "Width", Fmt::Pct },
                { "verbMix", "Mix", Fmt::Pct } } },
        };
        return p;
    }

    juce::String formatValue (Fmt f, double v)
    {
        switch (f)
        {
            case Fmt::Pct:    return juce::String ((int) std::lround (v * 100.0)) + "%";
            case Fmt::Pct200: return juce::String ((int) std::lround (v * 100.0)) + "%";
            case Fmt::Db:     return juce::String (v, 1) + " dB";
            case Fmt::Hz:     return v >= 1000.0 ? juce::String (v / 1000.0, 1) + " kHz" : juce::String ((int) std::lround (v)) + " Hz";
            case Fmt::Ms:     return juce::String (v, v < 10.0 ? 1 : 0) + " ms";
            case Fmt::Ratio:  return juce::String (v, 1) + ":1";
            case Fmt::Cents:  return juce::String (v, 1) + " ct";
        }
        return juce::String (v);
    }

    double parseValue (Fmt f, const juce::String& t)
    {
        const double v = t.retainCharacters ("0123456789.-").getDoubleValue();
        if (f == Fmt::Pct || f == Fmt::Pct200) return v / 100.0;
        if (f == Fmt::Hz && t.containsIgnoreCase ("k")) return v * 1000.0;
        return v;
    }
}

int FxPanel::numPages() { return (int) pages().size(); }
juce::String FxPanel::pageName (int i) { return pages()[(size_t) i].name; }
juce::String FxPanel::pageOnParam (int i) { return pages()[(size_t) i].onId; }

FxPanel::FxPanel (RetuneProcessor& p, NoteEditor& ed) : proc (p), editor (ed)
{
    for (int i = 0; i < numPages(); ++i) pageBox.addItem (pageName (i), i + 1);
    pageBox.onChange = [this] { showPage (pageBox.getSelectedItemIndex()); };
    addAndMakeVisible (pageBox);
    addAndMakeVisible (onBtn);
    info.setColour (juce::Label::textColourId, Theme::dim);
    info.setFont (juce::FontOptions (12.f));
    info.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (info);
    setSize (600, 380);
    showPage (0);
    startTimerHz (12);
}

FxPanel::~FxPanel()
{
    onAtt.reset();
    knobAtts.clear();
    choiceAtt.reset();
}

void FxPanel::showPage (int pg)
{
    pg = juce::jlimit (0, numPages() - 1, pg);
    if (pg == page && ! knobs.isEmpty()) return;
    page = pg;
    pageBox.setSelectedItemIndex (page, juce::dontSendNotification);
    buildPage();
}

void FxPanel::buildPage()
{
    const auto& pd = pages()[(size_t) page];
    onAtt.reset();
    knobAtts.clear();
    knobs.clear();
    knobLabels.clear();
    choiceAtt.reset(); choice.reset(); choiceLabel.reset();
    shapeField.clear();

    onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, pd.onId, onBtn);
    info.setText (pd.desc, juce::dontSendNotification);

    for (const auto& k : pd.knobs)
    {
        auto* s = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow));
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        auto* l = knobLabels.add (new juce::Label ({}, k.label));
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
        addAndMakeVisible (s);
        addAndMakeVisible (l);
        shapeField.push_back (k.id == nullptr ? k.field : -1);

        if (k.id != nullptr)
        {
            knobAtts.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, k.id, *s));
            if (auto* prm = proc.apvts.getParameter (k.id))
                s->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        }
        else
        {
            const int field = k.field;
            s->setRange (k.lo, k.hi, 0.0);
            s->setDoubleClickReturnValue (true, k.def);
            s->onDragStart = [this] { shapeDragging = true; editor.beginShapeEdit(); };
            s->onDragEnd   = [this] { shapeDragging = false; editor.endShapeEdit(); };
            s->onValueChange = [this, s, field]
            {
                if (shapeDragging) { editor.setShape (field, (float) s->getValue()); return; }
                editor.beginShapeEdit();                    // typed value or double-click reset
                editor.setShape (field, (float) s->getValue());
                editor.endShapeEdit();
            };
        }
        const Fmt f = k.fmt;
        s->textFromValueFunction = [f] (double v) { return formatValue (f, v); };
        s->valueFromTextFunction = [f] (const juce::String& t) { return parseValue (f, t); };
        s->updateText();
    }

    if (pd.choiceId != nullptr)
    {
        choice = std::make_unique<juce::ComboBox>();
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (pd.choiceId)))
            choice->addItemList (c->choices, 1);
        choiceAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, pd.choiceId, *choice);
        choiceLabel = std::make_unique<juce::Label> (juce::String(), pd.choiceLabel);
        choiceLabel->setJustificationType (juce::Justification::centred);
        choiceLabel->setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
        addAndMakeVisible (*choice);
        addAndMakeVisible (*choiceLabel);
    }
    timerCallback();
    resized();
    repaint();
}

void FxPanel::timerCallback()
{
    if (shapeDragging) return;
    int targets = 0;
    for (int i = 0; i < knobs.size(); ++i)
        if (shapeField[(size_t) i] >= 0)
            knobs[i]->setValue (editor.shapeValue (shapeField[(size_t) i], targets), juce::dontSendNotification);
    repaint (0, getHeight() - 30, getWidth(), 30);
}

void FxPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::border);
    g.fillRect (0, 52, getWidth(), 1);

    juce::String foot;
    if (page == 0 || page == 1)
    {
        int targets = 0;
        editor.shapeValue (0, targets);
        bool any = false;
        for (const auto& n : proc.getNotes()) any = any || n.selected;
        foot = targets == 0 ? juce::String ("No notes yet")
                            : (any ? "Editing " + juce::String (targets) + " selected note" + (targets == 1 ? "" : "s")
                                   : "Editing all " + juce::String (targets) + " notes");
    }
    else if (page == 2) foot = "Reduction: " + juce::String (proc.deessReduction(), 1) + " dB";
    else if (page == 3) foot = "Gain reduction: " + juce::String (proc.compReduction(), 1) + " dB";
    g.setColour (Theme::dim);
    g.setFont (juce::FontOptions (12.f));
    g.drawText (foot, 20, getHeight() - 26, getWidth() - 40, 18, juce::Justification::centredLeft);
}

void FxPanel::resized()
{
    pageBox.setBounds (16, 13, 200, 28);
    onBtn.setBounds (230, 15, 70, 24);
    info.setBounds (16, 58, getWidth() - 32, 36);

    const int n = knobs.size() + (choice != nullptr ? 1 : 0);
    const int cols = juce::jmin (5, juce::jmax (1, n)), rows = (n + cols - 1) / juce::jmax (1, cols);
    const int cw = 108, ch = 124, top = 100;
    const int x0 = (getWidth() - cols * cw) / 2;
    for (int i = 0; i < n; ++i)
    {
        const int r = i / cols, c = i % cols;
        juce::Rectangle<int> cell (x0 + c * cw, top + r * ch, cw, ch);
        auto lab = cell.removeFromTop (18);
        if (i < knobs.size())
        {
            knobLabels[i]->setBounds (lab);
            knobs[i]->setBounds (cell.reduced (8, 0));
        }
        else
        {
            choiceLabel->setBounds (lab);
            choice->setBounds (cell.withSizeKeepingCentre (cw - 12, 26));
        }
    }
    juce::ignoreUnused (rows);
}

// ===================================================================== Hotkey window
HotkeyPanel::HotkeyPanel (RetuneProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    for (int i = 0; i < HotkeyMap::N; ++i)
    {
        auto& l = labels[(size_t) i];
        auto& b = btns[(size_t) i];
        l.setText (HotkeyMap::label (i), juce::dontSendNotification);
        addAndMakeVisible (l);
        b.setWantsKeyboardFocus (false);
        b.onClick = [this, i]
        {
            capturing = i;
            refresh();
            grabKeyboardFocus();
        };
        addAndMakeVisible (b);
    }
    resetBtn.setWantsKeyboardFocus (false);
    resetBtn.onClick = [this] { proc.hotkeys.resetDefaults(); capturing = -1; refresh(); };
    addAndMakeVisible (resetBtn);
    refresh();
    setSize (440, 540);
}

void HotkeyPanel::refresh()
{
    for (int i = 0; i < HotkeyMap::N; ++i)
        btns[(size_t) i].setButtonText (i == capturing ? juce::String ("Press a key...") : proc.hotkeys.describe (i));
}

bool HotkeyPanel::keyPressed (const juce::KeyPress& k)
{
    if (capturing < 0) return false;
    if (k == juce::KeyPress::escapeKey) { capturing = -1; refresh(); return true; }
    proc.hotkeys.set (capturing, k);
    capturing = -1;
    refresh();
    return true;
}

void HotkeyPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (20.f, juce::Font::bold));
    g.drawText ("Hotkeys", 20, 14, 200, 26, juce::Justification::centredLeft);
    g.setColour (Theme::dim);
    g.setFont (juce::FontOptions (12.f));
    g.drawText ("Click a shortcut, then press the new key combination. Esc cancels.", 20, 40, 400, 16, juce::Justification::centredLeft);
}

void HotkeyPanel::resized()
{
    int y = 72;
    for (int i = 0; i < HotkeyMap::N; ++i, y += 30)
    {
        labels[(size_t) i].setBounds (20, y, 220, 26);
        btns[(size_t) i].setBounds (250, y, 170, 26);
    }
    resetBtn.setBounds (getWidth() - 160, getHeight() - 44, 140, 28);
}

// ===================================================================== RetuneEditor

RetuneEditor::RetuneEditor (RetuneProcessor& p)
    : AudioProcessorEditor (&p), proc (p), noteEditor (p)
{
    setLookAndFeel (&laf);
    setWantsKeyboardFocus (true);
    addAndMakeVisible (noteEditor);

    recBtn.setClickingTogglesState (true);
    recBtn.setColour (juce::TextButton::buttonOnColourId, Theme::red);
    recBtn.onClick = [this] { proc.setRecording (recBtn.getToggleState()); };

    editBtn.setClickingTogglesState (true);  editBtn.setRadioGroupId (1);  editBtn.setToggleState (true, juce::dontSendNotification);
    splitBtn.setClickingTogglesState (true); splitBtn.setRadioGroupId (1);
    editBtn.onClick  = [this] { noteEditor.setTool (NoteEditor::Tool::Edit); };
    splitBtn.onClick = [this] { noteEditor.setTool (NoteEditor::Tool::Split); };
    undoBtn.onClick  = [this] { noteEditor.undo(); };
    snapBtn.onClick  = [this] { noteEditor.snapAll (true); };
    snapSemiBtn.onClick = [this] { noteEditor.snapAll (false); };
    clearBtn.onClick = [this] { proc.clearClip(); };
    fxBtn.onClick    = [this] { showEffectsMenu(); };
    keysBtn.onClick  = [this] { showHotkeys(); };

    keyBox.addItemList ({ "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 1);
    scaleBox.addItemList (scaleNames(), 1);
    keyBox.setSelectedItemIndex (proc.keyRoot.load(), juce::dontSendNotification);
    scaleBox.setSelectedItemIndex (proc.keyScale.load(), juce::dontSendNotification);
    keyBox.onChange = scaleBox.onChange = [this] { updateScale(); };

    for (auto* c : std::initializer_list<juce::Component*> { &recBtn, &editBtn, &splitBtn, &undoBtn, &snapBtn, &snapSemiBtn, &clearBtn,
                                                              &fxBtn, &keysBtn, &keyBox, &scaleBox, &statusLabel })
    {
        c->setWantsKeyboardFocus (false);           // keep keyboard shortcuts flowing to the note editor
        addAndMakeVisible (c);
    }
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setColour (juce::Label::textColourId, Theme::dim);

    noteEditor.onExtraAction = [this] (Act a)
    {
        if (a == Act::Record)    toggleRecord();
        if (a == Act::EditTool)  selectTool (false);
        if (a == Act::SplitTool) selectTool (true);
        if (a == Act::Clear)     proc.clearClip();
    };

    setResizable (true, true);
    setResizeLimits (1060, 480, 1900, 1100);
    setSize (1180, 660);
    startTimerHz (10);
}

RetuneEditor::~RetuneEditor()
{
    fxWin.reset();
    keyWin.reset();
    setLookAndFeel (nullptr);
}

void RetuneEditor::toggleRecord()
{
    recBtn.setToggleState (! recBtn.getToggleState(), juce::dontSendNotification);
    proc.setRecording (recBtn.getToggleState());
}

void RetuneEditor::selectTool (bool split)
{
    (split ? splitBtn : editBtn).setToggleState (true, juce::dontSendNotification);
    noteEditor.setTool (split ? NoteEditor::Tool::Split : NoteEditor::Tool::Edit);
}

void RetuneEditor::showEffectsMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Notes");
    for (int i = 0; i < FxPanel::numPages(); ++i)
    {
        if (i == 2) m.addSectionHeader ("Vocal effects");
        auto* prm = proc.apvts.getParameter (FxPanel::pageOnParam (i));
        const bool on = prm != nullptr && prm->getValue() > 0.5f;
        m.addItem (juce::PopupMenu::Item (FxPanel::pageName (i)).setID (i + 1).setTicked (on));
    }
    m.addSeparator();
    m.addItem (100, "Turn all vocal effects off");

    juce::Component::SafePointer<RetuneEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fxBtn), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 100)
        {
            for (int i = 2; i < FxPanel::numPages(); ++i)
                if (auto* prm = safe->proc.apvts.getParameter (FxPanel::pageOnParam (i)))
                    prm->setValueNotifyingHost (0.f);
            return;
        }
        safe->showEffects (r - 1);
    });
}

void RetuneEditor::showEffects (int pg)
{
    if (fxWin == nullptr)
    {
        fxWin = std::make_unique<ToolWindow> ("Effects");
        fxWin->setLookAndFeel (&laf);
        fxPanel = new FxPanel (proc, noteEditor);
        fxWin->setContentOwned (fxPanel, true);
        fxWin->setUsingNativeTitleBar (true);
        fxWin->setResizable (false, false);
        fxWin->setAlwaysOnTop (true);
        fxWin->centreAroundComponent (this, fxWin->getWidth(), fxWin->getHeight());
    }
    fxPanel->showPage (pg);
    fxWin->setVisible (true);
    fxWin->toFront (true);
}

void RetuneEditor::showHotkeys()
{
    if (keyWin == nullptr)
    {
        keyWin = std::make_unique<ToolWindow> ("Hotkeys");
        keyWin->setLookAndFeel (&laf);
        keyWin->setContentOwned (new HotkeyPanel (proc), true);
        keyWin->setUsingNativeTitleBar (true);
        keyWin->setResizable (false, false);
        keyWin->setAlwaysOnTop (true);
        keyWin->centreAroundComponent (this, keyWin->getWidth(), keyWin->getHeight());
    }
    keyWin->setVisible (true);
    keyWin->toFront (true);
}

void RetuneEditor::updateScale()
{
    const int key = keyBox.getSelectedItemIndex();
    const int sc = juce::jlimit (0, scaleNames().size() - 1, scaleBox.getSelectedItemIndex());
    proc.keyRoot = key;
    proc.keyScale = sc;
    proc.noteMask = scaleMaskFor (key, sc);        // changing the key or scale resets the active notes to exactly that scale
}

void RetuneEditor::timerCallback()
{
    if (keyBox.getSelectedItemIndex() != proc.keyRoot.load())
        keyBox.setSelectedItemIndex (proc.keyRoot.load(), juce::dontSendNotification);      // follows auto-detected key
    if (scaleBox.getSelectedItemIndex() != proc.keyScale.load())
        scaleBox.setSelectedItemIndex (proc.keyScale.load(), juce::dontSendNotification);
    recBtn.setToggleState (proc.recordArmed.load(), juce::dontSendNotification);
    juce::String s;
    if (proc.recordArmed.load())
        s = proc.hostPlaying.load() ? "Recording..." : "Armed - press play in Ableton";
    else switch (proc.status.load())
    {
        case RetuneProcessor::Analysing: s = "Analysing..."; break;
        case RetuneProcessor::Rendering: s = "Rendering..."; break;
        case RetuneProcessor::Ready:     s = "Ready"; break;
        default:                         s = "Empty"; break;
    }
    statusLabel.setText (s, juce::dontSendNotification);
}

void RetuneEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::border);
    g.fillRect (0, 47, getWidth(), 1);
}

void RetuneEditor::resized()
{
    auto b = getLocalBounds();
    auto bar = b.removeFromTop (48).reduced (10, 9);
    const int gap = 8;
    recBtn.setBounds   (bar.removeFromLeft (92));   bar.removeFromLeft (gap + 6);
    editBtn.setBounds  (bar.removeFromLeft (58));   bar.removeFromLeft (4);
    splitBtn.setBounds (bar.removeFromLeft (58));   bar.removeFromLeft (gap);
    undoBtn.setBounds  (bar.removeFromLeft (58));   bar.removeFromLeft (gap + 6);
    keyBox.setBounds   (bar.removeFromLeft (64));   bar.removeFromLeft (gap);
    scaleBox.setBounds (bar.removeFromLeft (140));  bar.removeFromLeft (gap);
    snapBtn.setBounds  (bar.removeFromLeft (118));  bar.removeFromLeft (4);
    snapSemiBtn.setBounds (bar.removeFromLeft (138)); bar.removeFromLeft (gap + 6);
    fxBtn.setBounds    (bar.removeFromLeft (76));   bar.removeFromLeft (gap);
    keysBtn.setBounds  (bar.removeFromLeft (78));   bar.removeFromLeft (gap);
    clearBtn.setBounds (bar.removeFromLeft (58));
    statusLabel.setBounds (bar);
    noteEditor.setBounds (b.withTrimmedTop (1));
}
