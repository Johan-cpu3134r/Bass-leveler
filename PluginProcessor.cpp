#include "PluginProcessor.h"
#include "PluginEditor.h"

using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;

juce::AudioProcessorValueTreeState::ParameterLayout PhaseLockProcessor::createLayout()
{
    auto msText = [] (float v, int) { return juce::String (v, 2) + " ms"; };
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<APF> (juce::ParameterID { ids::offset, 1 }, "Offset",
        juce::NormalisableRange<float> (-maxOffsetMs, maxOffsetMs, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText)));
    auto limitText = [] (float v, int) { return (v < 0.0f ? juce::String::fromUTF8 ("\xe2\x88\x92") : juce::String ("+")) + juce::String (std::abs (v), 1) + " ms"; };
    p.push_back (std::make_unique<APF> (juce::ParameterID { ids::shiftMin, 1 }, "Shift limit (earlier)",
        juce::NormalisableRange<float> (-maxOffsetMs, maxOffsetMs, 0.1f), -5.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (limitText)));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ids::shiftMax, 1 }, "Shift limit (later)",
        juce::NormalisableRange<float> (-maxOffsetMs, maxOffsetMs, 0.1f), 5.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (limitText)));
    p.push_back (std::make_unique<APC> (juce::ParameterID { ids::scopeLen, 1 }, "Scope length",
        juce::StringArray { "40 ms", "1/16", "1/8", "1/4", "1/2", "1 bar" }, 3,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    p.push_back (std::make_unique<APC> (juce::ParameterID { ids::viewScale, 1 }, "Limit view scale",
        juce::StringArray { "2 ms", "5 ms", "10 ms", "20 ms" }, 2,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    p.push_back (std::make_unique<APC> (juce::ParameterID { ids::mode, 1 }, "Detection",
        juce::StringArray { "Correlation", "Zero-cross" }, 0));
    p.push_back (std::make_unique<APB> (juce::ParameterID { ids::lock, 1 }, "Lock", false));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ids::depth, 1 }, "Depth",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 70.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int)
        {
            if (v >= 99.95f) return juce::String ("-inf dB");
            return juce::String (juce::Decibels::gainToDecibels (1.0f - v / 100.0f), 1) + " dB";
        })));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ids::attack, 1 }, "Attack",
        juce::NormalisableRange<float> (0.1f, 20.0f, 0.1f, 0.5f), 2.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " ms"; })));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ids::release, 1 }, "Release",
        juce::NormalisableRange<float> (10.0f, 500.0f, 1.0f, 0.5f), 140.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return juce::String ((int) v) + " ms"; })));
    p.push_back (std::make_unique<APC> (juce::ParameterID { ids::slope, 2 }, "Slope",
        juce::StringArray (slope::names, slope::NumShapes), slope::Exponential));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ids::threshold, 1 }, "Kick threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -24.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));

    return { p.begin(), p.end() };
}

PhaseLockProcessor::PhaseLockProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PhaseLock", createLayout())
{
    pOffset    = apvts.getRawParameterValue (ids::offset);
    pShiftMin  = apvts.getRawParameterValue (ids::shiftMin);
    pShiftMax  = apvts.getRawParameterValue (ids::shiftMax);
    pMode      = apvts.getRawParameterValue (ids::mode);
    pLock      = apvts.getRawParameterValue (ids::lock);
    pDepth     = apvts.getRawParameterValue (ids::depth);
    pAttack    = apvts.getRawParameterValue (ids::attack);
    pRelease   = apvts.getRawParameterValue (ids::release);
    pSlope     = apvts.getRawParameterValue (ids::slope);
    pThreshold = apvts.getRawParameterValue (ids::threshold);
    pScopeLen  = apvts.getRawParameterValue (ids::scopeLen);

    setNodes (slope::defaultNodes());

    lastLock = pLock->load() > 0.5f;
    startTimerHz (30);
}

PhaseLockProcessor::~PhaseLockProcessor() { stopTimer(); }

