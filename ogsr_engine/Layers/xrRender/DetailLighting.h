#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// CPU-only grass-cache helpers. Keep independent of renderer state so the
// interpolation, edge handling and degenerate cases can be regression-tested.
namespace detail_lighting
{
struct Normal
{
    float x{}, y{}, z{};
};

inline Normal normalize(Normal n, Normal fallback = {})
{
    const float length2 = n.x * n.x + n.y * n.y + n.z * n.z;
    if (!(length2 > 1e-12f) || !std::isfinite(length2))
        return fallback;
    const float scale = 1.f / std::sqrt(length2);
    return {n.x * scale, n.y * scale, n.z * scale};
}

inline Normal smooth_normal(Normal face, std::array<Normal, 3> vertices, float u, float v)
{
    // Do not round cliffs/undersides into the grass-bearing surface. A zero
    // cached normal denotes a vertex without eligible upward-facing geometry.
    for (auto& n : vertices)
    {
        if (n.x * face.x + n.y * face.y + n.z * face.z < 0.8660254f)
            n = face;
    }
    const float w = 1.f - u - v;
    return normalize({vertices[0].x * w + vertices[1].x * u + vertices[2].x * v,
                         vertices[0].y * w + vertices[1].y * u + vertices[2].y * v,
                         vertices[0].z * w + vertices[1].z * u + vertices[2].z * v}, face);
}

struct HemiSample
{
    float value{}, min_y{}, max_y{};
    bool valid{};
};

inline float sample_hemi(const std::array<HemiSample, 4>& samples, float x, float z, float y, float fallback)
{
    const float weights[]{(1.f - x) * (1.f - z), x * (1.f - z), (1.f - x) * z, x * z};
    float sum = 0.f, weight = 0.f;
    for (unsigned i = 0; i < samples.size(); ++i)
    {
        const auto& s = samples[i];
        // Reject absent cells and disconnected heights (e.g. a bridge next to
        // the ground below it). Renormalize rather than importing black edges.
        if (!s.valid || y < s.min_y - 0.5f || y > s.max_y + 0.5f)
            continue;
        sum += s.value * weights[i];
        weight += weights[i];
    }
    return std::clamp(weight > 1e-6f ? sum / weight : fallback, 0.05f, 1.f);
}
} // namespace detail_lighting
