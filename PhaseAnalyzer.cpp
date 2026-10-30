#include "PhaseAnalyzer.h"
#include <cmath>
#include <algorithm>

void PhaseAnalyzer::reset()
{
    avgCurve.clear();
    curveLo = curveHi = 1 << 30;
    curveMode = -1;
    zcHasAverage = false;
    valid = false;
}

void PhaseAnalyzer::zeroPhaseLowpass (std::vector<float>& x, double sampleRate, float cutoffHz)
{
    // Two one-pole passes forward, two backward: steep enough to isolate the
    // sub/low-bass region, with zero phase shift so it can't bias the result.
    const float a = (float) std::exp (-2.0 * 3.14159265358979 * cutoffHz / sampleRate);
    for (int pass = 0; pass < 2; ++pass)
    {
        float s = 0.0f;
        for (auto& v : x) { s = v + a * (s - v); v = s; }
        s = 0.0f;
        for (auto it = x.rbegin(); it != x.rend(); ++it) { s = *it + a * (s - *it); *it = s; }
    }
}

float PhaseAnalyzer::correlationAtLag (int lag, int maxLag, int window) const
{
    // Bass delayed by 'lag' samples => bass sample used at kick index i is b[maxLag + i - lag]
    double kb = 0.0, kk = 0.0, bb = 0.0;
    const float* bp = b.data() + (maxLag - lag);
    for (int i = 0; i < window; ++i)
    {
        const double kv = k[(size_t) i], bv = bp[i];
        kb += kv * bv; kk += kv * kv; bb += bv * bv;
    }
    const double d = std::sqrt (kk * bb);
    return d > 1.0e-12 ? (float) (kb / d) : 0.0f;
}

void PhaseAnalyzer::positiveCrossings (const std::vector<float>& x, int start, int end, std::vector<float>& out)
{
    out.clear();
    start = std::max (start, 1);
    end = std::min (end, (int) x.size());
    for (int i = start; i < end; ++i)
        if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f)
        {
            const float a = x[(size_t) i - 1], c = x[(size_t) i];
            out.push_back ((float) (i - 1) + a / (a - c));
        }
}