bool PhaseLockProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != out)
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.inputBuses[1];
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void PhaseLockProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    latencySamples = (int) std::ceil (maxOffsetMs * sr / 1000.0) + 2;   // +2 keeps the cubic interpolator in the past
    maxLagSamples  = (int) std::ceil (maxOffsetMs * sr / 1000.0);
    windowSamples  = (int) std::ceil (analysisMs * sr / 1000.0);
    scopeSamples   = (int) std::ceil (scopeMs * sr / 1000.0);
    minRetrigger   = (int) (0.08 * sr);
    envRelease     = (float) std::exp (-1.0 / (0.01 * sr));

    const int needed = 2 * latencySamples + windowSamples + 2 * maxLagSamples + 64;
    ringSize = juce::nextPowerOfTwo (needed);
    ringMask = ringSize - 1;
    writePos = 0;

    bassRing.setSize (2, ringSize);
    bassRing.clear();
    scRing.assign ((size_t) ringSize, 0.0f);
    kickDelRing.assign ((size_t) ringSize, 0.0f);
    bassAlignedRing.assign ((size_t) ringSize, 0.0f);
    outRing.assign ((size_t) ringSize, 0.0f);

    {
        const juce::SpinLock::ScopedLockType sl (captureLock);
        pendingCapture.kick.assign ((size_t) windowSamples, 0.0f);
        pendingCapture.bass.assign ((size_t) (windowSamples + 2 * maxLagSamples), 0.0f);
        pendingCapture.windowSamples = windowSamples;
        pendingCapture.maxLagSamples = maxLagSamples;
        pendingCapture.sampleRate = sr;
        captureReady = false;
    }

    offsetSmooth.reset (sr, 0.05);
    offsetSmooth.setCurrentAndTargetValue (pLock->load() > 0.5f ? lockedOffsetMs.load() : pOffset->load());
    envLevel = 0.0f; armed = true; everTriggered = false;
    samplesSinceTrigger = 1 << 30; captureCountdown = -1; activityHold = 0;

    setLatencySamples (latencySamples);
}

