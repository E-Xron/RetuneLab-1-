#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "AirFx.h"

// The vocal effects chain behind the Effects menu. Everything is zero latency on the dry path, allocation free on the
// audio thread, and each effect fades in / out over ~20 ms when it is switched, so toggling never clicks.
// Order: De-Esser -> Compressor -> Air -> Choir -> Doubler -> Echo -> Reverb.

namespace vfx
{
    constexpr float kPi = 3.14159265358979f;

    inline float dbToGain (float db) { return std::pow (10.f, db / 20.f); }
    inline float gainToDb (float g)  { return 20.f * std::log10 (std::max (g, 1e-9f)); }

    // RBJ biquad, transposed direct form II. Coefficients are plain floats, so they can change on the audio thread.
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        void setHighPass (double sr, double f, double q = 0.7071) { set (sr, f, q, true); }
        void setLowPass  (double sr, double f, double q = 0.7071) { set (sr, f, q, false); }
        void reset() { z1 = z2 = 0.f; }
        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    private:
        void set (double sr, double f, double q, bool hp)
        {
            f = std::min (f, sr * 0.45);
            const double w = 2.0 * 3.14159265358979 * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
            const double a0 = 1.0 + al;
            const double nb0 = hp ? (1.0 + c) * 0.5 : (1.0 - c) * 0.5;
            const double nb1 = hp ? -(1.0 + c) : (1.0 - c);
            b0 = (float) (nb0 / a0); b1 = (float) (nb1 / a0); b2 = (float) (nb0 / a0);
            a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al) / a0);
        }
    };

    // Mono circular buffer the pitch-shifted voices read from.
    struct DelayLine
    {
        std::vector<float> buf;
        int mask = 0, w = 0;
        void prepare (int minSize)
        {
            int n = 1; while (n < minSize) n <<= 1;
            buf.assign ((size_t) n, 0.f); mask = n - 1; w = 0;
        }
        void reset() { std::fill (buf.begin(), buf.end(), 0.f); }
        void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
        float read (float delay) const                 // delay in samples behind the newest sample
        {
            const float p = (float) (w - 1) - delay;
            const int i0 = (int) std::floor (p);
            const float fr = p - (float) i0;
            const float a = buf[(size_t) (i0 & mask)], b = buf[(size_t) ((i0 + 1) & mask)];
            return a + (b - a) * fr;
        }
    };

    // One pitch-shifted voice: two taps sweeping through a window, crossfaded with sin^2 (sums to 1).
    struct ShiftVoice
    {
        float phase = 0.f, window = 1500.f, baseDelay = 400.f, ratio = 1.f;
        float lfoPhase = 0.f, lfoRate = 0.f, lfoDepth = 0.f;       // slow pitch wander (cents) for a human feel
        float gainL = 0.7f, gainR = 0.7f;

        float process (const DelayLine& d, double sr)
        {
            const float lfo = std::sin (lfoPhase * 2.f * kPi) * lfoDepth;
            lfoPhase += lfoRate / (float) sr; if (lfoPhase >= 1.f) lfoPhase -= 1.f;
            const float r = ratio * std::pow (2.f, lfo / 1200.f);
            phase += (1.f - r) / window;
            phase -= std::floor (phase);
            const float p2 = phase + 0.5f - std::floor (phase + 0.5f);
            const float g1 = std::sin (kPi * phase), g2 = std::sin (kPi * p2);
            return d.read (baseDelay + phase * window) * g1 * g1 + d.read (baseDelay + p2 * window) * g2 * g2;
        }
    };

    // ---------------------------------------------------------------- De-Esser
    // Split-band: only the sibilant band above `freq` is turned down, so nothing else in the voice changes.
    class DeEsser
    {
    public:
        void prepare (double s) { sr = s; for (auto& f : hp) f.reset(); env = 0.f; gr = 0.f; lastF = -1.f; }
        void process (float* const* d, int nch, int n, float threshDb, float freq, float rangeDb)
        {
            if (freq != lastF) { for (auto& f : hp) f.setHighPass (sr, freq); lastF = freq; }
            const float att = std::exp (-1.f / (0.0005f * (float) sr)), rel = std::exp (-1.f / (0.06f * (float) sr));
            const float grSm = std::exp (-1.f / (0.002f * (float) sr));
            for (int i = 0; i < n; ++i)
            {
                float hv[2] = { 0.f, 0.f }, peak = 0.f;
                for (int c = 0; c < nch; ++c) { hv[c] = hp[(size_t) c].process (d[c][i]); peak = std::max (peak, std::abs (hv[c])); }
                env = peak > env ? att * env + (1.f - att) * peak : rel * env + (1.f - rel) * peak;
                const float over = gainToDb (env) - threshDb;
                const float target = over > 0.f ? std::min (rangeDb, over * 0.75f) : 0.f;
                gr = grSm * gr + (1.f - grSm) * target;
                const float g = dbToGain (-gr);
                for (int c = 0; c < nch; ++c) d[c][i] -= (1.f - g) * hv[c];
            }
        }
        float reductionDb() const { return gr; }
    private:
        double sr = 48000.0;
        std::array<Biquad, 2> hp;
        float env = 0.f, gr = 0.f, lastF = -1.f;
    };

    // ---------------------------------------------------------------- Compressor
    // RMS detector, 6 dB soft knee, stereo linked, release that slows down on sustained gain reduction
    // (program dependent, like an opto), automatic make-up gain, parallel mix.
    class Compressor
    {
    public:
        void prepare (double s) { sr = s; rms = 0.f; grDb = 0.f; slow = 0.f; }
        void process (float* const* d, int nch, int n, float threshDb, float ratio, float attackMs, float releaseMs, float mix)
        {
            const float rmsC = std::exp (-1.f / (0.005f * (float) sr));
            const float att  = std::exp (-1.f / (std::max (0.1f, attackMs) * 0.001f * (float) sr));
            const float relF = std::exp (-1.f / (std::max (5.f, releaseMs) * 0.001f * (float) sr));
            const float relS = std::exp (-1.f / (std::max (5.f, releaseMs) * 0.004f * (float) sr));
            const float slowC = std::exp (-1.f / (0.5f * (float) sr));
            const float knee = 6.f, makeup = dbToGain (-0.5f * threshDb * (1.f - 1.f / ratio));
            for (int i = 0; i < n; ++i)
            {
                float sq = 0.f;
                for (int c = 0; c < nch; ++c) sq = std::max (sq, d[c][i] * d[c][i]);
                rms = rmsC * rms + (1.f - rmsC) * sq;
                const float lv = 10.f * std::log10 (std::max (rms, 1e-12f)) + 3.f;       // RMS -> peak-ish dB
                const float o = lv - threshDb;
                float target = 0.f;
                if (o >= knee * 0.5f)       target = o * (1.f - 1.f / ratio);
                else if (o > -knee * 0.5f)  { const float k = o + knee * 0.5f; target = (1.f - 1.f / ratio) * k * k / (2.f * knee); }
                slow = slowC * slow + (1.f - slowC) * target;
                const float rel = slow > 3.f ? relS : relF;
                grDb = target > grDb ? att * grDb + (1.f - att) * target : rel * grDb + (1.f - rel) * target;
                const float g = dbToGain (-grDb) * makeup;
                for (int c = 0; c < nch; ++c) d[c][i] = d[c][i] * (1.f - mix) + d[c][i] * g * mix;
            }
        }
        float reductionDb() const { return grDb; }
    private:
        double sr = 48000.0;
        float rms = 0.f, grDb = 0.f, slow = 0.f;
    };

    // ---------------------------------------------------------------- Choir (mix stage)
    // The singers themselves are rendered offline from the recorded clip (ClipEngine::renderChoir), which keeps
    // each voice's formants natural. Here the three sections are balanced, the harsh top and boomy bottom are
    // trimmed, and the ensemble is placed in a hall so it blends like a real choir instead of a stack of copies.
    class ChoirMix
    {
    public:
        void prepare (double s)
        {
            sr = s;
            for (auto& f : hp) { f.setHighPass (sr, 90.0); f.reset(); }
            for (auto& f : lp) { f.setLowPass (sr, 7000.0, 0.6); f.reset(); }
            for (auto& f : lp2) { f.setLowPass (sr, 11000.0, 0.7); f.reset(); }
            hall.setSampleRate (sr); hall.reset();
        }
        void reset() { hall.reset(); for (auto& f : hp) f.reset(); for (auto& f : lp) f.reset(); for (auto& f : lp2) f.reset(); }
        // in: 6 channels (bass L/R, alto L/R, soprano L/R) for this block, or nullptr outside the clip
        void process (float* const* d, int nch, int n, const float* const* in, const float volDb[3], float mix, float room)
        {
            float vol[3];
            for (int s2 = 0; s2 < 3; ++s2) vol[s2] = volDb[s2] <= -35.9f ? 0.f : dbToGain (volDb[s2]);
            juce::Reverb::Parameters p;
            p.roomSize = 0.55f + 0.35f * room; p.damping = 0.6f; p.width = 1.f;
            p.wetLevel = 0.12f + 0.3f * room; p.dryLevel = 1.f - 0.45f * room; p.freezeMode = 0.f;
            hall.setParameters (p);
            float pk[3] = { 0.f, 0.f, 0.f };
            for (int i = 0; i < n; ++i)
            {
                float l = 0.f, r = 0.f;
                if (in != nullptr)
                    for (int s2 = 0; s2 < 3; ++s2)
                    {
                        const float a = in[s2 * 2][i] * vol[s2], b = in[s2 * 2 + 1][i] * vol[s2];
                        l += a; r += b;
                        pk[s2] = std::max (pk[s2], std::max (std::abs (a), std::abs (b)));
                    }
                bufL[(size_t) i] = lp2[0].process (lp[0].process (hp[0].process (l)));
                bufR[(size_t) i] = lp2[1].process (lp[1].process (hp[1].process (r)));
            }
            for (int s2 = 0; s2 < 3; ++s2) levels[(size_t) s2].store (pk[s2] * mix);
            hall.processStereo (bufL.data(), bufR.data(), n);
            for (int i = 0; i < n; ++i)
            {
                if (nch > 1) { d[0][i] += mix * bufL[(size_t) i]; d[1][i] += mix * bufR[(size_t) i]; }
                else d[0][i] += mix * 0.5f * (bufL[(size_t) i] + bufR[(size_t) i]);
            }
        }
        static constexpr int kMaxBlock = 256;
        float level (int section) const { return levels[(size_t) section].load(); }
    private:
        std::array<std::atomic<float>, 3> levels { { { 0.f }, { 0.f }, { 0.f } } };
        double sr = 48000.0;
        std::array<Biquad, 2> hp, lp, lp2;
        juce::Reverb hall;
        std::array<float, kMaxBlock> bufL {}, bufR {};
    };

    // ---------------------------------------------------------------- Doubler
    // Two slightly detuned, slightly late copies panned apart: the "sang it twice" double-track sound.
    class Doubler
    {
    public:
        void prepare (double s)
        {
            sr = s; line.prepare ((int) (0.1 * sr));
            v[0].phase = 0.f; v[1].phase = 0.5f;
            v[0].lfoRate = 0.21f; v[1].lfoRate = 0.17f; v[0].lfoDepth = v[1].lfoDepth = 3.f;
        }
        void reset() { line.reset(); }
        void process (float* const* d, int nch, int n, float width, float cents, float mix)
        {
            const float pan[2] = { -width, width };
            for (int k = 0; k < 2; ++k)
            {
                v[k].ratio = std::pow (2.f, (k == 0 ? -cents : cents) / 1200.f);
                v[k].window = 0.03f * (float) sr;
                v[k].baseDelay = (k == 0 ? 0.011f : 0.017f) * (float) sr;
                v[k].gainL = std::cos ((pan[k] + 1.f) * kPi * 0.25f);
                v[k].gainR = std::sin ((pan[k] + 1.f) * kPi * 0.25f);
            }
            for (int i = 0; i < n; ++i)
            {
                const float in = nch > 1 ? 0.5f * (d[0][i] + d[1][i]) : d[0][i];
                line.push (in);
                float l = 0.f, r = 0.f;
                for (auto& vc : v) { const float y = vc.process (line, sr); l += y * vc.gainL; r += y * vc.gainR; }
                if (nch > 1) { d[0][i] += mix * l; d[1][i] += mix * r; }
                else d[0][i] += mix * 0.5f * (l + r);
            }
        }
    private:
        double sr = 48000.0;
        DelayLine line;
        ShiftVoice v[2];
    };

    // ---------------------------------------------------------------- Echo
    // Tempo-synced stereo delay with a darkening feedback loop.
    class Echo
    {
    public:
        static int numDivisions() { return 6; }
        static const char* divisionName (int i)
        {
            static const char* n[] = { "1/16", "1/8", "1/8 dotted", "1/4", "1/4 dotted", "1/2" };
            return n[i];
        }
        void prepare (double s)
        {
            sr = s;
            for (auto& l : line) l.prepare ((int) (4.1 * sr));
            for (auto& f : lp) f.reset();
            for (auto& f : hp) f.reset();
            curDelay = -1.f; lastTone = -1.f;
        }
        void reset() { for (auto& l : line) l.reset(); }
        void process (float* const* d, int nch, int n, double bpm, int division, float feedback, float mix, float tone)
        {
            static const float beats[] = { 0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.f };
            const double b = bpm > 20.0 && bpm < 400.0 ? bpm : 120.0;
            const float target = (float) juce::jmin (4.0 * sr, 60.0 / b * beats[juce::jlimit (0, 5, division)] * sr);
            if (curDelay < 0.f) curDelay = target;
            if (tone != lastTone) { for (auto& f : lp) f.setLowPass (sr, tone); for (auto& f : hp) f.setHighPass (sr, 150.0); lastTone = tone; }
            const float glide = 1.f - std::exp (-1.f / (0.05f * (float) sr));
            for (int i = 0; i < n; ++i)
            {
                curDelay += (target - curDelay) * glide;
                for (int c = 0; c < 2; ++c)
                {
                    const float in = d[juce::jmin (c, nch - 1)][i];
                    const float wet = line[(size_t) c].read (curDelay);
                    line[(size_t) c].push (in + hp[(size_t) c].process (lp[(size_t) c].process (wet)) * feedback);
                    if (c < nch) d[c][i] += wet * mix;
                }
            }
        }
    private:
        double sr = 48000.0;
        std::array<DelayLine, 2> line;
        std::array<Biquad, 2> lp, hp;
        float curDelay = -1.f, lastTone = -1.f;
    };

    // ---------------------------------------------------------------- Reverb
    class Reverb
    {
    public:
        void prepare (double s) { verb.setSampleRate (s); verb.reset(); }
        void reset() { verb.reset(); }
        void process (float* const* d, int nch, int n, float size, float damp, float width, float mix)
        {
            juce::Reverb::Parameters p;
            p.roomSize = size; p.damping = damp; p.width = width;
            p.wetLevel = mix * 0.35f; p.dryLevel = 1.f - 0.4f * mix; p.freezeMode = 0.f;
            verb.setParameters (p);
            if (nch > 1) verb.processStereo (d[0], d[1], n); else verb.processMono (d[0], n);
        }
    private:
        juce::Reverb verb;
    };
}

