#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <vector>

// Sidechain gain curves shared by the DSP and the editor.
// Each shape maps x in [0,1] of the release phase to a recovery amount:
// 0 = fully ducked, 1 = fully recovered.
namespace slope
{
    enum Shape
    {
        // row 1: recoveries
        Linear = 0, Exponential, Logarithmic, SCurve, LateS, EarlyS, SoftSquare, HardKnee,
        // row 2: rhythmic / non-monotonic
        Pump, Steps, Gate, Bell, DoubleBump, Dip, Wobble, Fade,
        Custom,
        NumShapes
    };

    constexpr int numPresets = Custom;
    // The custom curve reaches the audio thread as a lookup table.
    constexpr int numCustomPoints = 256;
    using CustomCurve = std::array<float, numCustomPoints>;

    //==========================================================================
    // FL-style point editor: points joined by segments, each segment with its
    // own curve type and tension. Point x/y are in [0,1]: x = release time,
    // y = recovery (0 = fully ducked, 1 = full level).
    enum SegmentType { SingleCurve = 0, DoubleCurve, Hold, Stairs, SmoothStairs, Pulse, Wave, NumSegmentTypes };

    inline const char* const segmentNames[NumSegmentTypes] =
        { "Single curve", "Double curve (S)", "Hold", "Stairs", "Smooth stairs", "Pulse", "Wave" };

    struct Node
    {
        float x = 0.0f, y = 0.0f;
        int type = SingleCurve;   // shape of the segment from this point to the next
        float tension = 0.0f;     // -1 .. 1
    };
    using Nodes = std::vector<Node>;

    inline bool segmentHasTension (int type) noexcept { return type != Hold; }

    // 0..1 progress from the segment's start value to its end value
    inline float segmentProgress (int type, float tension, float t) noexcept
    {
        constexpr float pi = 3.14159265358979f;
        t = std::clamp (t, 0.0f, 1.0f);
        const float k = std::clamp (tension, -1.0f, 1.0f);
        auto count = [k] (int lo, int hi) { return lo + (int) std::round ((k + 1.0f) * 0.5f * (float) (hi - lo)); };

        switch (type)
        {
            case SingleCurve:
            {
                const float e = std::pow (8.0f, -k);           // k > 0: fast start, k < 0: slow start
                return std::pow (t, e);
            }
            case DoubleCurve:
            {
                const float e = std::pow (6.0f, k);            // k > 0: sharper S, k < 0: reversed S
                return t < 0.5f ? 0.5f * std::pow (2.0f * t, e)
                                : 1.0f - 0.5f * std::pow (2.0f - 2.0f * t, e);
            }
            case Hold:         return t < 1.0f ? 0.0f : 1.0f;
            case Stairs:
            {
                const int n = count (2, 16);
                return std::min (1.0f, std::floor (t * (float) n) / (float) (n - 1));
            }
            case SmoothStairs:
            {
                const int n = count (2, 16);
                const float p = t * (float) n;
                const float i = std::floor (p), f = p - i;
                const float s = std::clamp ((f - 0.5f) / 0.5f, 0.0f, 1.0f);
                return std::min (1.0f, (i + s * s * (3.0f - 2.0f * s)) / (float) n);
            }
            case Pulse:
            {
                const int n = count (1, 16);
                return t >= 1.0f || std::fmod (t * ((float) n + 0.5f), 1.0f) >= 0.5f ? 1.0f : 0.0f;   // ends on the target
            }
            case Wave:
            {
                const int n = count (1, 16);
                return 0.5f - 0.5f * std::cos (2.0f * pi * ((float) n + 0.5f) * t);           // ends on the target
            }
            default: return t;
        }
    }

    inline float evaluate (const Nodes& nodes, float x) noexcept
    {
        if (nodes.empty()) return x;
        if (x <= nodes.front().x) return nodes.front().y;
        for (size_t i = 0; i + 1 < nodes.size(); ++i)
        {
            const auto& a = nodes[i];
            const auto& b = nodes[i + 1];
            if (x <= b.x)
            {
                const float t = b.x > a.x ? (x - a.x) / (b.x - a.x) : 1.0f;
                return std::clamp (a.y + (b.y - a.y) * segmentProgress (a.type, a.tension, t), 0.0f, 1.0f);
            }
        }
        return nodes.back().y;
    }

