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
    apvts.addParameterListener ("glideOn", this);
    apvts.addParameterListener ("shapeOn", this);
    for (auto& s : source) s.assign ((size_t) (60 * 48000), 0.f);
    worker = std::make_unique<Worker> (*this);
    worker->startThread();
}

RetuneProcessor::~RetuneProcessor()
{
    apvts.removeParameterListener ("glideOn", this);
    apvts.removeParameterListener ("shapeOn", this);
    worker->stopThread (3000);
}

void RetuneProcessor::parameterChanged (const juce::String&, float) { requestRender(); }

RenderOptions RetuneProcessor::renderOptions() const
{
    RenderOptions o;
    o.glide = pv ("glideOn") > 0.5f;
    o.shape = pv ("shapeOn") > 0.5f;
    return o;
}

juce::AudioProcessorValueTreeState::ParameterLayout RetuneProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto onOff = [&] (const char* id, const char* name, bool def)
    {
        p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    };
    auto num = [&] (const char* id, const char* name, float lo, float hi, float def, const char* unit = "", float skewCentre = 0.f)
    {
        juce::NormalisableRange<float> r (lo, hi, 0.f);
        if (skewCentre > 0.f) r.setSkewForCentre (skewCentre);
        p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def,
                     juce::AudioParameterFloatAttributes().withLabel (unit)));
    };

    onOff ("glideOn", "Note Glide On", true);
    onOff ("shapeOn", "Drift & Vibrato On", true);

    onOff ("deessOn", "De-Esser On", false);
    num ("deessThresh", "De-Esser Threshold", -60.f, 0.f, -30.f, "dB");
    num ("deessFreq",   "De-Esser Frequency", 3000.f, 12000.f, 6500.f, "Hz", 6500.f);
    num ("deessRange",  "De-Esser Max Reduction", 0.f, 24.f, 10.f, "dB");

    onOff ("compOn", "Compressor On", false);
    num ("compThresh",  "Compressor Threshold", -50.f, 0.f, -18.f, "dB");
    num ("compRatio",   "Compressor Ratio", 1.f, 10.f, 3.f, ":1", 3.f);
    num ("compAttack",  "Compressor Attack", 0.5f, 50.f, 8.f, "ms", 8.f);
    num ("compRelease", "Compressor Release", 20.f, 600.f, 120.f, "ms", 120.f);
    num ("compMix",     "Compressor Mix", 0.f, 1.f, 1.f);

    onOff ("airOn", "Air On", true);
    num ("midAir",  "Mid Air", 0.f, 1.f, 0.f);
    num ("highAir", "High Air", 0.f, 1.f, 0.f);

    onOff ("choirOn", "Choir On", false);
    num ("choirSize",    "Choir Size", 0.f, 1.f, 0.5f);
    num ("choirBass",    "Choir Bass Share", 0.f, 1.f, 0.3f);
    num ("choirAlto",    "Choir Alto Share", 0.f, 1.f, 0.4f);
    num ("choirSop",     "Choir Soprano Share", 0.f, 1.f, 0.3f);
    num ("choirBassVol", "Choir Bass Volume", -36.f, 6.f, 0.f, "dB");
    num ("choirAltoVol", "Choir Alto Volume", -36.f, 6.f, 0.f, "dB");
    num ("choirSopVol",  "Choir Soprano Volume", -36.f, 6.f, -3.f, "dB");
    num ("choirMix",     "Choir Mix", 0.f, 1.f, 0.5f);

    onOff ("doubleOn", "Doubler On", false);
    num ("doubleWidth",  "Doubler Width", 0.f, 1.f, 0.8f);
    num ("doubleDetune", "Doubler Detune", 0.f, 30.f, 9.f, "cents");
    num ("doubleMix",    "Doubler Mix", 0.f, 1.f, 0.5f);

    onOff ("echoOn", "Echo On", false);
    {
        juce::StringArray divs;
        for (int i = 0; i < vfx::Echo::numDivisions(); ++i) divs.add (vfx::Echo::divisionName (i));
        p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "echoTime", 1 }, "Echo Time", divs, 3));
    }
    num ("echoFeedback", "Echo Feedback", 0.f, 0.9f, 0.35f);
    num ("echoMix",      "Echo Mix", 0.f, 1.f, 0.25f);
    num ("echoTone",     "Echo Tone", 1000.f, 12000.f, 5000.f, "Hz", 4000.f);

    onOff ("verbOn", "Reverb On", false);
    num ("verbSize",  "Reverb Size", 0.f, 1.f, 0.6f);
    num ("verbDamp",  "Reverb Damping", 0.f, 1.f, 0.5f);
    num ("verbWidth", "Reverb Width", 0.f, 1.f, 1.f);
    num ("verbMix",   "Reverb Mix", 0.f, 1.f, 0.25f);
    return { p.begin(), p.end() };
}

