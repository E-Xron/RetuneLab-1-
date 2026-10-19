#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "ClipEngine.h"
#include "VocalFx.h"
#include "Hotkeys.h"

// Which MIDI notes are switched on in the keyboard strip (Snap to scale and the choir harmony use them).
// Per note, so one octave can be switched off without touching the others.
class ActiveNotes
{
public:
    bool get (int n) const
    {
        if (n < 0 || n > 127) return false;
        return ((w[n >> 6].load() >> (n & 63)) & 1u) != 0;
    }
    void set (int n, bool on)
    {
        if (n < 0 || n > 127) return;
        const juce::uint64 bit = (juce::uint64) 1 << (n & 63);
        if (on) w[n >> 6].fetch_or (bit); else w[n >> 6].fetch_and (~bit);
    }
    void setPitchClasses (int mask)                  // every octave of each pitch class in mask
    {
        juce::uint64 v[2] = { 0, 0 };
        for (int n = 0; n < 128; ++n) if ((mask >> (n % 12)) & 1) v[n >> 6] |= (juce::uint64) 1 << (n & 63);
        w[0] = v[0]; w[1] = v[1];
    }
    bool any() const { return w[0].load() != 0 || w[1].load() != 0; }
    juce::uint64 word (int i) const { return w[i].load(); }
    void setWords (juce::uint64 a, juce::uint64 b) { w[0] = a; w[1] = b; }
private:
    std::atomic<juce::uint64> w[2] { { 0 }, { 0 } };
};

class RetuneProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
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

    juce::AudioProcessorValueTreeState apvts;   // effect parameters (automatable)
    HotkeyMap hotkeys;

    // ---- state read by the UI ----
    std::atomic<bool>        recordArmed { false };
    std::atomic<juce::int64> clipOrigin { 0 };   // host sample position where the clip starts
    std::atomic<int>         clipLen { 0 };      // recorded samples
    std::atomic<juce::int64> hostPos { 0 };
    std::atomic<bool>        hostPlaying { false };
    std::atomic<bool>        freeRunning { false };            // no host transport (standalone)
    std::atomic<int>         status { Idle };
    std::atomic<int>         analysisVersion { 0 };

    // active key: root 0-11, scale index (0 major, 1 minor, ...), and the set of allowed pitch classes
    std::atomic<int>  keyRoot { 0 }, keyScale { 0 };
    ActiveNotes activeNotes;
    std::atomic<bool> keyPending { false };      // set after a fresh analysis: UI should auto-detect the key

    double getSr() const { return sr.load(); }
    RenderOptions renderOptions() const;
    ChoirSpec choirSpec() const;
    float deessReduction() const { return fx.deessReduction(); }
    float compReduction() const  { return fx.compReduction(); }
    float compInLevel() const    { return fx.compIn.load(); }
    float compOutLevel() const   { return fx.compOut.load(); }
    float choirLevel (int s) const { return fx.choirLevel (s); }
    std::atomic<float> outLevel[2] { { 0.f }, { 0.f } }, inLevel[2] { { 0.f }, { 0.f } };   // block peaks for the meters
    std::atomic<double> hostBpm { 120.0 };

private:
    class Worker;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void doAnalyse();
    void doRender();
    void parameterChanged (const juce::String&, float) override;   // Note Glide / Drift & Vibrato switches re-render
    float pv (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    FxSettings readFx (double bpm) const;

    FxChain fx;

    std::atomic<double> sr { 48000.0 };
    std::array<std::vector<float>, 2> source;                 // recorded clip (fixed capacity, ~60 s)
    std::array<std::array<std::vector<float>, 8>, 3> renderBuf;   // triple buffer of rendered audio: L/R + 6 choir channels
    std::array<std::vector<float>, 6> choirScratch;               // audio thread: this block's choir sections
    std::atomic<int> activeBuf { -1 }, retiredBuf { -1 };

    // audio thread only
    bool originSet = false;
    juce::int64 freePos = 0;                                       // clock when the host has no transport
    int lastBuf = -1, fadeFrom = -1, fadeLeft = 0;
    static constexpr int kFade = 480;                         // ~10 ms crossfade when a new render goes live

    mutable juce::CriticalSection modelLock;
    std::vector<Note> notes;
    ClipAnalysis analysis;
    struct Cleared { std::vector<Note> notes; ClipAnalysis analysis; int len = 0; juce::int64 origin = 0; bool valid = false; } cleared;

    std::atomic<bool> needAnalyse { false }, needRender { false };
    std::unique_ptr<Worker> worker;
};
