#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

// Small DSP building blocks shared by the modules.
namespace pw
{
constexpr float twoPi = 6.28318530718f;

// Zero-delay-feedback state variable filter (Simper/Cytomic).
struct Svf
{
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;

    void set (float cutoff, float q, double sr)
    {
        cutoff = std::clamp (cutoff, 10.0f, (float) (sr * 0.49));
        g = (float) std::tan (3.14159265358979 * cutoff / sr);
        k = 1.0f / std::max (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    struct Out { float lp, bp, hp; };

    Out process (float v0)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return { v2, v1, v0 - k * v1 - v2 };
    }

    void reset() { ic1 = ic2 = 0; }
};

struct OnePole
{
    float z = 0, a = 1;
    void setCutoff (float hz, double sr) { a = 1.0f - (float) std::exp (-twoPi * hz / sr); }
    float process (float x) { z += a * (x - z); return z; }
};

// Exponential decay. "seconds" is the time to fall by 80 dB.
struct DecayEnv
{
    float v = 0, c = 0;

    void trigger (float peak, float seconds, double sr)
    {
        v = peak;
        c = (float) std::exp (-9.21 / (std::max (0.001, (double) seconds) * sr));
    }

    void choke (double sr) { c = (float) std::exp (-9.21 / (0.006 * sr)); }

    float next()
    {
        const float o = v;
        v *= c;
        if (v < 1.0e-5f) v = 0;
        return o;
    }
};

inline float polyBlep (double t, double dt)
{
    if (t < dt)       { t /= dt;             return (float) (t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt;  return (float) (t * t + t + t + 1.0); }
    return 0.0f;
}

// wave: 0 saw, 1 square, 2 triangle, 3 sine. phase in [0,1), dt = freq / sr
inline float oscillator (int wave, double phase, double dt)
{
    switch (wave)
    {
        case 0: return (float) (2.0 * phase - 1.0) - polyBlep (phase, dt);
        case 1:
        {
            float s = phase < 0.5 ? 1.0f : -1.0f;
            s += polyBlep (phase, dt);
            s -= polyBlep (std::fmod (phase + 0.5, 1.0), dt);
            return s;
        }
        case 2: return (float) (4.0 * std::abs (phase - 0.5) - 1.0);
        default: return std::sin (twoPi * (float) phase);
    }
}

struct Noise
{
    uint32_t seed = 22222;
    float next()
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) (int32_t) seed * (1.0f / 2147483648.0f);
    }
};

// Gentle limiter for the master output: clean below 0.9, soft above.
inline float softClip (float x)
{
    const float a = std::abs (x);
    if (a < 0.9f) return x;
    const float y = 0.9f + 0.1f * std::tanh ((a - 0.9f) / 0.1f);
    return x < 0 ? -y : y;
}
} // namespace pw
