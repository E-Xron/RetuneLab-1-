#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

class RetuneProcessor::Worker : public juce::Thread
{
public:
    explicit Worker (RetuneProcessor& p) : juce::Thread ("RetuneLab worker"), proc (p) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            if (proc.needAnalyse.exchange (false))     { proc.doAnalyse(); proc.needRender = true; }
            else if (proc.needRender.exchange (false)) proc.doRender();
            else                                       wait (500);
        }
    }
private:
    RetuneProcessor& proc;
};

RetuneProcessor::RetuneProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    airOnP   = apvts.getRawParameterValue ("airOn");
    midAirP  = apvts.getRawParameterValue ("midAir");
    highAirP = apvts.getRawParameterValue ("highAir");
    for (auto& s : source) s.assign ((size_t) (60 * 48000), 0.f);
    worker = std::make_unique<Worker> (*this);
    worker->startThread();
}

RetuneProcessor::~RetuneProcessor() { worker->stopThread (3000); }

juce::AudioProcessorValueTreeState::ParameterLayout RetuneProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "airOn", 1 }, "Air On", true));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "midAir", 1 }, "Mid Air",
        juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "highAir", 1 }, "High Air",
        juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    return { p.begin(), p.end() };
}

bool RetuneProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo())
           && o == l.getMainInputChannelSet();
}

void RetuneProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    airFx.prepare (sampleRate);
    const size_t needed = (size_t) (60.0 * sampleRate);
    for (auto& s : source) if (s.size() < needed) s.resize (needed, 0.f);
}

// ---------------------------------------------------------------- audio thread
void RetuneProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    juce::int64 pos = 0;
    bool playing = false, havePos = false;
    if (auto* ph = getPlayHead())
        if (auto info = ph->getPosition())
        {
            playing = info->getIsPlaying();
            if (auto t = info->getTimeInSamples()) { pos = *t; havePos = true; }
        }
    hostPos.store (pos);
    hostPlaying.store (playing);

    const int n = buf.getNumSamples(), nch = juce::jmin (buf.getNumChannels(), 2);
    const bool rec = recordArmed.load();
    if (! rec) originSet = false;

    if (rec && playing && havePos)                         // ---- record (input passes through)
    {
        if (! originSet) { clipOrigin.store (pos); clipLen.store (0); originSet = true; }
        const juce::int64 o = clipOrigin.load();
        const juce::int64 cap = (juce::int64) source[0].size();
        for (int i = 0; i < n; ++i)
        {
            const juce::int64 idx = pos + i - o;
            if (idx < 0 || idx >= cap) continue;
            const float l = buf.getSample (0, i);
            const float r = nch > 1 ? buf.getSample (1, i) : l;
            source[0][(size_t) idx] = l;
            source[1][(size_t) idx] = r;
        }
        const juce::int64 endIdx = juce::jmin (cap, pos + n - o);
        if (endIdx > (juce::int64) clipLen.load()) clipLen.store ((int) endIdx);
    }
    else
    {
        const int ai = activeBuf.load (std::memory_order_acquire);   // ---- playback of the edited clip
        if (! rec && playing && havePos && ai >= 0)
        {
            const auto& rb = renderBuf[(size_t) ai];
            const juce::int64 len = (juce::int64) rb[0].size(), o = clipOrigin.load();
            for (int i = 0; i < n; ++i)
            {
                const juce::int64 idx = pos + i - o;
                if (idx < 0 || idx >= len) continue;               // outside the clip: dry input
                for (int c = 0; c < nch; ++c) buf.setSample (c, i, rb[(size_t) c][(size_t) idx]);
            }
        }
    }

    airFx.process (buf, airOnP->load() > 0.5f, midAirP->load(), highAirP->load());
}

// ---------------------------------------------------------------- worker jobs
void RetuneProcessor::doAnalyse()
{
    status = Analysing;
    const int len = juce::jmin (clipLen.load(), (int) source[0].size());
    std::vector<float> mono ((size_t) len);
    for (int i = 0; i < len; ++i) mono[(size_t) i] = 0.5f * (source[0][(size_t) i] + source[1][(size_t) i]);

    ClipAnalysis an;
    std::vector<Note> nts;
    ClipEngine::analyse (mono, sr.load(), an, nts);
    {
        juce::ScopedLock sl (modelLock);
        notes = std::move (nts);
        analysis = std::move (an);
    }
    ++analysisVersion;
}