static inline float cubicRead (const float* ring, int mask, double pos) noexcept
{
    const int i = (int) std::floor (pos);
    const float f = (float) (pos - i);
    const float y0 = ring[(i - 1) & mask], y1 = ring[i & mask], y2 = ring[(i + 1) & mask], y3 = ring[(i + 2) & mask];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

void PhaseLockProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    auto main = getBusBuffer (buffer, true, 0);
    const int numCh = juce::jmin (main.getNumChannels(), 2);

    const float* sc[2] = { nullptr, nullptr };
    int numSc = 0;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        auto scBuf = getBusBuffer (buffer, true, 1);
        numSc = juce::jmin (scBuf.getNumChannels(), 2);
        for (int c = 0; c < numSc; ++c) sc[c] = scBuf.getReadPointer (c);
    }

    // parameters for this block
    const auto [loMs, hiMs] = getShiftLimits();
    const bool  locked   = pLock->load() > 0.5f;
    const float target   = juce::jlimit (loMs, hiMs, locked ? lockedOffsetMs.load() : pOffset->load());
    const float depth01  = pDepth->load() / 100.0f;
    const float attackMs = pAttack->load();
    const float releaseMs = pRelease->load();
    const int   slopeIdx = (int) pSlope->load();
    const float thr      = juce::Decibels::decibelsToGain (pThreshold->load());
    const float msPerSample = (float) (1000.0 / sr);
    const double samplesPerMs = sr / 1000.0;

    slope::CustomCurve curve;
    for (size_t i = 0; i < curve.size(); ++i) curve[i] = customCurve[i].load (std::memory_order_relaxed);

    // scope length / host tempo
    const int scopeLenIdx = juce::jlimit (0, 5, (int) pScopeLen->load());
    double bpm = 120.0, ppq0 = 0.0, barQ = 4.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto ts = pos->getTimeSignature())
                if (ts->denominator > 0) barQ = ts->numerator * 4.0 / ts->denominator;
            if (auto q = pos->getPpqPosition()) { ppq0 = *q; playing = pos->getIsPlaying(); }
        }
    bpm = juce::jlimit (20.0, 400.0, bpm);
    static const double lenQs[] = { 0.0, 0.25, 0.5, 1.0, 2.0, 0.0 };
    const double lenQ = scopeLenIdx == 5 ? barQ : lenQs[scopeLenIdx];
    const bool longView = scopeLenIdx > 0;
    const bool synced = longView && playing;
    const double samplesPerQ = sr * 60.0 / bpm;
    const int periodSamples = longView ? juce::jmax (16, (int) std::lround (lenQ * samplesPerQ)) : 0;
    if (! longView || std::abs (lenQ - accLenQ) > 1.0e-9)
    {
        accActive = false;
        accPeriodIdx = -1;
        accLenQ = lenQ;
    }

    offsetSmooth.setTargetValue (target);

    float* out[2] = { main.getWritePointer (0), numCh > 1 ? main.getWritePointer (1) : nullptr };
    float* bassR[2] = { bassRing.getWritePointer (0), bassRing.getWritePointer (1) };
    float scPeak = 0.0f;

    for (int s = 0; s < n; ++s)
    {
        const int wp = writePos = (writePos + 1) & ringMask;

        float scv = 0.0f;
        for (int c = 0; c < numSc; ++c) scv += sc[c][s];
        if (numSc > 1) scv *= 0.5f;
        scPeak = juce::jmax (scPeak, std::abs (scv));
        scRing[(size_t) wp] = scv;
        const float kd = scRing[(size_t) ((wp - latencySamples) & ringMask)];
        kickDelRing[(size_t) wp] = kd;

        float mono = 0.0f;
        for (int c = 0; c < numCh; ++c) bassR[c][wp] = out[c][s];
        for (int c = 0; c < numCh; ++c) mono += bassR[c][(wp - latencySamples) & ringMask];
        bassAlignedRing[(size_t) wp] = numCh > 1 ? 0.5f * mono : mono;

        // finish a pending capture
        if (captureCountdown > 0 && --captureCountdown == 0)
        {
            captureCountdown = -1;
            const int age = windowSamples + maxLagSamples;
            const int trig = (wp - age) & ringMask;

            if (captureLock.tryEnter())
            {
                for (int i = 0; i < windowSamples; ++i)
                    pendingCapture.kick[(size_t) i] = kickDelRing[(size_t) ((trig + i) & ringMask)];
                const int bl = windowSamples + 2 * maxLagSamples;
                for (int j = 0; j < bl; ++j)
                    pendingCapture.bass[(size_t) j] = bassAlignedRing[(size_t) ((trig - maxLagSamples + j) & ringMask)];
                captureReady = true;
                captureLock.exit();
            }
            if (! longView && scopeLock.tryEnter())
            {
                scopeFrame.envelope = false;
                scopeFrame.lengthMs = scopeMs;
                for (int i = 0; i < scopePoints; ++i)
                {
                    const double pos = trig + (double) i * (scopeSamples - 1) / (scopePoints - 1);
                    const int ip = (int) pos; const float f = (float) (pos - ip);
                    auto lerp = [&] (const std::vector<float>& r)
                    { return r[(size_t) (ip & ringMask)] * (1.0f - f) + r[(size_t) ((ip + 1) & ringMask)] * f; };
                    scopeFrame.kick[(size_t) i] = lerp (kickDelRing);
                    scopeFrame.bass[(size_t) i] = lerp (outRing);
                }
                ++scopeFrame.serial;
                scopeLock.exit();
            }
        }

        // kick trigger (on the latency-delayed sidechain so it lines up with our output)
        const float a = std::abs (kd);
        envLevel = a > envLevel ? a : envLevel * envRelease;
        if (samplesSinceTrigger < (1 << 30)) ++samplesSinceTrigger;
        if (! armed && envLevel < thr * 0.5f) armed = true;
        bool triggered = false;
        if (armed && envLevel > thr && samplesSinceTrigger > minRetrigger)
        {
            triggered = true;
            armed = false;
            everTriggered = true;
            samplesSinceTrigger = 0;
            if (captureCountdown < 0) captureCountdown = windowSamples + maxLagSamples;
        }

        const float gain = everTriggered
            ? slope::gainAt ((float) samplesSinceTrigger * msPerSample, attackMs, releaseMs, depth01, slopeIdx, curve)
            : 1.0f;

        const double d = latencySamples + offsetSmooth.getNextValue() * samplesPerMs;
        const double readPos = (double) wp - d;
        float outMono = 0.0f;
        for (int c = 0; c < numCh; ++c)
        {
            const float y = cubicRead (bassR[c], ringMask, readPos) * gain;
            out[c][s] = y;
            outMono += y;
        }
        const float outM = numCh > 1 ? 0.5f * outMono : outMono;
        outRing[(size_t) wp] = outM;

        // long scope views: synced to the host's beat grid while playing,
        // otherwise started by a kick (free run)
        if (longView)
        {
            if (synced)
            {
                // Every view starts on the beat grid: views shorter than a beat start on
                // each beat, longer ones on their own boundary (1/2 note, bar). So the
                // picture always shows the same part of the beat and stands still.
                const double anchorQ = juce::jmax (lenQ, 1.0);
                // our output (and the delayed kick) runs latencySamples behind the host's
                // timeline, so shift the grid by the same amount to line the beats up
                const double pos = (ppq0 + (s - latencySamples) / samplesPerQ) / anchorQ + 1.0e-9;
                const long long idx = (long long) std::floor (pos);
                if (accPeriodIdx < 0)
                {
                    // transport just started: only begin if we're right on a boundary
                    accPeriodIdx = idx;
                    if ((pos - (double) idx) * anchorQ * samplesPerQ < 1.0)
                        startAccumulation (periodSamples);
                }
                else if (idx != accPeriodIdx)
                {
                    if (accActive && accPos >= (int) (accLen * 0.9))
                        publishAccumulation (true, lenQ, bpm);
                    startAccumulation (periodSamples);
                    accPeriodIdx = idx;
                }
            }
            else
            {
                accPeriodIdx = -1;
                if (! accActive && triggered)
                    startAccumulation (periodSamples);
            }

            if (accActive)
            {
                const int b = juce::jmin (scopePoints - 1, (int) ((long long) accPos * scopePoints / accLen));
                accKMin[(size_t) b] = juce::jmin (accKMin[(size_t) b], kd);
                accKMax[(size_t) b] = juce::jmax (accKMax[(size_t) b], kd);
                accBMin[(size_t) b] = juce::jmin (accBMin[(size_t) b], outM);
                accBMax[(size_t) b] = juce::jmax (accBMax[(size_t) b], outM);
                accGain[(size_t) b] = juce::jmin (accGain[(size_t) b], gain);
                ++accPos;
                if (accPos >= accLen)
                {
                    // done: show it and wait for the next beat/bar (synced) or kick (free run)
                    publishAccumulation (synced, lenQ, bpm);
                    accActive = false;
                }
            }
        }
    }

    for (int c = numCh; c < main.getNumChannels(); ++c) main.clear (c, 0, n);

    if (longView)
        publishLive (synced, lenQ, bpm);   // lets the editor draw the sweep as it happens

    effectiveOffsetMs.store (offsetSmooth.getCurrentValue());
    if (scPeak > 0.0003f) activityHold = (int) (0.5 * sr);
    else activityHold = juce::jmax (0, activityHold - n);
    sidechainActive.store (activityHold > 0);
    scLevelDb.store (juce::Decibels::gainToDecibels (scPeak, -100.0f));
}

