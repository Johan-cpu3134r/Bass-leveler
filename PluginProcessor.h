#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "Slope.h"
#include "PhaseAnalyzer.h"

namespace ids
{
    inline constexpr auto offset    = "offset";
    inline constexpr auto shiftMin  = "shiftmin";
    inline constexpr auto shiftMax  = "shiftmax";
    inline constexpr auto viewScale = "viewscale";
    inline constexpr auto scopeLen  = "scopelen";
    inline constexpr auto mode      = "mode";
    inline constexpr auto lock      = "lock";
    inline constexpr auto depth     = "depth";
    inline constexpr auto attack    = "attack";
    inline constexpr auto release   = "release";
    inline constexpr auto slope     = "slope";
    inline constexpr auto threshold = "threshold";
}

class PhaseLockProcessor : public juce::AudioProcessor,
                           private juce::Timer
{
public:
    static constexpr float maxOffsetMs  = 20.0f;   // also the plugin's fixed latency
    static constexpr float scopeMs      = 40.0f;
    static constexpr float analysisMs   = 50.0f;
    static constexpr int   scopePoints  = 512;

    PhaseLockProcessor();
    ~PhaseLockProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Phase Lock"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- shared with the editor (message thread) ----
    struct ScopeFrame
    {
        std::array<float, scopePoints> kick {}, bass {};            // 40 ms view: raw samples
        std::array<float, scopePoints> kMin {}, kMax {}, bMin {}, bMax {}, gain {};   // long views: min/max per pixel
        bool  envelope = false;      // true = long (tempo) view
        bool  synced = false;        // aligned to the host's beat/bar grid
        float lengthMs = scopeMs;
        float lengthQ = 0.0f;        // length in quarter notes (long views)
        float bpm = 120.0f;
        float progress = 1.0f;       // live frame: how far the sweep has got (0..1)
        bool  active = false;        // live frame: a sweep is in progress
        int serial = 0;
    };
    bool copyScope (ScopeFrame& dest);                // false if nothing new
    bool copyLiveScope (ScopeFrame& dest);            // the long-view sweep in progress
    float getEffectiveOffsetMs() const noexcept       { return effectiveOffsetMs.load(); }
    float getAnalysedOffsetMs() const noexcept        { return analysedOffsetMs.load(); }
    bool  hasAnalysis() const noexcept                { return analysisValid.load(); }
    float getCorrelation() const noexcept             { return correlation.load(); }
    float getFundamentalHz() const noexcept           { return fundamentalHz.load(); }
    bool  isSidechainActive() const noexcept          { return sidechainActive.load(); }
    float getSidechainLevelDb() const noexcept        { return scLevelDb.load(); }

    slope::CustomCurve getCustomCurve() const;      // lookup table the audio uses

    // Editable custom curve (points + segment types). Message thread.
    slope::Nodes getNodes() const;
    void setNodes (slope::Nodes);

    // Turns Lock off and puts the offset at the given value in one step.
    void unlockAt (float offsetMs);

    // Allowed shift window in ms, always lo <= hi.
    std::pair<float, float> getShiftLimits() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void setParam (const char* id, float denormalised);

    // parameters
    std::atomic<float>* pOffset = nullptr; std::atomic<float>* pShiftMin = nullptr; std::atomic<float>* pShiftMax = nullptr;
    std::atomic<float>* pMode = nullptr;   std::atomic<float>* pLock = nullptr;
    std::atomic<float>* pDepth = nullptr;  std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pRelease = nullptr; std::atomic<float>* pSlope = nullptr;
    std::atomic<float>* pThreshold = nullptr;
    std::atomic<float>* pScopeLen = nullptr;

    std::array<std::atomic<float>, slope::numCustomPoints> customCurve;
    slope::Nodes nodes;
    juce::CriticalSection nodesLock;
    static juce::String nodesToString (const slope::Nodes&);
    static slope::Nodes nodesFromString (const juce::String&);

    // DSP state
    double sr = 44100.0;
    int latencySamples = 0, maxLagSamples = 0, scopeSamples = 0, windowSamples = 0;
    int ringSize = 0, ringMask = 0, writePos = 0;
    juce::AudioBuffer<float> bassRing;          // raw bass input, per channel
    std::vector<float> scRing;                  // raw sidechain (mono)
    std::vector<float> kickDelRing;             // sidechain delayed by latency
    std::vector<float> bassAlignedRing;         // bass mono, delayed by latency only (no offset)
    std::vector<float> outRing;                 // processed output (mono)

    juce::SmoothedValue<float> offsetSmooth;
    float envLevel = 0.0f, envRelease = 0.0f;
    bool armed = true, everTriggered = false;
    int samplesSinceTrigger = 1 << 30, minRetrigger = 0;
    int captureCountdown = -1;                  // samples until capture is complete
    int activityHold = 0;

    // audio -> message thread hand-off
    juce::SpinLock captureLock, scopeLock;
    KickCapture pendingCapture, workCapture;
    bool captureReady = false;
    ScopeFrame scopeFrame, liveFrame;
    void publishLive (bool synced, double lengthQ, double bpm);

    // long-view scope accumulation (min/max per pixel, built as audio arrives)
    void startAccumulation (int lengthSamples);
    void publishAccumulation (bool synced, double lengthQ, double bpm);
    std::array<float, scopePoints> accKMin {}, accKMax {}, accBMin {}, accBMax {}, accGain {};
    bool accActive = false;
    int accPos = 0, accLen = 1;
    long long accPeriodIdx = -1;
    double accLenQ = -1.0;

    PhaseAnalyzer analyzer;
    std::atomic<float> effectiveOffsetMs { 0.0f }, analysedOffsetMs { 0.0f }, lockedOffsetMs { 0.0f };
    std::atomic<float> correlation { 0.0f }, fundamentalHz { 0.0f }, scLevelDb { -100.0f };
    std::atomic<bool>  analysisValid { false }, sidechainActive { false };
    bool lastLock = false, suppressUnlockCopy = false;
    int lastMode = -1, lastRange = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaseLockProcessor)
};
