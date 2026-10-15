#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "ClipEngine.h"
#include "AirFx.h"
#include "Hotkeys.h"

class RetuneProcessor : public juce::AudioProcessor
{
public:
    enum Status { Idle = 0, Recording, Analysing, Rendering, Ready };

    RetuneProcessor();
    ~RetuneProcessor() override;

    void prepareToPlay (double sampleRate, int blockSize) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "RetuneLab"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- control API (message thread) ----
    void setRecording (bool on);
    void clearClip();
    void requestRender();
    std::vector<Note> getNotes() const;
    ClipAnalysis getAnalysis() const;
    void setNotes (std::vector<Note>);          // updates the model only; call requestRender() when done editing

    juce::AudioProcessorValueTreeState apvts;   // Air parameters (automatable)
    HotkeyMap hotkeys;

    // ---- state read by the UI ----
    std::atomic<bool>        recordArmed { false };
    std::atomic<juce::int64> clipOrigin { 0 };   // host sample position where the clip starts
    std::atomic<int>         clipLen { 0 };      // recorded samples
    std::atomic<juce::int64> hostPos { 0 };
    std::atomic<bool>        hostPlaying { false };
    std::atomic<int>         status { Idle };
    std::atomic<int>         analysisVersion { 0 };
    double getSr() const { return sr.load(); }

private:
    class Worker;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    AirFx airFx;
    std::atomic<float>* airOnP = nullptr;
    std::atomic<float>* midAirP = nullptr;
    std::atomic<float>* highAirP = nullptr;
    void doAnalyse();
    void doRender();

    std::atomic<double> sr { 48000.0 };
    std::array<std::vector<float>, 2> source;                 // recorded clip (fixed capacity, ~60 s)
    std::array<std::array<std::vector<float>, 2>, 3> renderBuf;   // triple buffer of rendered audio
    std::atomic<int> activeBuf { -1 }, retiredBuf { -1 };
    bool originSet = false;                                   // audio thread only

    mutable juce::CriticalSection modelLock;
    std::vector<Note> notes;
    ClipAnalysis analysis;

    std::atomic<bool> needAnalyse { false }, needRender { false };
    std::unique_ptr<Worker> worker;
};