void RetuneProcessor::doRender()
{
    status = Rendering;
    std::vector<Note> nts;
    ClipAnalysis an;
    { juce::ScopedLock sl (modelLock); nts = notes; an = analysis; }

    const int len = juce::jmin (clipLen.load(), (int) source[0].size());
    if (len <= 0) { status = Idle; return; }

    const float* ptrs[2] = { source[0].data(), source[1].data() };
    std::vector<std::vector<float>> out;
    ClipEngine::render (ptrs, 2, len, sr.load(), nts, an, out);

    const int a = activeBuf.load(), r = retiredBuf.load();
    int pick = 0;
    for (int i = 0; i < 3; ++i) if (i != a && i != r) { pick = i; break; }
    renderBuf[(size_t) pick][0] = std::move (out[0]);
    renderBuf[(size_t) pick][1] = std::move (out[1]);
    retiredBuf.store (a);
    activeBuf.store (pick, std::memory_order_release);
    status = Ready;
}

// ---------------------------------------------------------------- control API
void RetuneProcessor::setRecording (bool on)
{
    if (on)
    {
        { juce::ScopedLock sl (modelLock); notes.clear(); analysis = {}; }
        activeBuf.store (-1);
        clipLen.store (0);
        ++analysisVersion;
        status = Recording;
        recordArmed.store (true);
    }
    else
    {
        if (! recordArmed.exchange (false)) return;
        if (clipLen.load() > 0) { needAnalyse = true; worker->notify(); }
        else status = Idle;
    }
}

void RetuneProcessor::clearClip()
{
    recordArmed.store (false);
    activeBuf.store (-1);
    { juce::ScopedLock sl (modelLock); notes.clear(); analysis = {}; }
    clipLen.store (0);
    ++analysisVersion;
    status = Idle;
}

void RetuneProcessor::requestRender() { needRender = true; worker->notify(); }

std::vector<Note> RetuneProcessor::getNotes() const { juce::ScopedLock sl (modelLock); return notes; }
ClipAnalysis RetuneProcessor::getAnalysis() const   { juce::ScopedLock sl (modelLock); return analysis; }
void RetuneProcessor::setNotes (std::vector<Note> n) { juce::ScopedLock sl (modelLock); notes = std::move (n); }

// ---------------------------------------------------------------- project state
void RetuneProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream o (dest, false);
    juce::ScopedLock sl (modelLock);
    const int len = juce::jmin (clipLen.load(), (int) source[0].size());
    o.writeInt (0x52544c31);
    o.writeDouble (sr.load());
    o.writeInt64 (clipOrigin.load());
    o.writeInt (len);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < len; ++i)
            o.writeShort ((short) juce::jlimit (-32767, 32767, (int) std::lround (source[(size_t) c][(size_t) i] * 32767.f)));
    o.writeInt ((int) notes.size());
    for (const auto& n : notes)
    {
        o.writeInt (n.srcStart); o.writeInt (n.srcEnd); o.writeInt (n.outStart); o.writeInt (n.outLen);
        o.writeFloat (n.detectedMid); o.writeFloat (n.targetMid); o.writeBool (n.muted);
    }
    o.writeInt (analysis.hop); o.writeInt (analysis.frameOffset); o.writeInt ((int) analysis.contour.size());
    for (float f : analysis.contour) o.writeFloat (f);
    if (auto xml = apvts.copyState().createXml()) o.writeString (xml->toString()); else o.writeString ({});
}

void RetuneProcessor::setStateInformation (const void* data, int size)
{
    if (size < 32) return;
    juce::MemoryInputStream in (data, (size_t) size, false);
    if (in.readInt() != 0x52544c31) return;
    in.readDouble();
    const juce::int64 origin = in.readInt64();
    const int len = juce::jmin (in.readInt(), (int) source[0].size());
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < len; ++i)
            source[(size_t) c][(size_t) i] = (float) in.readShort() / 32767.f;

    std::vector<Note> nts ((size_t) juce::jmax (0, in.readInt()));
    for (auto& n : nts)
    {
        n.srcStart = in.readInt(); n.srcEnd = in.readInt(); n.outStart = in.readInt(); n.outLen = in.readInt();
        n.detectedMid = in.readFloat(); n.targetMid = in.readFloat(); n.muted = in.readBool();
    }
    ClipAnalysis an;
    an.hop = in.readInt(); an.frameOffset = in.readInt();
    an.contour.resize ((size_t) juce::jmax (0, in.readInt()));
    for (auto& f : an.contour) f = in.readFloat();
    if (! in.isExhausted())
        if (auto xml = juce::XmlDocument::parse (in.readString()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

    {
        juce::ScopedLock sl (modelLock);
        notes = std::move (nts);
        analysis = std::move (an);
    }
    clipOrigin.store (origin);
    clipLen.store (len);
    ++analysisVersion;
    requestRender();
}

juce::AudioProcessorEditor* RetuneProcessor::createEditor() { return new RetuneEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RetuneProcessor(); }