// Everything the chain needs for one block, read from the parameters by the processor.
struct FxSettings
{
    bool  deessOn = false;  float deessThresh = -30.f, deessFreq = 6500.f, deessRange = 10.f;
    bool  compOn = false;   float compThresh = -18.f, compRatio = 3.f, compAttack = 8.f, compRelease = 120.f, compMix = 1.f;
    bool  airOn = true;     float midAir = 0.f, highAir = 0.f;
    bool  choirOn = false;  float choirVol[3] = { 0.f, 0.f, 0.f }, choirMix = 0.5f, choirRoom = 0.5f;
    const float* const* choirIn = nullptr;   // this block's rendered choir sections (6 channels), or nullptr
    bool  doubleOn = false; float doubleWidth = 0.8f, doubleDetune = 9.f, doubleMix = 0.5f;
    bool  echoOn = false;   int echoDiv = 3; float echoFeedback = 0.35f, echoMix = 0.25f, echoTone = 5000.f;
    bool  verbOn = false;   float verbSize = 0.6f, verbDamp = 0.5f, verbWidth = 1.f, verbMix = 0.25f;
    double bpm = 120.0;
};

class FxChain
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        deess.prepare (sr); comp.prepare (sr); air.prepare (sr); choir.prepare (sr);
        doubler.prepare (sr); echo.prepare (sr); verb.prepare (sr);
        for (auto& f : fade) { f.reset (sr, 0.02); f.setCurrentAndTargetValue (0.f); }
    }

    void process (juce::AudioBuffer<float>& b, const FxSettings& s)
    {
        const int nch = juce::jmin (2, b.getNumChannels()), total = b.getNumSamples();
        if (nch == 0) return;
        const bool on[kSlots] = { s.deessOn, s.compOn, s.choirOn, s.doubleOn, s.echoOn, s.verbOn };
        for (int k = 0; k < kSlots; ++k)
        {
            auto& f = fade[(size_t) k];
            if (on[k] && f.getCurrentValue() <= 0.f && ! f.isSmoothing()) resetSlot (k);   // fresh start, no stale tail
            f.setTargetValue (on[k] ? 1.f : 0.f);
        }

        for (int off = 0; off < total; off += kChunk)
        {
            const int n = juce::jmin (kChunk, total - off);
            chunkOffset = off;
            float* d[2] = { b.getWritePointer (0, off), nch > 1 ? b.getWritePointer (1, off) : nullptr };
            for (int k = 0; k < kSlots; ++k)
            {
                if (k == 2)                                   // Air sits after the dynamics and keeps its own smoothing
                {
                    juce::AudioBuffer<float> view (d, nch, n);
                    air.process (view, s.airOn, s.midAir, s.highAir);
                }
                auto& f = fade[(size_t) k];
                if (! f.isSmoothing() && f.getTargetValue() <= 0.f) continue;
                float* w[2] = { wet[0].data(), wet[1].data() };
                for (int c = 0; c < nch; ++c) std::copy (d[c], d[c] + n, w[c]);
                const float before = k == 1 ? peak (w, nch, n) : 0.f;
                runSlot (k, w, nch, n, s);
                if (k == 1) { compIn.store (before); compOut.store (peak (w, nch, n)); }
                for (int i = 0; i < n; ++i)
                {
                    const float g = f.getNextValue();
                    for (int c = 0; c < nch; ++c) d[c][i] += (w[c][i] - d[c][i]) * g;
                }
            }
        }
    }

    float deessReduction() const { return deess.reductionDb(); }
    float compReduction() const  { return comp.reductionDb(); }
    float choirLevel (int section) const { return choir.level (section); }

    std::atomic<float> compIn { 0.f }, compOut { 0.f };      // peak levels around the compressor, for the meters

