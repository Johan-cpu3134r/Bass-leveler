#include "PluginProcessor.h"
#include "PluginEditor.h"

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using NR  = juce::NormalisableRange<float>;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout BassLevelerProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto pid = [] (const juce::String& s) { return juce::ParameterID { s, 1 }; };

    auto withSkew = [] (float lo, float hi, float centre) { NR r (lo, hi); r.setSkewForCentre (centre); return r; };

    p.push_back (std::make_unique<APC> (pid (ids::mode), "Mode", juce::StringArray { "Dynamic", "Note map" }, 0));
    p.push_back (std::make_unique<APF> (pid (ids::target),   "Target",   NR (-48.0f, 0.0f, 0.1f), -18.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    p.push_back (std::make_unique<APF> (pid (ids::amount),   "Amount",   NR (0.0f, 100.0f, 0.1f), 70.0f, juce::AudioParameterFloatAttributes().withLabel ("%")));
    p.push_back (std::make_unique<APF> (pid (ids::maxGain),  "Max gain", NR (0.0f, 18.0f, 0.1f), 9.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    p.push_back (std::make_unique<APF> (pid (ids::q),        "Q",        withSkew (0.5f, 10.0f, 2.4f), 2.4f));
    p.push_back (std::make_unique<APF> (pid (ids::attack),   "Attack",   withSkew (1.0f, 200.0f, 20.0f), 12.0f, juce::AudioParameterFloatAttributes().withLabel ("ms")));
    p.push_back (std::make_unique<APF> (pid (ids::release),  "Release",  withSkew (5.0f, 1000.0f, 100.0f), 80.0f, juce::AudioParameterFloatAttributes().withLabel ("ms")));
    p.push_back (std::make_unique<APF> (pid (ids::harmonic), "Harmonic", NR (0.0f, 100.0f, 0.1f), 40.0f, juce::AudioParameterFloatAttributes().withLabel ("%")));
    p.push_back (std::make_unique<APB> (pid (ids::snap),       "Snap to semitone", true));
    p.push_back (std::make_unique<APB> (pid (ids::lowLatency), "Low latency", false));
    p.push_back (std::make_unique<APF> (pid (ids::minFreq),  "Min freq", NR (20.0f, 80.0f, 1.0f), 25.0f, juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    p.push_back (std::make_unique<APF> (pid (ids::maxFreq),  "Max freq", NR (100.0f, 400.0f, 1.0f), 250.0f, juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    p.push_back (std::make_unique<APF> (pid (ids::glide),    "Glide",    withSkew (0.0f, 200.0f, 30.0f), 20.0f, juce::AudioParameterFloatAttributes().withLabel ("ms")));
    p.push_back (std::make_unique<APF> (pid (ids::blend),    "Blend dynamic", NR (0.0f, 100.0f, 0.1f), 30.0f, juce::AudioParameterFloatAttributes().withLabel ("%")));
    p.push_back (std::make_unique<APB> (pid (ids::pitchClass), "Map by pitch class", false));

    return { p.begin(), p.end() };
}

BassLevelerProcessor::BassLevelerProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto& v : noteMap) v.store (0.0f);
    clearLearned();
}

bool BassLevelerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void BassLevelerProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    decimation = std::max (1, (int) std::floor (sampleRate / 3000.0));
    detRate = sampleRate / decimation;

    // Allocate for the worst case (20 Hz lowest note)
    const int maxTau = (int) std::ceil (detRate / 20.0) + 4;
    detBuf.assign ((size_t) maxTau * 4 + 8, 0.0f);
    yin.prepare (maxTau + 4);

    const int maxDelay = maxTau * 2 * decimation + 64;
    delayLines.assign ((size_t) getTotalNumOutputChannels(), std::vector<float> ((size_t) maxDelay, 0.0f));
    delayLen = maxDelay;
    delayWrite = 0;

    for (auto& b : bell1) b.reset();
    for (auto& b : bell2) b.reset();
    detLp1.reset(); detLp2.reset(); levelBp.reset();
    levelEnv = 0.0f; gainDb = 0.0f; voiced = false; unvoicedHops = 0;
    decimCounter = hopCounter = coeffCounter = 0;

    lastMinFreq = -1.0f;
    configureDetector();
}

void BassLevelerProcessor::configureDetector()
{
    const float minFreq = apvts.getRawParameterValue (ids::minFreq)->load();
    const bool lowLat   = apvts.getRawParameterValue (ids::lowLatency)->load() > 0.5f;

    if (std::abs (minFreq - lastMinFreq) < 0.5f && lowLat == lastLowLatency)
        return;

    lastMinFreq = minFreq;
    lastLowLatency = lowLat;

    const int tauMax = (int) std::ceil (detRate / minFreq);
    detN = std::min (tauMax * 2, (int) detBuf.size() / 2);
    std::fill (detBuf.begin(), detBuf.end(), 0.0f);
    detWrite = 0;

    // The detector "sees" a note roughly one window after it starts, so delay the audio by that much.
    latencySamples = detN * decimation;
    if (lowLat) latencySamples /= 2;
    latencySamples = std::min (latencySamples, delayLen - 1);

    for (auto& d : delayLines) std::fill (d.begin(), d.end(), 0.0f);
    setLatencySamples (latencySamples);
    uiLatencyMs.store ((float) (1000.0 * latencySamples / currentSampleRate));
}

//==============================================================================
void BassLevelerProcessor::runDetection()
{
    const float minFreq = apvts.getRawParameterValue (ids::minFreq)->load();
    const float maxFreq = apvts.getRawParameterValue (ids::maxFreq)->load();
    const bool snap = apvts.getRawParameterValue (ids::snap)->load() > 0.5f;

    const int tauMax = detN / 2;
    const int tauMin = (int) std::floor (detRate / maxFreq);
    const float* window = detBuf.data() + detWrite;   // oldest first, detN samples

    // Skip silence
    float energy = 0.0f;
    for (int i = 0; i < detN; ++i) energy += window[i] * window[i];
    energy /= (float) std::max (1, detN);

    bl::Yin::Result r;
    if (energy > 1.0e-7f)
        r = yin.detect (window, tauMin, tauMax, detRate);

    const bool good = r.confidence > 0.8f && r.freq >= minFreq * 0.97f && r.freq <= maxFreq * 1.03f;

    if (good)
    {
        float m = bl::freqToMidi (r.freq);
        if (! voiced) { median.reset (m); smoothMidi = m; }
        m = median.push (m);
        targetMidi = snap ? std::round (m) : m;
        voiced = true;
        unvoicedHops = 0;
        uiConfidence.store (r.confidence);
    }
    else if (++unvoicedHops > 4)
    {
        voiced = false;
        uiConfidence.store (r.confidence);
    }

    // Learning + history bookkeeping, once per hop
    if (voiced && levelDb > -70.0f)
    {
        const int note = (int) std::round (targetMidi);
        const int idx = note - kFirstMapNote;

        if (learningMap.load() && idx >= 0 && idx < kNumMapNotes)
        {
            learnSum[(size_t) idx].store (learnSum[(size_t) idx].load() + levelDb);
            learnCount[(size_t) idx].store (learnCount[(size_t) idx].load() + 1);
        }
        if (learningMap.load())
            learnSeconds.store (learnSeconds.load() + kHop / detRate);

        if (learningTarget.load())
        {
            targetSum.store (targetSum.load() + levelDb);
            targetCount.store (targetCount.load() + 1);
        }

        if (note != curHistNote)
        {
            finishHistoryNote();
            curHistNote = note;
        }
        histBeforeSum += levelDb;
        histGainSum += gainDb;
        ++histCount;
    }
    else if (! voiced && curHistNote >= 0)
    {
        finishHistoryNote();
        curHistNote = -1;
    }
}

void BassLevelerProcessor::finishHistoryNote()
{
    if (histCount >= 3)
    {
        const float before = (float) (histBeforeSum / histCount);
        const float after  = before + (float) (histGainSum / histCount);
        const juce::SpinLock::ScopedTryLockType lock (historyLock);
        if (lock.isLocked())
        {
            history[(size_t) historyWrite] = { before, after };
            historyWrite = (historyWrite + 1) % kHistory;
            historyCount = std::min (historyCount + 1, kHistory);
        }
    }
    histBeforeSum = histGainSum = 0.0;
    histCount = 0;
}

std::array<BassLevelerProcessor::HistoryEntry, BassLevelerProcessor::kHistory>
BassLevelerProcessor::getHistory (int& count) const
{
    std::array<HistoryEntry, kHistory> out {};
    const juce::SpinLock::ScopedLockType lock (historyLock);
    count = historyCount;
    for (int i = 0; i < historyCount; ++i)
    {
        const int src = (historyWrite - historyCount + i + kHistory) % kHistory;
        out[(size_t) i] = history[(size_t) src];
    }
    return out;
}

//==============================================================================
void BassLevelerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    configureDetector();   // cheap no-op unless min freq / low latency changed

    const int numCh = std::min (buffer.getNumChannels(), (int) delayLines.size());
    const int numSamples = buffer.getNumSamples();
    const double fs = currentSampleRate;

    const bool  noteMapMode = apvts.getRawParameterValue (ids::mode)->load() > 0.5f;
    const float target   = apvts.getRawParameterValue (ids::target)->load();
    const float amount   = apvts.getRawParameterValue (ids::amount)->load() * 0.01f;
    const float maxGain  = apvts.getRawParameterValue (ids::maxGain)->load();
    const float q        = apvts.getRawParameterValue (ids::q)->load();
    const float attackMs = apvts.getRawParameterValue (ids::attack)->load();
    const float releaseMs= apvts.getRawParameterValue (ids::release)->load();
    const float harmonic = apvts.getRawParameterValue (ids::harmonic)->load() * 0.01f;
    const float maxFreq  = apvts.getRawParameterValue (ids::maxFreq)->load();
    const float glideMs  = apvts.getRawParameterValue (ids::glide)->load();
    const float blend    = apvts.getRawParameterValue (ids::blend)->load() * 0.01f;

    auto coeff = [fs] (float ms) { return ms <= 0.0f ? 1.0f : 1.0f - std::exp (-1.0f / (float) (0.001 * ms * fs)); };
    const float atkC   = coeff (attackMs);
    const float relC   = coeff (releaseMs);
    const float glideC = coeff (glideMs);
    const float levC   = coeff (30.0f);

    const float lpCut = std::min (maxFreq * 1.25f, (float) (detRate * 0.4));
    detLp1.setLowpass (fs, lpCut, 0.5412f);   // 4th-order Butterworth
    detLp2.setLowpass (fs, lpCut, 1.3066f);

    float specScratch[512];
    int specCount = 0;

    for (int n = 0; n < numSamples; ++n)
    {
        // ---- Detection side (undelayed input) ----
        float mono = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) mono += buffer.getSample (ch, n);
        mono /= (float) std::max (1, numCh);

        const float lp = detLp2.process (detLp1.process (mono));
        if (++decimCounter >= decimation)
        {
            decimCounter = 0;
            detBuf[(size_t) detWrite] = lp;
            detBuf[(size_t) (detWrite + detN)] = lp;
            detWrite = (detWrite + 1) % detN;
            if (++hopCounter >= kHop) { hopCounter = 0; runDetection(); }
        }

        // Pitch glide (log domain)
        smoothMidi += glideC * (targetMidi - smoothMidi);
        const float f0 = bl::midiToFreq (smoothMidi);

        // Level at the fundamental
        if (coeffCounter == 0) levelBp.setBandpass (fs, f0, 4.0f);
        const float bp = levelBp.process (mono);
        levelEnv += levC * (bp * bp - levelEnv);
        levelDb = 10.0f * std::log10 (std::max (levelEnv * 2.0f, 1.0e-10f));

        // ---- Gain computer ----
        float wanted = 0.0f;
        if (voiced && levelDb > -70.0f)
        {
            if (noteMapMode)
            {
                const int idx = (int) std::round (targetMidi) - kFirstMapNote;
                const float mapG = (idx >= 0 && idx < kNumMapNotes) ? noteMap[(size_t) idx].load() * amount : 0.0f;
                wanted = mapG + blend * (target - (levelDb + mapG));
            }
            else
            {
                wanted = amount * (target - levelDb);
            }
            wanted = juce::jlimit (-maxGain, maxGain, wanted);
        }
        const float c = std::abs (wanted) > std::abs (gainDb) ? atkC : relC;
        gainDb += c * (wanted - gainDb);

        // ---- Filters (coefficients refreshed every 16 samples) ----
        if (coeffCounter == 0)
            for (int ch = 0; ch < numCh; ++ch)
            {
                bell1[(size_t) ch].setBell (fs, f0, q, gainDb);
                bell2[(size_t) ch].setBell (fs, f0 * 2.0f, q, gainDb * harmonic);
            }
        coeffCounter = (coeffCounter + 1) & 15;

        // ---- Delayed audio path ----
        int readPos = delayWrite - latencySamples;
        if (readPos < 0) readPos += delayLen;
        float outMono = 0.0f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& dl = delayLines[(size_t) ch];
            dl[(size_t) delayWrite] = buffer.getSample (ch, n);
            float y = dl[(size_t) readPos];
            y = bell2[(size_t) ch].process (bell1[(size_t) ch].process (y));
            buffer.setSample (ch, n, y);
            outMono += y;
        }
        delayWrite = (delayWrite + 1) % delayLen;

        // Spectrum feed
        specScratch[specCount++] = outMono / (float) std::max (1, numCh);
        if (specCount == 512)
        {
            if (specFifo.getFreeSpace() >= specCount)
                { int src = 0; specFifo.write (specCount).forEach ([&] (int dst) { specBuffer[(size_t) dst] = specScratch[src++]; }); }
            specCount = 0;
        }
    }

    if (specCount > 0 && specFifo.getFreeSpace() >= specCount)
        { int src = 0; specFifo.write (specCount).forEach ([&] (int dst) { specBuffer[(size_t) dst] = specScratch[src++]; }); }

    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    uiMidi.store (smoothMidi);
    uiFreq.store (bl::midiToFreq (smoothMidi));
    uiLevelDb.store (levelDb);
    uiGainDb.store (gainDb);
    uiVoiced.store (voiced);
}

//==============================================================================
void BassLevelerProcessor::setNoteMapValue (int index, float db)
{
    if (index < 0 || index >= kNumMapNotes) return;
    db = juce::jlimit (-18.0f, 18.0f, db);

    if (apvts.getRawParameterValue (ids::pitchClass)->load() > 0.5f)
    {
        const int pc = (index + kFirstMapNote) % 12;
        for (int i = 0; i < kNumMapNotes; ++i)
            if ((i + kFirstMapNote) % 12 == pc) noteMap[(size_t) i].store (db);
    }
    else
    {
        noteMap[(size_t) index].store (db);
    }
}

void BassLevelerProcessor::clearLearned()
{
    for (auto& s : learnSum) s.store (0.0f);
    for (auto& c : learnCount) c.store (0);
    learnSeconds.store (0.0);
}

void BassLevelerProcessor::applyLearnedSuggestion()
{
    double sum = 0.0; int n = 0;
    std::array<float, kNumMapNotes> avg {};
    for (int i = 0; i < kNumMapNotes; ++i)
    {
        const int c = learnCount[(size_t) i].load();
        if (c > 0) { avg[(size_t) i] = learnSum[(size_t) i].load() / (float) c; sum += avg[(size_t) i]; ++n; }
    }
    if (n == 0) return;
    const float mean = (float) (sum / n);
    for (int i = 0; i < kNumMapNotes; ++i)
        if (learnCount[(size_t) i].load() > 0)
            noteMap[(size_t) i].store (juce::jlimit (-18.0f, 18.0f, mean - avg[(size_t) i]));
}

//==============================================================================
void BassLevelerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("NOTEMAP"), nullptr);
    juce::ValueTree map ("NOTEMAP");
    for (int i = 0; i < kNumMapNotes; ++i)
        map.setProperty ("n" + juce::String (i), noteMap[(size_t) i].load(), nullptr);
    state.appendChild (map, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void BassLevelerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.hasType (apvts.state.getType())) return;

        auto map = state.getChildWithName ("NOTEMAP");
        for (int i = 0; i < kNumMapNotes; ++i)
            noteMap[(size_t) i].store ((float) map.getProperty ("n" + juce::String (i), 0.0f));
        state.removeChild (map, nullptr);
        apvts.replaceState (state);
    }
}

juce::AudioProcessorEditor* BassLevelerProcessor::createEditor()
{
    return new BassLevelerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BassLevelerProcessor();
}
