#include "ogsr_rsm_common.h"

Texture2D<float4> gi_position : register(t0);
Texture2D<float4> gi_albedo : register(t1);
Texture2D<float2> gi_capture : register(t2);
Texture2D<float> gi_depth : register(t3);
RWTexture2D<float4> gi_noisy : register(u0);
RWTexture2D<float4> gi_geometry : register(u1);
RWTexture2D<float4> gi_diagnostics : register(u2);

float3 RSMNormal(float packed)
{
    uint bits = (uint)round(packed);
    return GIDecodeNormal(float2(bits & 1023, bits >> 10) / 1023.0);
}

// Finite slabs in both available depth views. They can detect visible blockers,
// but cannot prove visibility through geometry hidden from both cameras.
bool RSMVisible(float3 start, float3 end, uint steps)
{
    [loop] for (uint i = 1; i <= steps; ++i)
    {
        float3 rayPosition = lerp(start, end, (float)i / (steps + 1));
        float3 light = mul(float4(rayPosition, 1), gi_world_to_light).xyz;
        float2 uv = light.xy * float2(0.5, -0.5) + 0.5;
        if (all(uv > 0) && all(uv < 1))
        {
            float depth = gi_depth.Load(int3(int2(uv * gi_map.x), 0));
            float behind = (light.z - depth) / max(gi_map.w, 1e-8);
            if (depth < 1.0 && behind > 0.03 && behind < gi_trace.y)
                return false;
        }
        float3 view = mul(float4(rayPosition, 1), gi_world_to_view).xyz;
        if (view.z <= GI_MIN_DEPTH)
            continue;
        float2 cameraUV = (view.xy / view.z - gi_ndc.zw) / gi_ndc.xy;
        if (all(cameraUV > 0) && all(cameraUV < 1))
        {
            float depth = gi_position.Load(int3(int2(cameraUV * gi_full.xy), 0)).z;
            float behind = view.z - depth;
            if (depth > GI_MIN_DEPTH && behind > 0.03 && behind < gi_trace.y)
                return false;
        }
    }
    return true;
}

// Single-bounce reflective shadow map integration (Dachsbacher/Stamminger 2005).
// Uniform disk sampling is normalized by projected world area, independent of
// shadow-map resolution. Output is irradiance/pi, before receiving albedo.
[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchId.xy;
    if (any(pixel >= uint2(gi_half.xy)))
        return;
    int2 fullPixel = int2(pixel * 2);
    uint selected = 0;
    float nearest = 1e20;
    [unroll] for (uint j = 0; j < 4; ++j)
    {
        int2 p = min(int2(pixel * 2) + int2(j & 1, j >> 1), int2(gi_full.xy) - 1);
        float depth = gi_position.Load(int3(p, 0)).z;
        if (depth > 0.001 && depth < nearest)
        {
            nearest = depth;
            fullPixel = p;
            selected = j;
        }
    }
    float4 packed = gi_position.Load(int3(fullPixel, 0));
    float3 N = GIWorldNormal(packed.xy);
    gi_noisy[pixel] = float4(0, 0, 0, selected);
    gi_geometry[pixel] = float4(N, packed.z);
    gi_diagnostics[pixel] = 0;
    if (packed.z < GI_MIN_DEPTH || packed.z > 9999)
        return;

    float3 P = mul(float4(GIPosition(float2(fullPixel) + 0.5, packed.z), 1), gi_inverse_view).xyz;
    float3 projected = mul(float4(P, 1), gi_world_to_light).xyz;
    float2 centerUV = projected.xy * float2(0.5, -0.5) + 0.5;
    // Fade before reaching the capture boundary; never clamp rays onto it.
    float edge = min(min(centerUV.x, centerUV.y), min(1-centerUV.x, 1-centerUV.y));
    if (edge <= 0 || projected.z < 0 || projected.z > 1)
        return;

    uint count = gi_trace.w < 1.5 ? 16 : (gi_trace.w < 2.5 ? 32 : 64);
    uint steps = gi_trace.w < 1.5 ? 8 : (gi_trace.w < 2.5 ? 12 : 16);
    float rotation = GIHash(float2(pixel) + gi_trace.z * 5.588238) * (2 * GI_PI);
    float3 bounce = 0;
    uint candidates = 0, rejected = 0;
    [loop] for (uint i = 0; i < count; ++i)
    {
        float angle = i * 2.39996323 + rotation;
        float2 offset = float2(cos(angle), sin(angle)) * sqrt((i + 0.5) / count);
        float2 uv = centerUV + offset * (gi_trace.x / gi_map.z);
        if (any(uv <= 0) || any(uv >= 1))
            continue;
        int2 samplePixel = int2(uv * gi_map.x);
        float4 albedo = gi_albedo.Load(int3(samplePixel, 0));
        float2 capture = gi_capture.Load(int3(samplePixel, 0));
        float depth = gi_depth.Load(int3(samplePixel, 0));
        // A later depth-only caster (e.g. grass) must not inherit the emitter
        // underneath it. Preserve float32 capture depth for this comparison.
        if (albedo.a < 0.5 || depth >= 1 || abs(depth - capture.y) > 0.0000003)
            continue;
        float2 sampleUV = (float2(samplePixel) + 0.5) * gi_map.y;
        float3 Q = mul(float4(sampleUV * float2(2, -2) + float2(-1, 1), depth, 1), gi_light_to_world).xyz;
        float3 emitterNormal = RSMNormal(capture.x);
        float3 delta = Q - P;
        float distanceSquared = dot(delta, delta);
        if (distanceSquared < 0.01 || distanceSquared > gi_trace.x * gi_trace.x)
            continue;
        float3 direction = delta * rsqrt(distanceSquared);
        float cosine = saturate(dot(N, direction)) * saturate(dot(emitterNormal, -direction));
        if (cosine <= 0)
            continue;
        ++candidates;
        if (!RSMVisible(P + N * 0.04, Q + emitterNormal * 0.04, steps))
        {
            ++rejected;
            continue;
        }
        float fade = 1 - smoothstep(gi_trace.x * 0.8, gi_trace.x, sqrt(distanceSquared));
        bounce += GILinear(albedo.rgb) * cosine * fade / max(distanceSquared, 0.09);
    }
    bounce *= gi_sun.rgb * (gi_trace.x * gi_trace.x / (GI_PI * count));
    bounce *= saturate(edge * gi_map.z / max(0.5, gi_trace.x)) * smoothstep(GI_MIN_DEPTH, 1.0, packed.z);
    gi_noisy[pixel] = float4(bounce, selected);
    // Compare accepted/rejected links on the same scale. Dividing green by the
    // whole sample budget made sparse but valid bounce look completely rejected.
    gi_diagnostics[pixel] = float4((float)rejected / max(1u, candidates), (float)(candidates-rejected) / max(1u, candidates), (float)candidates / count, 1);
}