void PhaseLockProcessor::setParam (const char* id, float v)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

std::pair<float, float> PhaseLockProcessor::getShiftLimits() const noexcept
{
    const float a = pShiftMin->load(), b = pShiftMax->load();
    return { juce::jmin (a, b), juce::jmax (a, b) };
}

void PhaseLockProcessor::unlockAt (float offsetMs)
{
    if (pLock->load() > 0.5f)
    {
        suppressUnlockCopy = true;
        setParam (ids::lock, 0.0f);
    }
    setParam (ids::offset, offsetMs);
}

void PhaseLockProcessor::timerCallback()
{
    const auto [loMs, hiMs] = getShiftLimits();
    const int mode = (int) pMode->load();
    const bool locked = pLock->load() > 0.5f;

    bool haveNew = false;
    {
        const juce::SpinLock::ScopedTryLockType tl (captureLock);
        if (tl.isLocked() && captureReady)
        {
            workCapture = pendingCapture;
            captureReady = false;
            haveNew = true;
        }
    }

    if (mode != lastMode)
    {
        analyzer.reset();
        analysisValid.store (false);
        lastMode = mode;
    }

    if (haveNew)
    {
        analyzer.analyse (workCapture, mode, loMs, hiMs, effectiveOffsetMs.load());
        if (analyzer.isValid())
        {
            analysedOffsetMs.store (juce::jlimit (loMs, hiMs, analyzer.getBestOffsetMs()));
            analysisValid.store (true);
        }
        correlation.store (analyzer.getCurrentCorrelation());
        fundamentalHz.store (analyzer.getFundamentalHz());
    }

    if (analysisValid.load())
        analysedOffsetMs.store (juce::jlimit (loMs, hiMs, analysedOffsetMs.load()));

    if (locked && ! lastLock)
        lockedOffsetMs.store (analysisValid.load() ? analysedOffsetMs.load() : pOffset->load());
    else if (locked && analysisValid.load())
        lockedOffsetMs.store (analysedOffsetMs.load());

    if (! locked && lastLock)
    {
        if (! suppressUnlockCopy)
            setParam (ids::offset, lockedOffsetMs.load());   // stay where Lock left the bass
        suppressUnlockCopy = false;
    }
    lastLock = locked;
}

