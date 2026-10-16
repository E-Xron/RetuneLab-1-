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
    void clearClip();                           // undoable with undoClear()
    bool undoClear();
    bool canUndoClear() const;
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

    // active key: root 0-11, scale index (0 major, 1 minor, ...), and the set of allowed pitch classes
    std::atomic<int>  keyRoot { 0 }, keyScale { 0 }, noteMask { 2741 };
    std::atomic<bool> keyPending { false };      // set after a fresh analysis: UI should auto-detect the key

    double getSr() const { return sr.load(); }

private:
    class Worker;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void doAnalyse();
    void doRender();

    AirFx airFx;
    std::atomic<float>* airOnP = nullptr;
    std::atomic<float>* midAirP = nullptr;
    std::atomic<float>* highAirP = nullptr;

    std::atomic<double> sr { 48000.0 };
    std::array<std::vector<float>, 2> source;                 // recorded clip (fixed capacity, ~60 s)
    std::array<std::array<std::vector<float>, 2>, 3> renderBuf;   // triple buffer of rendered audio
    std::atomic<int> activeBuf { -1 }, retiredBuf { -1 };

    // audio thread only
    bool originSet = false;
    int lastBuf = -1, fadeFrom = -1, fadeLeft = 0;
    static constexpr int kFade = 480;                         // ~10 ms crossfade when a new render goes live

    mutable juce::CriticalSection modelLock;
    std::vector<Note> notes;
    ClipAnalysis analysis;
    struct Cleared { std::vector<Note> notes; ClipAnalysis analysis; int len = 0; juce::int64 origin = 0; bool valid = false; } cleared;

    std::atomic<bool> needAnalyse { false }, needRender { false };
    std::unique_ptr<Worker> worker;
};
