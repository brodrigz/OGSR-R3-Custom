#ifndef OGSR_RSM_COMMON_H
#define OGSR_RSM_COMMON_H

// Standalone SM5 compute interface. Do not include common.h's graphics cbuffers.
cbuffer RSMConstants : register(b0)
{
    float4 gi_full;
    float4 gi_half;
    float4 gi_ndc;
    float4 gi_trace; // radius (m), assumed surface thickness (m), noise frame, quality
    float4 gi_history; // reset, previous projection jitter.xy, reserved
    float4 gi_jitter; // current projection jitter.xy
    row_major float4x4 gi_inverse_view;
    row_major float4x4 gi_current_to_previous_view;
    float4 gi_map; // capture width, reciprocal width, world-space width, light depth/metre
    float4 gi_sun; // linear sun irradiance.rgb
    row_major float4x4 gi_world_to_light;
    row_major float4x4 gi_light_to_world;
    row_major float4x4 gi_world_to_view;
};

static const float GI_PI = 3.14159265359;
// HUD uses a different projection. Conservatively exclude near geometry from
// both sending and receiving GI; this also excludes world surfaces this close.
static const float GI_MIN_DEPTH = 0.6;

float3 GIPosition(float2 pixel, float depth)
{
    return float3((pixel * gi_full.zw * gi_ndc.xy + gi_ndc.zw) * depth, depth);
}

float3 GIDecodeNormal(float2 packed)
{
    float2 f = packed * 2.0 - 1.0;
    float3 n = float3(f, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += float2(n.x >= 0 ? -t : t, n.y >= 0 ? -t : t);
    return normalize(n);
}

float3 GIWorldNormal(float2 packed)
{
    return normalize(mul(float4(GIDecodeNormal(packed), 0), gi_inverse_view).xyz);
}

float3 GILinear(float3 c)
{
    // Matches the renderer's SRGBToLinear conversion (srgb.h).
    return pow(max(c, 0.0), 2.2);
}

float GIGeometryWeight(float4 center, float4 sampleGeometry)
{
    if (sampleGeometry.w < GI_MIN_DEPTH)
        return 0;
    float normalWeight = pow(saturate(dot(center.xyz, sampleGeometry.xyz)), 32.0);
    float depthWeight = exp2(-abs(center.w - sampleGeometry.w) / max(0.02, center.w * 0.02));
    return normalWeight * depthWeight;
}

float GIHash(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

#endif
