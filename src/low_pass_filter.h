#ifndef _LOW_PASS_FILTER_H_
#define _LOW_PASS_FILTER_H_

#include <Arduino.h>

class LowPassFilter
{
public:
    LowPassFilter() {};

    inline void set_cutoff_frequency(float cutOffFreq)
    {
        if (cutOffFreq > 0.f)
            tF = 1.f / (2.f * float(M_PI) * cutOffFreq);
    }

    inline void set_time_constant(float tF)
    {
        this->tF = (tF > 0.f) ? tF : 0.f;
    }

    inline float operator()(float x, float dt = -1)
    {
        // if sample interval is not provided, calculate it on the fly
        if (dt < 0.f)
        {
            uint32_t now = micros();
            dt = (now - tPrev) * 1e-6f;
            if (dt > 0.3f)
            {
                output = x;
                tPrev = now;
                return output;
            }
            tPrev = now;
        }

        const float alpha = dt / (tF + dt);
        output += alpha * (x - output);
        return output;
    }

    inline void reset(float value = 0.f)
    {
        output = value;
        tPrev = micros();
    }

private:
    float tF = 0;
    float output = 0;
    uint32_t tPrev = 0;
};

#endif