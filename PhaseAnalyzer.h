#pragma once
#include <vector>

// One snapshot taken around a detected kick, handed from the audio thread
// to the analyzer (which runs on the message thread).
struct KickCapture
{
    std::vector<float> kick;   // windowSamples, starting at the kick trigger
    std::vector<float> bass;   // windowSamples + 2 * maxLagSamples, starting maxLagSamples before the trigger
    int windowSamples = 0;
    int maxLagSamples = 0;
    double sampleRate = 44100.0;
};

// Finds the bass delay (in ms) that best lines the bass up with the kick.
// Positive offset = bass is delayed (played later).
class PhaseAnalyzer
{
public:
    enum Mode { Correlation = 0, ZeroCross = 1 };

    void reset();
    // minMs/maxMs: allowed shift window (minMs may be negative = bass earlier)
    void analyse (const KickCapture& capture, int mode, float minMs, float maxMs, float currentOffsetMs);

    bool  isValid() const noexcept             { return valid; }
    float getBestOffsetMs() const noexcept     { return bestOffsetMs; }
    float getCurrentCorrelation() const noexcept { return currentCorrelation; }
    float getFundamentalHz() const noexcept    { return fundamentalHz; }

private:
    static void zeroPhaseLowpass (std::vector<float>& x, double sampleRate, float cutoffHz);
    float correlationAtLag (int lag, int maxLag, int window) const;
    static void positiveCrossings (const std::vector<float>& x, int start, int end, std::vector<float>& out);

    std::vector<float> k, b, curve, avgCurve, crossings;
    int curveLo = 1 << 30, curveHi = 1 << 30, curveMode = -1;
    double curveRate = 0.0;

    bool  valid = false;
    float bestOffsetMs = 0.0f, currentCorrelation = 0.0f, fundamentalHz = 0.0f;
    float zcAverageMs = 0.0f;
    bool  zcHasAverage = false;
};
