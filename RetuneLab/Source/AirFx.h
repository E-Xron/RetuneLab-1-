#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

// "Air" exciter in the spirit of Slate Digital's Fresh Air (not a clone): two bands of
// harmonic excitation + a gentle parallel high-frequency lift. Zero latency.
//   Mid Air  - presence region (~2.5-7 kHz band, harmonics reach 6 kHz and up)
//   High Air - top end (8 kHz and up, plus harmonics above 12 kHz)
class AirFx
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        auto lim = [this] (double f) { return (float) std::min (f, sr * 0.45); };
        for (size_t c = 0; c < 2; ++c)
        {
            setHP (hp25[c], lim (2500.0));  setLP (lp7[c], lim (7000.0));
            setHP (hp6[c],  lim (6000.0));  setHP (hp9[c], lim (9000.0));
            setHP (hp12[c], lim (12000.0));
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
                const float band = lp7[c].processSample (hp25[c].processSample (x));
                const float midH = hp6[c].processSample (std::tanh (18.f * band) * 0.04f);
                const float hb   = hp9[c].processSample (x);
                const float hiH  = hp12[c].processSample (std::tanh (30.f * hb) * 0.03f);
                d[c][i] = x + m * (0.3f * band + 1.0f * midH) + h * (0.7f * hb + 1.4f * hiH);
            }
        }
    }

private:
    using Filt = juce::dsp::IIR::Filter<float>;
    void setHP (Filt& f, float hz) { f.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, hz, 0.707f); f.reset(); }
    void setLP (Filt& f, float hz) { f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sr, hz, 0.707f); f.reset(); }

    std::array<Filt, 2> hp25, lp7, hp6, hp9, hp12;
    juce::SmoothedValue<float> midSm, highSm;
    double sr = 48000.0;
};