void PhaseAnalyzer::analyse (const KickCapture& cap, int mode, float minMs, float maxMs, float currentOffsetMs)
{
    const int window = cap.windowSamples, maxLag = cap.maxLagSamples;
    if (window <= 0 || (int) cap.kick.size() < window || (int) cap.bass.size() < window + 2 * maxLag)
        return;

    const double msPerSample = 1000.0 / cap.sampleRate;
    if (minMs > maxMs) std::swap (minMs, maxMs);
    const int loLag = std::clamp ((int) std::lround (minMs / msPerSample), -maxLag, maxLag);
    const int hiLag = std::clamp ((int) std::lround (maxMs / msPerSample), loLag, maxLag);

    k = cap.kick;
    b = cap.bass;
    zeroPhaseLowpass (k, cap.sampleRate, 160.0f);
    zeroPhaseLowpass (b, cap.sampleRate, 160.0f);

    // Need real signal in both
    double ke = 0.0, be = 0.0;
    for (int i = 0; i < window; ++i) { ke += k[(size_t) i] * k[(size_t) i]; be += b[(size_t) (maxLag + i)] * b[(size_t) (maxLag + i)]; }
    if (ke < 1.0e-8 || be < 1.0e-8)
        return;

    // Bass fundamental from zero-crossing spacing
    positiveCrossings (b, 0, (int) b.size(), crossings);
    if (crossings.size() >= 3)
    {
        const float period = (crossings.back() - crossings.front()) / (float) (crossings.size() - 1);
        const float hz = (float) (cap.sampleRate / period);
        if (hz > 15.0f && hz < 500.0f)
            fundamentalHz = fundamentalHz > 0.0f ? 0.7f * fundamentalHz + 0.3f * hz : hz;
    }

    const int currentLag = std::clamp ((int) std::lround (currentOffsetMs / msPerSample), -maxLag, maxLag);
    currentCorrelation = correlationAtLag (currentLag, maxLag, window);

    if (mode != curveMode || loLag != curveLo || hiLag != curveHi || std::abs (cap.sampleRate - curveRate) > 0.5)
    {
        avgCurve.clear();
        zcHasAverage = false;
        curveMode = mode;
        curveLo = loLag;
        curveHi = hiLag;
        curveRate = cap.sampleRate;
    }

    if (mode == Correlation)
    {
        // curve[i] is the correlation with the bass delayed by (loLag + i) samples.
        // Step through lags in ~0.05 ms steps, then refine around the peak.
        const int n = hiLag - loLag + 1;
        curve.assign ((size_t) n, 0.0f);
        const int step = std::max (1, (int) std::lround (0.05 / msPerSample));
        for (int i = 0; i < n; i += step)
            curve[(size_t) i] = correlationAtLag (loLag + i, maxLag, window);
        curve[(size_t) n - 1] = correlationAtLag (hiLag, maxLag, window);
        for (int i = 0; i < n; ++i)
            if (i % step != 0)
            {
                const int lo = (i / step) * step, hi = std::min (lo + step, n - 1);
                const float f = hi > lo ? (float) (i - lo) / (float) (hi - lo) : 0.0f;
                curve[(size_t) i] = curve[(size_t) lo] * (1.0f - f) + curve[(size_t) hi] * f;
            }

        if (avgCurve.size() != curve.size())
            avgCurve = curve;
        else
            for (size_t i = 0; i < curve.size(); ++i)
                avgCurve[i] = 0.75f * avgCurve[i] + 0.25f * curve[i];

        // The correlation repeats every bass period, so several peaks can be almost
        // equally good. Take the one closest to zero offset among those within 5 %
        // of the best. A window edge counts as a peak, so when the true best lies
        // outside the allowed window we go as far as allowed and stop.
        const float peakVal = *std::max_element (avgCurve.begin(), avgCurve.end());
        int best = 0, bestDist = 1 << 30;
        for (int i = 0; i < n; ++i)
        {
            const bool isPeak = (i == 0 || avgCurve[(size_t) i] >= avgCurve[(size_t) i - 1])
                             && (i == n - 1 || avgCurve[(size_t) i] >= avgCurve[(size_t) i + 1]);
            const int dist = std::abs (loLag + i);
            if (isPeak && avgCurve[(size_t) i] >= peakVal - 0.05f * std::abs (peakVal) && dist < bestDist)
            { bestDist = dist; best = i; }
        }

        // exact correlation around the coarse peak
        int exactBest = best; float exactVal = -2.0f;
        for (int i = std::max (0, best - step); i <= std::min (n - 1, best + step); ++i)
        {
            const float v = correlationAtLag (loLag + i, maxLag, window);
            if (v > exactVal) { exactVal = v; exactBest = i; }
        }
        best = exactBest;

        float frac = 0.0f;
        if (best > 0 && best < n - 1)
        {
            const float y0 = correlationAtLag (loLag + best - 1, maxLag, window);
            const float y2 = correlationAtLag (loLag + best + 1, maxLag, window);
            const float den = y0 - 2.0f * exactVal + y2;
            if (std::abs (den) > 1.0e-9f)
                frac = std::clamp (0.5f * (y0 - y2) / den, -0.5f, 0.5f);
        }
        bestOffsetMs = (float) (((float) (loLag + best) + frac) * msPerSample);
        valid = true;
    }
    else
    {
        // Zero-cross: put a positive-going bass crossing on the kick's first
        // positive-going crossing after the click (> 3 ms).
        std::vector<float> kc;
        positiveCrossings (k, (int) (3.0 / msPerSample), window, kc);
        if (kc.empty())
            return;
        const float pk = kc.front();

        positiveCrossings (b, 0, (int) b.size(), crossings);
        // Prefer a crossing inside the window (closest to zero); otherwise take
        // the one nearest the window and clamp it to the window edge.
        float bestLag = 0.0f, bestScore = 1.0e9f; bool found = false;
        for (float pb : crossings)
        {
            const float lag = (float) maxLag + pk - pb;
            const float outside = lag < (float) loLag ? (float) loLag - lag : (lag > (float) hiLag ? lag - (float) hiLag : 0.0f);
            const float score = outside > 0.0f ? 1.0e5f + outside : std::abs (lag);
            if (score < bestScore) { bestScore = score; bestLag = std::clamp (lag, (float) loLag, (float) hiLag); found = true; }
        }
        if (! found)
            return;

        const float ms = (float) (bestLag * msPerSample);
        const float halfPeriodMs = fundamentalHz > 0.0f ? 500.0f / fundamentalHz : 5.0f;
        if (! zcHasAverage || std::abs (ms - zcAverageMs) > halfPeriodMs)
            zcAverageMs = ms;
        else
            zcAverageMs = 0.75f * zcAverageMs + 0.25f * ms;
        zcHasAverage = true;
        bestOffsetMs = zcAverageMs;
        valid = true;
    }
}