    inline CustomCurve toTable (const Nodes& nodes)
    {
        CustomCurve c {};
        for (int i = 0; i < numCustomPoints; ++i)
            c[(size_t) i] = evaluate (nodes, (float) i / (float) (numCustomPoints - 1));
        return c;
    }

    inline Nodes defaultNodes()
    {
        return { { 0.0f, 0.0f, SingleCurve, 0.6f }, { 1.0f, 1.0f, SingleCurve, 0.0f } };
    }

    inline const char* const names[NumShapes] =
    {
        "Linear", "Exponential", "Logarithmic", "S-curve", "Late S", "Early S", "Soft square", "Hard knee",
        "Pump", "Steps", "Gate", "Bell", "Double bump", "Dip", "Wobble", "Fade",
        "Custom"
    };

    inline float smoothstep (float a, float b, float x) noexcept
    {
        const float t = std::clamp ((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    inline float shape (int s, float x, const CustomCurve& custom) noexcept
    {
        constexpr float pi = 3.14159265358979f;
        x = std::clamp (x, 0.0f, 1.0f);
        switch (s)
        {
            case Linear:      return x;
            case Exponential: return (1.0f - std::exp (-5.0f * x)) / (1.0f - std::exp (-5.0f));
            case Logarithmic: return std::pow (x, 2.4f);
            case SCurve:      return smoothstep (0.0f, 1.0f, x);
            case LateS:       return smoothstep (0.4f, 1.0f, x);
            case EarlyS:      return smoothstep (0.0f, 0.45f, x);
            case SoftSquare:  return smoothstep (0.08f, 0.3f, x);
            case HardKnee:    return smoothstep (0.55f, 0.65f, x);
            case Pump:
            {
                if (x < 0.35f) return 0.0f;
                const float y = 1.0f - (x - 0.35f) / 0.65f;
                return 1.0f - y * y * y;
            }
            case Steps:       return std::min (1.0f, std::floor (x * 4.0f + 0.0001f) / 3.0f);
            case Gate:        return smoothstep (0.05f, 0.2f, x) * (1.0f - smoothstep (0.75f, 0.9f, x));
            case Bell:        { const float v = std::sin (pi * x); return v * v; }
            case DoubleBump:  return 0.5f - 0.5f * std::cos (4.0f * pi * x);
            case Dip:         return smoothstep (0.0f, 0.25f, x) * (1.0f - 0.6f * std::exp (-std::pow ((x - 0.6f) / 0.09f, 2.0f)));
            case Wobble:      return std::clamp (x + 0.18f * std::sin (6.0f * pi * x), 0.0f, 1.0f);
            case Fade:        return smoothstep (0.0f, 0.15f, x) * (1.0f - 0.85f * smoothstep (0.3f, 1.0f, x));
            default:
            {
                const float p = x * (float) (numCustomPoints - 1);
                const int i = (int) p;
                if (i >= numCustomPoints - 1) return custom.back();
                const float f = p - (float) i;
                return custom[(size_t) i] * (1.0f - f) + custom[(size_t) i + 1] * f;
            }
        }
    }

    // Gain applied to the bass t milliseconds after a kick.
    inline float gainAt (float tMs, float attackMs, float releaseMs, float depth01, int s, const CustomCurve& custom) noexcept
    {
        if (tMs < attackMs)
            return 1.0f - depth01 * (tMs / attackMs);
        const float x = std::min (1.0f, (tMs - attackMs) / releaseMs);
        return 1.0f - depth01 * (1.0f - shape (s, x, custom));
    }

    // A preset turned into editable points (straight segments between samples).
    inline Nodes presetAsNodes (int s)
    {
        const CustomCurve none {};
        Nodes n;
        constexpr int count = 9;
        for (int i = 0; i < count; ++i)
        {
            const float x = (float) i / (float) (count - 1);
            n.push_back ({ x, shape (s, x, none), SingleCurve, 0.0f });
        }
        return n;
    }
}
