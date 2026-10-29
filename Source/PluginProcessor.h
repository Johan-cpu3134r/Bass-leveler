#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <array>
#include "DSP.h"

namespace ids
{
    #define BL_ID(x) inline const juce::String x { #x };
    BL_ID (mode) BL_ID (target) BL_ID (amount) BL_ID (maxGain) BL_ID (q)
    BL_ID (attack) BL_ID (release) BL_ID (harmonic) BL_ID (snap) BL_ID (lowLatency)
    BL_ID (minFreq) BL_ID (maxFreq) BL_ID (glide) BL_ID (blend) BL_ID (pitchClass)
    #undef BL_ID
}

class BassLevelerProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kFirstMapNote = 28;   // E1
    static constexpr int kNumMapNotes  = 24;   // E1 .. D#3
    static constexpr int kHistory      = 24;

    BassLevelerProcessor();
    ~BassLevelerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    //==============================================================================
    // Live values for the UI (written on the audio thread, read on the message thread)
    std::atomic<float> uiMidi { 0.0f }, uiFreq { 0.0f }, uiConfidence { 0.0f };
    std::atomic<float> uiLevelDb { -100.0f }, uiGainDb { 0.0f };
    std::atomic<bool>  uiVoiced { false };
    std::atomic<float> uiLatencyMs { 0.0f };

    struct HistoryEntry { float before = 0.0f, after = 0.0f; };
    std::array<HistoryEntry, kHistory> getHistory (int& count) const;

    // Note map (dB correction per note)
    std::array<std::atomic<float>, kNumMapNotes> noteMap;
    void setNoteMapValue (int index, float db);

    // Learning the note map
    std::atomic<bool> learningMap { false };
    std::array<std::atomic<float>, kNumMapNotes> learnSum;
    std::array<std::atomic<int>,   kNumMapNotes> learnCount;
    std::atomic<double> learnSeconds { 0.0 };
    void clearLearned();
    void applyLearnedSuggestion();

    // Learning the target level
    std::atomic<bool> learningTarget { false };
    std::atomic<double> targetSum { 0.0 };
    std::atomic<int> targetCount { 0 };

    // Spectrum feed (post-processing mono)
    juce::AbstractFifo specFifo { 32768 };
    std::vector<float> specBuffer = std::vector<float> (32768, 0.0f);
    double getCurrentSampleRate() const { return currentSampleRate; }

private:
    void configureDetector();
    void runDetection();
    void finishHistoryNote();

    double currentSampleRate = 44100.0;
    int decimation = 1;
    double detRate = 44100.0;
    int decimCounter = 0;
    bl::Svf detLp1, detLp2;

    // Detection ring (double-written so the last N samples are always contiguous)
    std::vector<float> detBuf;
    int detN = 0, detWrite = 0, hopCounter = 0;
    static constexpr int kHop = 32;
    bl::Yin yin;
    bl::Median<5> median;
    int unvoicedHops = 0;
    bool voiced = false;
    float targetMidi = 45.0f, smoothMidi = 45.0f;

    // Level measurement
    bl::Svf levelBp;
    float levelEnv = 0.0f;
    float levelDb = -100.0f;

    // Gain
    float gainDb = 0.0f;
    int coeffCounter = 0;
    std::array<bl::Svf, 2> bell1, bell2;

    // Lookahead delay
    std::vector<std::vector<float>> delayLines;
    int delayLen = 1, delayWrite = 0, latencySamples = 0;
    float lastMinFreq = -1.0f;
    bool lastLowLatency = false;

    // History
    mutable juce::SpinLock historyLock;
    std::array<HistoryEntry, kHistory> history {};
    int historyWrite = 0, historyCount = 0;
    int curHistNote = -1;
    double histBeforeSum = 0.0, histGainSum = 0.0;
    int histCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassLevelerProcessor)
};
