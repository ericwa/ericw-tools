
#pragma once

namespace color
{

inline float linear_to_srgb(float x)
{
    if (x <= 0.0031308f) {
        return 12.92f * x;
    } else {
        return (1.0f + 0.055f) * pow(x, 1.0f / 2.4f) - 0.055f;
    }
}

inline float srgb_to_linear(float x)
{
    if (x <= 0.04045f) {
        return x / 12.92f;
    } else {
        return pow((x + 0.055f) / (1.0f + 0.055f), 2.4f);
    }
}

// vector helpers
// these will truncate to floats if doubles are used as input

template<typename T>
[[nodiscard]] inline qvec<T, 3> linear_to_srgb(qvec<T, 3> v)
{
    for (int i = 0; i < 3; ++i) {
        v[i] = linear_to_srgb(v[i]);
    }
    return v;
}

template<typename T>
[[nodiscard]] inline qvec<T, 3> srgb_to_linear(qvec<T, 3> v)
{
    for (int i = 0; i < 3; ++i) {
        v[i] = srgb_to_linear(v[i]);
    }
    return v;
}

} // namespace color
