#include "ClipEngine.h"
#include <algorithm>

namespace
{
    const float kNaN = std::numeric_limits<float>::quiet_NaN();
    inline double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

    // YIN. x must hold W + tauMax samples. Returns period in samples (fractional) or 0.
    float yinPeriod (const float* x, int W, int tauMin, int tauMax, std::vector<float>& d)
    {
        d.assign ((size_t) tauMax + 1, 0.f);
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float s = 0.f;
            for (int j = 0; j < W; ++j) { float e = x[j] - x[j + tau]; s += e * e; }
            d[(size_t) tau] = s;
        }
        float run = 0.f;
        d[0] = 1.f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run += d[(size_t) tau];
            d[(size_t) tau] = run > 0.f ? d[(size_t) tau] * (float) tau / run : 1.f;
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (d[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0) return 0.f;
        float t = (float) best;
        if (best > 1 && best < tauMax)
        {
            float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            float den = a - 2.f * b + c;
            if (std::abs (den) > 1e-9f) t += 0.5f * (a - c) / den;
        }
        return t;
    }
}

float ClipEngine::medianPitch (const ClipAnalysis& an, int a, int b)
{
    if (an.contour.empty()) return kNaN;
    const int nf = (int) an.contour.size();
    const int fa = std::max (0, (int) std::ceil ((double) (a - an.frameOffset) / an.hop));
    const int fb = std::min (nf - 1, (int) std::floor ((double) (b - an.frameOffset) / an.hop));
    std::vector<float> v;
    for (int f = fa; f <= fb; ++f)
        if (! std::isnan (an.contour[(size_t) f])) v.push_back (an.contour[(size_t) f]);
    if (v.empty()) return kNaN;

    // Pitch centre: skip the onset scoop and release droop, then take the median of the steady part.
    std::vector<float> w = v;
    if (v.size() >= 8)
        w.assign (v.begin() + (long) (v.size() * 15 / 100), v.end() - (long) (v.size() / 10));
    std::nth_element (w.begin(), w.begin() + (long) w.size() / 2, w.end());
    return w[w.size() / 2];
}

void ClipEngine::analyse (const std::vector<float>& mono, double sr, ClipAnalysis& an, std::vector<Note>& notes)
{
    const int H = 256, W = 768;
    const int tauMax = std::min (1100, (int) (sr / 70.0));
    const int tauMin = std::max (2, (int) (sr / 1000.0));
    const int frameLen = W + tauMax, n = (int) mono.size();

    an.hop = H; an.frameOffset = W / 2; an.contour.clear(); notes.clear();
    if (n < frameLen) return;

    const int frames = (n - frameLen) / H + 1;
    an.contour.assign ((size_t) frames, kNaN);
    std::vector<float> rms ((size_t) frames, 0.f), d;

    for (int f = 0; f < frames; ++f)
    {
        const float* x = &mono[(size_t) f * H];
        double e = 0.0;
        for (int j = 0; j < W; ++j) e += (double) x[j] * x[j];
        const float r = (float) std::sqrt (e / W);
        rms[(size_t) f] = r;
        if (r < 0.004f) continue;                         // silence gate
        const float per = yinPeriod (x, W, tauMin, tauMax, d);
        if (per > 0.f) an.contour[(size_t) f] = (float) (69.0 + 12.0 * std::log2 ((sr / per) / 440.0));
    }
    { // 3-point median to kill single-frame spikes
        auto c2 = an.contour;
        for (int i = 1; i < frames - 1; ++i)
        {
            float a = an.contour[(size_t) i - 1], b = an.contour[(size_t) i], c = an.contour[(size_t) i + 1];
            if (std::isnan (a) || std::isnan (b) || std::isnan (c)) continue;
            c2[(size_t) i] = std::max (std::min (a, b), std::min (std::max (a, b), c));
        }
        an.contour = c2;
    }

    auto flush = [&] (int fa, int fb)
    {
        if (fb - fa + 1 < 5) return;                      // too short: leave it as untouched residual
        Note nt;
        nt.srcStart = std::max (0, fa * H + an.frameOffset - H / 2);
        nt.srcEnd   = std::min (n, fb * H + an.frameOffset + H / 2);
        const float med = medianPitch (an, nt.srcStart, nt.srcEnd);
        if (std::isnan (med)) return;
        nt.detectedMid = nt.targetMid = med;
        nt.outStart = nt.srcStart;
        nt.outLen   = nt.srcEnd - nt.srcStart;
        notes.push_back (nt);
    };

    int start = -1, lastVoiced = -1, unvoicedRun = 0, devRun = 0, cnt = 0, dipFrame = -1;
    float mean = 0.f, notePeak = 0.f, dipRms = 0.f;
    for (int i = 0; i < frames; ++i)
    {
        const float p = an.contour[(size_t) i], r = rms[(size_t) i];
        if (std::isnan (p))
        {
            if (start >= 0 && ++unvoicedRun >= 3) { flush (start, lastVoiced); start = -1; }
            continue;
        }
        unvoicedRun = 0;
        if (start < 0) { start = i; mean = p; cnt = 1; devRun = 0; notePeak = r; dipFrame = -1; lastVoiced = i; continue; }
        lastVoiced = i;

        if (std::abs (p - mean) > 0.75f)                   // pitch jump -> new note
        {
            if (++devRun >= 4)
            {
                const int ns = i - 3;
                flush (start, ns - 1);
                start = ns; mean = p; cnt = 1; devRun = 0; notePeak = r; dipFrame = -1;
                continue;
            }
        }
        else { devRun = 0; cnt = std::min (cnt + 1, 30); mean += (p - mean) / (float) cnt; }

        notePeak = std::max (notePeak, r);                 // energy dip -> repeated note
        if (r < 0.3f * notePeak) { if (dipFrame < 0 || r < dipRms) { dipFrame = i; dipRms = r; } }
        else if (dipFrame >= 0 && r > 0.6f * notePeak)
        {
            flush (start, dipFrame - 1);
            start = dipFrame; mean = p; cnt = 1; notePeak = r; dipFrame = -1;
        }
    }
    if (start >= 0) flush (start, lastVoiced);
}