FxSettings RetuneProcessor::readFx (double bpm) const
{
    FxSettings s;
    s.deessOn = pv ("deessOn") > 0.5f; s.deessThresh = pv ("deessThresh"); s.deessFreq = pv ("deessFreq"); s.deessRange = pv ("deessRange");
    s.compOn = pv ("compOn") > 0.5f;   s.compThresh = pv ("compThresh"); s.compRatio = pv ("compRatio");
    s.compAttack = pv ("compAttack");  s.compRelease = pv ("compRelease"); s.compMix = pv ("compMix");
    s.airOn = pv ("airOn") > 0.5f;     s.midAir = pv ("midAir"); s.highAir = pv ("highAir");
    s.choirOn = pv ("choirOn") > 0.5f; s.choirSize = pv ("choirSize");
    s.choirWeight[0] = pv ("choirBass"); s.choirWeight[1] = pv ("choirAlto"); s.choirWeight[2] = pv ("choirSop");
    s.choirVol[0] = pv ("choirBassVol"); s.choirVol[1] = pv ("choirAltoVol"); s.choirVol[2] = pv ("choirSopVol");
    s.choirMix = pv ("choirMix");
    s.doubleOn = pv ("doubleOn") > 0.5f; s.doubleWidth = pv ("doubleWidth"); s.doubleDetune = pv ("doubleDetune"); s.doubleMix = pv ("doubleMix");
    s.echoOn = pv ("echoOn") > 0.5f;   s.echoDiv = (int) std::lround (pv ("echoTime"));
    s.echoFeedback = pv ("echoFeedback"); s.echoMix = pv ("echoMix"); s.echoTone = pv ("echoTone");
    s.verbOn = pv ("verbOn") > 0.5f;   s.verbSize = pv ("verbSize"); s.verbDamp = pv ("verbDamp");
    s.verbWidth = pv ("verbWidth");    s.verbMix = pv ("verbMix");
    s.bpm = bpm;
    return s;
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
    fx.prepare (sampleRate);
    const size_t needed = (size_t) (60.0 * sampleRate);
    for (auto& s : source) if (s.size() < needed) s.resize (needed, 0.f);
}