private:
    static constexpr int kSlots = 6, kChunk = 256;

    static float peak (float* const* d, int nch, int n)
    {
        float p = 0.f;
        for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) p = std::max (p, std::abs (d[c][i]));
        return p;
    }

    void resetSlot (int k)
    {
        if (k == 0) deess.prepare (sr);
        if (k == 1) comp.prepare (sr);
        if (k == 2) choir.reset();
        if (k == 3) doubler.reset();
        if (k == 4) echo.reset();
        if (k == 5) verb.reset();
    }

    void runSlot (int k, float* const* w, int nch, int n, const FxSettings& s)
    {
        switch (k)
        {
            case 0: deess.process (w, nch, n, s.deessThresh, s.deessFreq, s.deessRange); break;
            case 1: comp.process (w, nch, n, s.compThresh, s.compRatio, s.compAttack, s.compRelease, s.compMix); break;
            case 2:
            {
                const float* in[6] = {};
                if (s.choirIn != nullptr) for (int c = 0; c < 6; ++c) in[c] = s.choirIn[c] + chunkOffset;
                choir.process (w, nch, n, s.choirIn != nullptr ? in : nullptr, s.choirVol, s.choirMix, s.choirRoom);
                break;
            }
            case 3: doubler.process (w, nch, n, s.doubleWidth, s.doubleDetune, s.doubleMix); break;
            case 4: echo.process (w, nch, n, s.bpm, s.echoDiv, s.echoFeedback, s.echoMix, s.echoTone); break;
            case 5: verb.process (w, nch, n, s.verbSize, s.verbDamp, s.verbWidth, s.verbMix); break;
            default: break;
        }
    }

    double sr = 48000.0;
    vfx::DeEsser deess;
    vfx::Compressor comp;
    AirFx air;
    vfx::ChoirMix choir;
    int chunkOffset = 0;
    vfx::Doubler doubler;
    vfx::Echo echo;
    vfx::Reverb verb;
    std::array<juce::SmoothedValue<float>, kSlots> fade;
    std::array<std::array<float, kChunk>, 2> wet {};
};