void PhaseLockProcessor::startAccumulation (int lengthSamples)
{
    accActive = true;
    accPos = 0;
    accLen = juce::jmax (1, lengthSamples);
    accKMin.fill (1.0e9f); accKMax.fill (-1.0e9f);
    accBMin.fill (1.0e9f); accBMax.fill (-1.0e9f);
    accGain.fill (1.0e9f);
}

void PhaseLockProcessor::publishAccumulation (bool synced, double lengthQ, double bpm)
{
    if (! scopeLock.tryEnter())
        return;
    float lk0 = 0, lk1 = 0, lb0 = 0, lb1 = 0, lg = 1;
    for (size_t i = 0; i < (size_t) scopePoints; ++i)
    {
        if (accKMin[i] <= accKMax[i]) { lk0 = accKMin[i]; lk1 = accKMax[i]; lb0 = accBMin[i]; lb1 = accBMax[i]; lg = accGain[i]; }
        scopeFrame.kMin[i] = lk0; scopeFrame.kMax[i] = lk1;
        scopeFrame.bMin[i] = lb0; scopeFrame.bMax[i] = lb1;
        scopeFrame.gain[i] = lg;
    }
    scopeFrame.envelope = true;
    scopeFrame.synced = synced;
    scopeFrame.lengthQ = (float) lengthQ;
    scopeFrame.bpm = (float) bpm;
    scopeFrame.lengthMs = (float) (accLen * 1000.0 / sr);
    ++scopeFrame.serial;
    scopeLock.exit();
}

void PhaseLockProcessor::publishLive (bool synced, double lengthQ, double bpm)
{
    if (! scopeLock.tryEnter())
        return;
    auto& f = liveFrame;
    const int filled = accActive ? juce::jmin (scopePoints, (int) ((long long) accPos * scopePoints / accLen) + 1) : 0;
    for (size_t i = 0; i < (size_t) filled; ++i)
    {
        const bool ok = accKMin[i] <= accKMax[i];
        f.kMin[i] = ok ? accKMin[i] : 0.0f; f.kMax[i] = ok ? accKMax[i] : 0.0f;
        f.bMin[i] = ok ? accBMin[i] : 0.0f; f.bMax[i] = ok ? accBMax[i] : 0.0f;
        f.gain[i] = ok ? accGain[i] : 1.0f;
    }
    f.envelope = true;
    f.active = accActive;
    f.progress = accActive ? juce::jlimit (0.0f, 1.0f, (float) accPos / (float) accLen) : 1.0f;
    f.synced = synced;
    f.lengthQ = (float) lengthQ;
    f.bpm = (float) bpm;
    f.lengthMs = (float) (accLen * 1000.0 / sr);
    ++f.serial;
    scopeLock.exit();
}