void ClipEngine::render (const float* const* src, int nch, int len, double sr,
                         const std::vector<Note>& notes, const ClipAnalysis& an,
                         std::vector<std::vector<float>>& out)
{
    out.assign ((size_t) nch, std::vector<float> ((size_t) len, 0.f));
    std::vector<float> mono ((size_t) len, 0.f), cover ((size_t) len, 0.f);
    for (int c = 0; c < nch; ++c)
        for (int i = 0; i < len; ++i) mono[(size_t) i] += src[c][i] / (float) nch;

    const double pi = 3.14159265358979323846;
    const int Tmin = std::max (8, (int) (sr / 1000.0)), Tmax = (int) (sr / 70.0);
    const int h = std::max (8, (int) (0.004 * sr));            // half-length of the note-to-note crossfade (4 ms)

    auto freqAt = [&] (int s, float fallbackMidi) -> double
    {
        const int nf = (int) an.contour.size();
        if (nf > 0)
        {
            const int i = (int) std::lround ((double) (s - an.frameOffset) / an.hop);
            for (int k = 0; k <= 8; ++k)
                for (int sg = -1; sg <= 1; sg += 2)
                {
                    const int j = i + sg * k;
                    if (j >= 0 && j < nf && ! std::isnan (an.contour[(size_t) j])) return midiToHz (an.contour[(size_t) j]);
                }
        }
        return midiToHz (fallbackMidi);
    };
    auto periodAt = [&] (int s, float fb) { return std::min (Tmax, std::max (Tmin, (int) std::lround (sr / freqAt (s, fb)))); };
    auto argmax = [&] (int lo, int hi)
    {
        int bi = lo; float bv = mono[(size_t) lo];
        for (int i = lo + 1; i <= hi; ++i) if (mono[(size_t) i] > bv) { bv = mono[(size_t) i]; bi = i; }
        return bi;
    };

    auto renderNote = [&] (const Note& nt) -> bool
    {
        const int a = std::max (0, nt.srcStart), b = std::min (len, nt.srcEnd);
        if (b - a < 128 || nt.outLen < 2 * h + 16) return false;

        // Pitch marks, extended a little past both ends so the crossfade regions have real audio to use.
        const int lo = std::max (0, a - 2 * h), hi = std::min (len - 1, b + 2 * h);
        std::vector<int> marks;
        int m = argmax (a, std::min (b - 1, a + periodAt (a, nt.detectedMid)));
        marks.push_back (m);
        for (;;)
        {
            const int T = periodAt (m, nt.detectedMid), nxt = m + T;
            if (nxt >= hi) break;
            const int l2 = std::max (m + 1, nxt - T / 4), h2 = std::min (hi, nxt + T / 4);
            if (h2 <= l2) break;
            m = argmax (l2, h2);
            marks.push_back (m);
        }
        std::vector<int> pre;
        m = marks.front();
        for (;;)
        {
            const int T = periodAt (m, nt.detectedMid), prv = m - T;
            if (prv <= lo) break;
            const int l2 = std::max (lo, prv - T / 4), h2 = std::min (m - 1, prv + T / 4);
            if (h2 <= l2) break;
            m = argmax (l2, h2);
            pre.push_back (m);
        }
        marks.insert (marks.begin(), pre.rbegin(), pre.rend());
        if (marks.size() < 3) return false;

        const double ratio   = std::pow (2.0, (double) (nt.targetMid - nt.detectedMid) / 12.0);
        const double stretch = (double) (b - a) / (double) nt.outLen;
        const double endT = (double) (nt.outLen - h);

        double ts = -(double) h;
        size_t k = 0;
        while (ts < (double) (nt.outLen + h))
        {
            const double tsrc = (double) a + ts * stretch;
            while (k + 1 < marks.size() && std::abs ((double) marks[k + 1] - tsrc) <= std::abs ((double) marks[k] - tsrc)) ++k;
            const int mk = marks[k], Ta = periodAt (mk, nt.detectedMid);
            const double Ts = (double) Ta / ratio;
            const double g = Ts / (double) Ta;
            const int centre = nt.outStart + (int) std::lround (ts);

            // raised-cosine in/out: neighbouring notes overlap by 2h and their weights sum to 1
            double fg = 1.0;
            if (ts < (double) h) fg = 0.5 - 0.5 * std::cos (pi * (ts + h) / (2.0 * h));
            if (ts > endT)       fg = std::min (fg, 0.5 + 0.5 * std::cos (pi * (ts - endT) / (2.0 * h)));

            for (int j = -Ta; j <= Ta; ++j)
            {
                const int si = mk + j, oi = centre + j;
                if (si < 0 || si >= len || oi < 0 || oi >= len) continue;
                const float w = (float) (0.5 * (1.0 + std::cos (pi * (double) j / (double) Ta)) * g * fg);
                for (int c = 0; c < nch; ++c) out[(size_t) c][(size_t) oi] += src[c][si] * w;
            }
            ts += Ts;
        }
        return true;
    };

    // Where a note replaces the original audio, remove the original with the same crossfade shape,
    // so original + notes always add up to exactly one signal (no dips, no leaks at note boundaries).
    auto addCover = [&] (const Note& nt)
    {
        const int a = std::max (0, nt.srcStart), b = std::min (len, nt.srcEnd);
        const int s0 = std::max (0, a - h), s1 = std::min (len - 1, b + h);
        for (int i = s0; i <= s1; ++i)
        {
            const double wi = i < a - h ? 0.0 : (i > a + h ? 1.0 : 0.5 - 0.5 * std::cos (pi * (double) (i - (a - h)) / (2.0 * h)));
            const double wo = i < b - h ? 1.0 : (i > b + h ? 0.0 : 0.5 + 0.5 * std::cos (pi * (double) (i - (b - h)) / (2.0 * h)));
            cover[(size_t) i] += (float) (wi * wo);
        }
    };

    for (const auto& nt : notes)
    {
        bool ok = true;
        if (! nt.muted) ok = renderNote (nt);
        if (ok) addCover (nt);                              // couldn't process: leave original audio in place
    }
    for (int c = 0; c < nch; ++c)
        for (int i = 0; i < len; ++i)
            out[(size_t) c][(size_t) i] += src[c][i] * std::max (0.f, 1.f - cover[(size_t) i]);
}
