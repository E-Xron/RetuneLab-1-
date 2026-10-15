#pragma once
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

    int srcLen() const { return srcEnd - srcStart; }
    int outEnd() const { return outStart + outLen; }
};

struct ClipAnalysis
{
    int hop = 256;
    int frameOffset = 384;              // sample offset of a frame's pitch estimate from frame*hop
    std::vector<float> contour;         // MIDI per frame, NaN = unvoiced
};

namespace ClipEngine
{
    float medianPitch (const ClipAnalysis&, int a, int b);                     // NaN if no voiced frames
    void  analyse (const std::vector<float>& mono, double sr, ClipAnalysis&, std::vector<Note>&);
    // TD-PSOLA render of all notes (pitch shift + time stretch) plus the untouched residual.
    void  render (const float* const* src, int nch, int len, double sr,
                  const std::vector<Note>&, const ClipAnalysis&, std::vector<std::vector<float>>& out);
}