bool PhaseLockProcessor::copyLiveScope (ScopeFrame& dest)
{
    const juce::SpinLock::ScopedLockType sl (scopeLock);
    if (liveFrame.serial == dest.serial) return false;
    dest = liveFrame;
    return true;
}

bool PhaseLockProcessor::copyScope (ScopeFrame& dest)
{
    const juce::SpinLock::ScopedLockType sl (scopeLock);
    if (scopeFrame.serial == dest.serial) return false;
    dest = scopeFrame;
    return true;
}

slope::CustomCurve PhaseLockProcessor::getCustomCurve() const
{
    slope::CustomCurve c {};
    for (size_t i = 0; i < c.size(); ++i) c[i] = customCurve[i].load();
    return c;
}

slope::Nodes PhaseLockProcessor::getNodes() const
{
    const juce::ScopedLock sl (nodesLock);
    return nodes;
}

void PhaseLockProcessor::setNodes (slope::Nodes n)
{
    // keep it valid: sorted, inside [0,1], first at x=0, last at x=1
    if (n.size() < 2) n = slope::defaultNodes();
    std::sort (n.begin(), n.end(), [] (const slope::Node& a, const slope::Node& b) { return a.x < b.x; });
    for (auto& p : n)
    {
        p.x = juce::jlimit (0.0f, 1.0f, p.x);
        p.y = juce::jlimit (0.0f, 1.0f, p.y);
        p.type = juce::jlimit (0, slope::NumSegmentTypes - 1, p.type);
        p.tension = juce::jlimit (-1.0f, 1.0f, p.tension);
    }
    n.front().x = 0.0f;
    n.back().x = 1.0f;

    const auto table = slope::toTable (n);
    {
        const juce::ScopedLock sl (nodesLock);
        nodes = std::move (n);
    }
    for (size_t i = 0; i < table.size(); ++i) customCurve[i].store (table[i]);
}

juce::String PhaseLockProcessor::nodesToString (const slope::Nodes& n)
{
    juce::StringArray parts;
    for (auto& p : n)
        parts.add (juce::String (p.x, 4) + "," + juce::String (p.y, 4) + "," + juce::String (p.type) + "," + juce::String (p.tension, 3));
    return parts.joinIntoString (";");
}

slope::Nodes PhaseLockProcessor::nodesFromString (const juce::String& s)
{
    slope::Nodes n;
    for (auto& part : juce::StringArray::fromTokens (s, ";", ""))
    {
        auto f = juce::StringArray::fromTokens (part, ",", "");
        if (f.size() == 4)
            n.push_back ({ f[0].getFloatValue(), f[1].getFloatValue(), f[2].getIntValue(), f[3].getFloatValue() });
    }
    return n;
}

void PhaseLockProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("nodes", nodesToString (getNodes()), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PhaseLockProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.hasType (apvts.state.getType())) return;

        if (state.hasProperty ("nodes"))
            setNodes (nodesFromString (state.getProperty ("nodes").toString()));
        else if (state.hasProperty ("custom"))
        {
            // older versions: 16 evenly spaced values
            auto pts = juce::StringArray::fromTokens (state.getProperty ("custom").toString(), " ", "");
            slope::Nodes n;
            for (int i = 0; i < pts.size(); ++i)
                n.push_back ({ (float) i / (float) juce::jmax (1, pts.size() - 1), pts[i].getFloatValue(), slope::SingleCurve, 0.0f });
            setNodes (n);
        }
        apvts.replaceState (state);
        lastLock = pLock->load() > 0.5f;
        lockedOffsetMs.store (pOffset->load());
    }
}

juce::AudioProcessorEditor* PhaseLockProcessor::createEditor() { return new PhaseLockEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PhaseLockProcessor(); }
