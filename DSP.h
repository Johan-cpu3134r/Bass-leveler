#pragma once

#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace bl
{
constexpr double pi = 3.14159265358979323846;

inline float dbToGain (float db)  { return std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g)   { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float freqToMidi (float f) { return 69.0f + 12.0f * std::log2 (f / 440.0f); }
inline float midiToFreq (float m) { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }

//==============================================================================
/** Cytomic / TPT state-variable filter. Safe to modulate every sample. */
class Svf
{
public:
    void reset() { ic1 = ic2 = 0.0f; }

    /** Peaking bell. */
    void setBell (double fs, float freq, float q, float gainDb)
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float g = (float) std::tan (pi * std::clamp ((double) freq, 5.0, fs * 0.45) / fs);
        const float k = 1.0f / (q * A);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        m0 = 1.0f; m1 = k * (A * A - 1.0f); m2 = 0.0f;
    }

    /** Constant-peak bandpass (unity gain at centre). */
    void setBandpass (double fs, float freq, float q)
    {
        const float g = (float) std::tan (pi * std::clamp ((double) freq, 5.0, fs * 0.45) / fs);
        const float k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        m0 = 0.0f; m1 = k; m2 = 0.0f;
    }

    /** 2-pole lowpass with the given Q (0.7071 = Butterworth). */
    void setLowpass (double fs, float freq, float q)
    {
        const float g = (float) std::tan (pi * std::clamp ((double) freq, 5.0, fs * 0.45) / fs);
        const float k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        m0 = 0.0f; m1 = 0.0f; m2 = 1.0f;
    }

    inline float process (float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return m0 * v0 + m1 * v1 + m2 * v2;
    }

private:
    float a1 = 0, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;
    float ic1 = 0, ic2 = 0;
};

//==============================================================================
/** YIN pitch detector working on a (decimated) mono buffer. */
class Yin
{
public:
    struct Result { float freq = 0.0f; float confidence = 0.0f; };

    void prepare (int maxTau)
    {
        diff.assign ((size_t) maxTau + 2, 0.0f);
    }

    /** x must hold at least 2 * tauMax samples, oldest first. */
    Result detect (const float* x, int tauMin, int tauMax, double fs, float threshold = 0.15f)
    {
        Result r;
        const int W = tauMax;
        tauMax = std::min (tauMax, (int) diff.size() - 2);
        tauMin = std::max (tauMin, 2);
        if (tauMax <= tauMin + 2) return r;

        // Difference function
        diff[0] = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float s = 0.0f;
            for (int i = 0; i < W; ++i)
            {
                const float d = x[i] - x[i + tau];
                s += d * d;
            }
            diff[(size_t) tau] = s;
        }

        // Cumulative mean normalised difference
        float running = 0.0f;
        diff[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += diff[(size_t) tau];
            diff[(size_t) tau] = running > 0.0f ? diff[(size_t) tau] * (float) tau / running : 1.0f;
        }

        // Absolute threshold: first dip below threshold, then walk to its minimum
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (diff[(size_t) tau] < threshold)
            {
                while (tau + 1 < tauMax && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }

        // Fallback: global minimum (reported with low confidence)
        if (best < 0)
        {
            best = tauMin;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (diff[(size_t) tau] < diff[(size_t) best]) best = tau;
        }

        // Parabolic interpolation
        float betterTau = (float) best;
        if (best > 1 && best < tauMax)
        {
            const float s0 = diff[(size_t) best - 1], s1 = diff[(size_t) best], s2 = diff[(size_t) best + 1];
            const float denom = 2.0f * (2.0f * s1 - s2 - s0);
            if (std::abs (denom) > 1.0e-9f)
                betterTau += (s2 - s0) / denom;
        }

        r.freq = (float) (fs / betterTau);
        r.confidence = std::clamp (1.0f - diff[(size_t) best], 0.0f, 1.0f);
        return r;
    }

private:
    std::vector<float> diff;
};

//==============================================================================
/** Tiny median filter for killing octave-jump outliers. */
template <int N>
class Median
{
public:
    void reset (float v) { buf.fill (v); idx = 0; }
    float push (float v)
    {
        buf[(size_t) idx] = v;
        idx = (idx + 1) % N;
        auto tmp = buf;
        std::nth_element (tmp.begin(), tmp.begin() + N / 2, tmp.end());
        return tmp[(size_t) (N / 2)];
    }
private:
    std::array<float, N> buf {};
    int idx = 0;
};

} // namespace bl