// ---------------------------------------------------------------- audio thread
void RetuneProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    juce::int64 pos = 0;
    bool playing = false, havePos = false;
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto info = ph->getPosition())
        {
            playing = info->getIsPlaying();
            if (auto b = info->getBpm()) bpm = *b;
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
        lastBuf = -1; fadeLeft = 0; fadeFrom = -1;
    }
    else
    {
        const int ai = activeBuf.load (std::memory_order_acquire);   // ---- playback of the edited clip
        if (! rec && playing && havePos && ai >= 0)
        {
            if (ai != lastBuf)                                       // a new render just went live: blend from the old one
            {
                fadeFrom = (lastBuf >= 0 && lastBuf == retiredBuf.load()) ? lastBuf : -1;
                fadeLeft = fadeFrom >= 0 ? kFade : 0;
                lastBuf = ai;
            }
            const auto& rb = renderBuf[(size_t) ai];
            const std::array<std::vector<float>, 2>* ob = fadeFrom >= 0 ? &renderBuf[(size_t) fadeFrom] : nullptr;
            const juce::int64 len = (juce::int64) rb[0].size(), o = clipOrigin.load();
            for (int i = 0; i < n; ++i)
            {
                const juce::int64 idx = pos + i - o;
                if (idx >= 0 && idx < len)                           // outside the clip: dry input
                    for (int c = 0; c < nch; ++c)
                    {
                        float v = rb[(size_t) c][(size_t) idx];
                        if (fadeLeft > 0 && ob != nullptr && idx < (juce::int64) (*ob)[0].size())
                        {
                            const float f = (float) fadeLeft / (float) kFade;
                            v = v * (1.f - f) + (*ob)[(size_t) c][(size_t) idx] * f;
                        }
                        buf.setSample (c, i, v);
                    }
                if (fadeLeft > 0) --fadeLeft;
            }
        }
        else { lastBuf = -1; fadeLeft = 0; fadeFrom = -1; }
    }

    fx.process (buf, readFx (bpm));
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
    keyPending = true;
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
    ClipEngine::render (ptrs, 2, len, sr.load(), nts, an, renderOptions(), out);

    if (clipLen.load() <= 0) { status = Idle; return; }      // the clip was cleared while we were rendering
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
        { juce::ScopedLock sl (modelLock); notes.clear(); analysis = {}; cleared.valid = false; }   // new audio overwrites the old clip
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
    {
        juce::ScopedLock sl (modelLock);
        if (clipLen.load() > 0)                                 // keep everything so Undo can bring it back
        {
            cleared.notes = notes;
            cleared.analysis = analysis;
            cleared.len = clipLen.load();
            cleared.origin = clipOrigin.load();
            cleared.valid = true;
        }
        notes.clear();
        analysis = {};
    }
    clipLen.store (0);
    ++analysisVersion;
    status = Idle;
}

bool RetuneProcessor::undoClear()
{
    {
        juce::ScopedLock sl (modelLock);
        if (! cleared.valid) return false;
        notes = cleared.notes;
        analysis = cleared.analysis;
        clipLen.store (cleared.len);
        clipOrigin.store (cleared.origin);
        cleared.valid = false;
    }
    ++analysisVersion;
    requestRender();
    return true;
}

bool RetuneProcessor::canUndoClear() const { juce::ScopedLock sl (modelLock); return cleared.valid; }

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
    o.writeInt (0x52544c33);
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
    o.writeInt (noteMask.load()); o.writeInt (keyRoot.load()); o.writeInt (keyScale.load());
    for (const auto& n : notes)                                   // v3: glide + drift/vibrato per note
    {
        o.writeFloat (n.glideMs); o.writeFloat (n.glideSmooth); o.writeFloat (n.drift); o.writeFloat (n.vibrato);
    }
    if (auto xml = apvts.copyState().createXml()) o.writeString (xml->toString()); else o.writeString ({});
}

void RetuneProcessor::setStateInformation (const void* data, int size)
{
    if (size < 32) return;
    juce::MemoryInputStream in (data, (size_t) size, false);
    const int magic = in.readInt();
    if (magic != 0x52544c31 && magic != 0x52544c32 && magic != 0x52544c33) return;
    const bool v2 = (magic != 0x52544c31);                    // v2 also stores the key
    const bool v3 = (magic == 0x52544c33);                    // v3 also stores glide + drift/vibrato
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
    if (v2)
    {
        noteMask.store (in.readInt()); keyRoot.store (in.readInt()); keyScale.store (in.readInt());
    }
    if (v3)
        for (auto& n : nts)
        {
            n.glideMs = in.readFloat(); n.glideSmooth = in.readFloat(); n.drift = in.readFloat(); n.vibrato = in.readFloat();
        }
    if (! in.isExhausted())
        if (auto xml = juce::XmlDocument::parse (in.readString()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

    {
        juce::ScopedLock sl (modelLock);
        notes = std::move (nts);
        analysis = std::move (an);
        cleared.valid = false;
    }
    clipOrigin.store (origin);
    clipLen.store (len);
    keyPending = false;
    ++analysisVersion;
    requestRender();
}

juce::AudioProcessorEditor* RetuneProcessor::createEditor() { return new RetuneEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RetuneProcessor(); }
