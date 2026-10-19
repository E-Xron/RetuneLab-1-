#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <limits>

// One detected note. Source fields never change; output fields are what the user edits.
struct Note
{
    int   srcStart = 0, srcEnd = 0;     // region of the recorded clip (samples)
    int   outStart = 0, outLen = 0;     // where/how long it plays (samples from clip origin)
    float detectedMid = 60.f;           // median MIDI pitch of the source
    float targetMid   = 60.f;           // MIDI pitch it should play at
    bool  muted    = false;
    bool  selected = false;             // UI only
    float glideMs     = 0.f;            // portamento into this note from the one before it (0 = jump)
    float glideSmooth = 0.5f;           // 0 = straight line, 1 = S-curve
    float drift   = 1.f;                // scale of the slow pitch wander around the note centre (1 = as sung)
    float vibrato = 1.f;                // scale of the fast pitch wobble (1 = as sung)

    int srcLen() const { return srcEnd - srcStart; }
    int outEnd() const { return outStart + outLen; }
};

struct ClipAnalysis
{
    int hop = 256;
    int frameOffset = 384;              // sample offset of a frame's pitch estimate from frame*hop
    std::vector<float> contour;         // MIDI per frame, NaN = unvoiced
};

// Global switches for the per-note pitch shaping (Note Glide / Drift & Vibrato in the Effects menu).
struct RenderOptions
{
    bool glide = true;
    bool shape = true;
};

// What the choir should sing: how many singers per section (bass, alto, soprano), whether the altos sing a
// harmony a third below (in the active scale) or the melody, and how big the ensemble feels.
struct ChoirSpec
{
    int   singers[3] = { 0, 0, 0 };
    bool  harmony = true;
    std::array<bool, 128> allowed {};     // notes switched on in the keyboard strip (harmony stays on these)
    float size = 0.5f;
};

namespace ClipEngine
{
    float medianPitch (const ClipAnalysis&, int a, int b);                     // NaN if no voiced frames
    void  analyse (const std::vector<float>& mono, double sr, ClipAnalysis&, std::vector<Note>&);
    // Krumhansl-Schmuckler key estimate from the detected notes (root 0-11, minor flag). Leaves the outputs alone if no notes.
    void  detectKey (const std::vector<Note>&, int& root, bool& minor);
    // Pitch contour with vibrato smoothed away (per frame, NaN outside notes). contour - slow = vibrato,
    // slow - detectedMid = drift.
    std::vector<float> slowContour (const ClipAnalysis&, const std::vector<Note>&, double sr);
    // Contour value at a source sample, interpolated between frames, nearest voiced frame as a fallback.
    double contourAt (const ClipAnalysis&, const std::vector<float>& c, double s, float fallback);
    // The note that notes[i] glides in from (the one ending just before it), or -1.
    int   glideFrom (const std::vector<Note>&, int i, double sr);
    // Semitones to shift the source by at output sample outPos of notes[i] (pitch move + drift/vibrato + glide).
    double shiftAt (const std::vector<Note>&, int i, int prev, const ClipAnalysis&, const std::vector<float>& slow,
                    double outPos, double sr, const RenderOptions&);
    // True if notes[i] differs from the recording in any way and has to be resynthesised.
    bool  isEdited (const std::vector<Note>&, int i, int prev, const RenderOptions&);
    // TD-PSOLA render of all notes (pitch shift + time stretch) plus the untouched residual.
    void  render (const float* const* src, int nch, int len, double sr,
                  const std::vector<Note>&, const ClipAnalysis&, const RenderOptions&,
                  std::vector<std::vector<float>>& out);
    // Choir layer for the clip: 6 channels (bass L/R, alto L/R, soprano L/R). Formant-aware PSOLA voices, each
    // singer with their own timing, tuning, vibrato and stereo position. Only voiced notes are sung, so
    // consonants and breaths stay with the lead.
    void  renderChoir (const float* const* src, int nch, int len, double sr,
                       const std::vector<Note>&, const ClipAnalysis&, const RenderOptions&, const ChoirSpec&,
                       std::vector<std::vector<float>>& out);
}
