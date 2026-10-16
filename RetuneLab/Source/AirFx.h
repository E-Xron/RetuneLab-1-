#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

// "Air" exciter in the spirit of Slate Digital's Fresh Air (not a clone): a smooth presence/top-end shelf
// plus two bands of harmonic excitation. Zero latency. At 100% it adds roughly +8..12 dB of top end.
//   Mid Air  - presence region (~2-8 kHz) plus harmonics generated from 2.5-7 kHz
//   High Air - top end (6 kHz and up) plus harmonics generated from 9 kHz and up
class AirFx
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        auto lim = [this] (double f) { return (float) std::min (f, sr * 0.45); };
        for (size_t c = 0; c < 2; ++c)
        {
            set (fHp2k[c],  juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (sr, lim (2000.0)));
            set (fLp8k[c],  juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass  (sr, lim (8000.0)));
            set (fHp6k1[c], juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (sr, lim (6000.0)));
            set (bHp25[c],  juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, lim (2500.0), 0.707f));
            set (bLp7[c],   juce::dsp::IIR::Coefficients<float>::makeLowPass  (sr, lim (7000.0), 0.707f));
            set (hHp6[c],   juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, lim (6000.0), 0.707f));
            set (tHp9[c],   juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, lim (9000.0), 0.707f));
            set (hHp12[c],  juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, lim (12000.0), 0.707f));
        }
        midSm.reset (sr, 0.03);  highSm.reset (sr, 0.03);
        midSm.setCurrentAndTargetValue (0.f);  highSm.setCurrentAndTargetValue (0.f);
    }

    void process (juce::AudioBuffer<float>& b, bool on, float mid, float high)
    {
        midSm.setTargetValue (on ? mid : 0.f);
        highSm.setTargetValue (on ? high : 0.f);
        if (! midSm.isSmoothing() && ! highSm.isSmoothing()
            && midSm.getTargetValue() <= 0.f && highSm.getTargetValue() <= 0.f)
            return;

        const int n = b.getNumSamples(), nch = juce::jmin (b.getNumChannels(), 2);
        float* d[2] = { b.getWritePointer (0), nch > 1 ? b.getWritePointer (1) : nullptr };
        for (int i = 0; i < n; ++i)
        {
            const float m = midSm.getNextValue(), h = highSm.getNextValue();
            for (size_t c = 0; c < (size_t) nch; ++c)
            {
                const float x = d[c][i];
                // smooth first-order shelves/bands (no phase notches when mixed back with the dry signal)
                const float band = fLp8k[c].processSample (fHp2k[c].processSample (x));
                const float top  = fHp6k1[c].processSample (x);
                // harmonic excitation
                const float mb   = bLp7[c].processSample (bHp25[c].processSample (x));
                const float midH = hHp6[c].processSample (std::tanh (18.f * mb) * 0.04f);
                const float hb   = tHp9[c].processSample (x);
                const float hiH  = hHp12[c].processSample (std::tanh (30.f * hb) * 0.03f);

                float y = x + m * (1.6f * band + 3.0f * midH) + h * (2.5f * top + 5.0f * hiH);
                const float ay = std::abs (y);
                if (ay > 0.9f) y = std::copysign (0.9f + 0.1f * std::tanh ((ay - 0.9f) / 0.1f), y);   // soft ceiling
                d[c][i] = y;
            }
        }
    }

private:
    using Filt = juce::dsp::IIR::Filter<float>;
    static void set (Filt& f, juce::dsp::IIR::Coefficients<float>::Ptr c) { f.coefficients = c; f.reset(); }

    std::array<Filt, 2> fHp2k, fLp8k, fHp6k1, bHp25, bLp7, hHp6, tHp9, hHp12;
    juce::SmoothedValue<float> midSm, highSm;
    double sr = 48000.0;
};
