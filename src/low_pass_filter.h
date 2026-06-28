#ifndef _LOW_PASS_FILTER_H_
#define _LOW_PASS_FILTER_H_

#include <Arduino.h>

// First-order low-pass filter (exponential moving average).
// Supports explicit-dt (branchless) and auto-dt (timekeeping) modes.
class LowPassFilter
{
public:
    LowPassFilter() {};

    // Set cutoff frequency in Hz. Converts to time constant internally.
    inline void set_cutoff_frequency(float cutOffFreq)
    {
        if (cutOffFreq > 0.f)
            tF = 1.f / (2.f * float(M_PI) * cutOffFreq);
    }

    // Set filter time constant directly (seconds).
    inline void set_time_constant(float tF)
    {
        this->tF = (tF > 0.f) ? tF : 0.f;
    }

    // Return current time constant (seconds).
    inline float get_time_constant() const
    {
        return tF;
    }

    // Filter with pre-computed alpha (no internal division).
    inline float filter_alpha(float x, float alpha)
    {
        output += alpha * (x - output);
        return output;
    }

    // Filter step with explicit dt. Branchless hot path.
    inline float operator()(float x, float dt)
    {
        const float alpha = dt / (tF + dt);
        output += alpha * (x - output);
        return output;
    }

    // Filter step with automatic dt (micros() timekeeping).
    // Resets on gap > 0.3s to avoid spike after long idle.
    inline float operator()(float x)
    {
        uint32_t now = micros();
        float dt = (now - tPrev) * 1e-6f;
        if (dt > 0.3f)
        {
            output = x;
            tPrev = now;
            return output;
        }
        tPrev = now;

        const float alpha = dt / (tF + dt);
        output += alpha * (x - output);
        return output;
    }

    // Reset filter state. Optionally set initial output value.
    inline void reset(float value = 0.f)
    {
        output = value;
        tPrev = micros();
    }

private:
    float tF = 0;       // time constant (seconds)
    float output = 0;   // current filter output
    uint32_t tPrev = 0; // previous timestamp (micros, for auto-dt mode)
};

#endif