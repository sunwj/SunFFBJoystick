#ifndef _MATH_UTILS_H_
#define _MATH_UTILS_H_

#include <stdint.h>
#include <math.h>

__attribute__((weak)) float _sqrt(float number)
{
    long i;
    float y;
    
    y = number;
    i = *(long*)&y;
    i = 0x5f375a86 - (i >> 1);
    y = *(float*)&i;

    return number * y;
}

__attribute__((weak)) float _sinf(float a)
{
    static uint16_t sine_array[65] = {0, 804, 1608, 2411, 3212, 4011, 4808, 5602, 6393,
                                      7180, 7962, 8740, 9512, 10279, 11039, 11793, 12540, 13279,
                                      14010, 14733, 15447, 16151, 16846, 17531, 18205, 18868, 19520,
                                      20160, 20788, 21403, 22006, 22595, 23170, 23732, 24279, 24812,
                                      25330, 25833, 26320, 26791, 27246, 27684, 28106, 28511, 28899,
                                      29269, 29622, 29957, 30274, 30572, 30853, 31114, 31357, 31581,
                                      31786, 31972, 32138, 32286, 32413, 32522, 32610, 32679, 32729,
                                      32758, 32768};
    int32_t t1, t2;
    uint32_t i = uint32_t(a * (64 * 4 * 256.0f / (M_PI * 2)));
    int frac = i & 0xff;
    i = (i >> 8) & 0xff;
    if (i < 64)
    {
        t1 = (int32_t)sine_array[i];
        t2 = (int32_t)sine_array[i + 1];
    }
    else if (i < 128)
    {
        t1 = (int32_t)sine_array[128 - i];
        t2 = (int32_t)sine_array[127 - i];
    }
    else if (i < 192)
    {
        t1 = -(int32_t)sine_array[-128 + i];
        t2 = -(int32_t)sine_array[-127 + i];
    }

    else
    {
        t1 = -(int32_t)sine_array[256 - i];
        t2 = -(int32_t)sine_array[255 - i];
    }

    return (1.0f / 32768.0f) * (t1 + (((t2 - t1) * frac) >> 8));
}

__attribute__((weak)) float _cosf(float a)
{
    float a_sin = a + M_PI_2;
    a_sin = a_sin > (M_PI * 2) ? a_sin - (M_PI * 2) : a_sin;
    return _sinf(a_sin);
}

template <typename T>
inline T normalize_angle(T angle)
{
    angle = fmod(angle, M_PI * 2);
    angle = angle < 0 ? (angle + M_PI * 2) : angle;

    return angle;
}

template <typename T>
inline T clamp(T value, T minValue, T maxValue)
{
    return (value < minValue) ? minValue : ((value > maxValue) ? maxValue : value);
}

#endif