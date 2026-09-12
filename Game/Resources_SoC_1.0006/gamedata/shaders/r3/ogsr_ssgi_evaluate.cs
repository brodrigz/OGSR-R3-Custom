#include "ogsr_ssgi_common.h"

Texture2D<float4> gi_position : register(t0);
Texture2D<float4> gi_source : register(t1);
Texture2D<float4> gi_albedo : register(t2);
RWTexture2D<float4> gi_noisy : register(u0);
RWTexture2D<float4> gi_geometry : register(u1);

// Original implementation of the visibility-bitmask approach described by
// Therrien et al., Screen Space Indirect Lighting with Visibility Bitmask (2023).
// https://cdrinmatane.github.io/posts/ssaovb-code/
// Uses cosine/solid-angle sector weights and diffuse/emissive source radiance.
[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchId.xy;
    if (any(pixel >= uint2(gi_half.xy)))
        return;

    int2 fullPixel = int2(pixel * 2);
    uint selected = 0;
    float nearest = 1e20;
    [unroll] for (uint i = 0; i < 4; ++i)
    {
        int2 candidate = min(int2(pixel * 2) + int2(i & 1, i >> 1), int2(gi_full.xy) - 1);
        float depth = gi_position.Load(int3(candidate, 0)).z;
        // Keep the closest surface even if it is HUD, so we never fill it with
        // a farther surface's GI at a silhouette.
        if (depth > 0.001 && depth < nearest)
        {
            nearest = depth;
            fullPixel = candidate;
            selected = i;
        }
    }
    float4 packed = gi_position.Load(int3(fullPixel, 0));
    gi_noisy[pixel] = float4(0, 0, 0, selected);
    gi_geometry[pixel] = float4(GIWorldNormal(packed.xy), packed.z);
    if (packed.z < GI_MIN_DEPTH || packed.z > 9999.0)
        return;

    float3 P = GIPosition(float2(fullPixel) + 0.5, packed.z);
    float3 N = GIDecodeNormal(packed.xy);
    float3 V = normalize(-P);
    // Small normal bias prevents same-surface self-lighting.
    float3 origin = P + N * 0.02;
    uint slices = gi_trace.w < 1.5 ? 1 : (gi_trace.w < 2.5 ? 2 : 3);
    uint steps = gi_trace.w < 1.5 ? 8 : (gi_trace.w < 2.5 ? 10 : 12);
    float noise = GIHash(float2(pixel) + gi_trace.z * float2(5.588238, 5.588238));
    // A pixel cap changes the world-space reach with resolution/FOV. The
    // number of taps already bounds the work; out-of-screen taps are rejected.
    float radiusPixels = gi_trace.x * gi_full.y / (abs(gi_ndc.y) * P.z);
    float3 indirect = 0;

    [loop] for (uint slice = 0; slice < slices; ++slice)
    {
        float angle = (slice + noise) * GI_PI / slices;
        float2 screenDirection = float2(cos(angle), sin(angle));
        float3 planeDirection = float3(screenDirection * gi_ndc.xy * gi_full.zw, 0);
        float3 T = normalize(planeDirection - V * dot(planeDirection, V));
        float nV = dot(N, V), nT = dot(N, T);
        float normalLength = sqrt(nV * nV + nT * nT);
        if (normalLength < 0.001)
            continue;
        float normalAngle = atan2(nT, nV);
        uint occupied = 0;

        [loop] for (uint stepIndex = 0; stepIndex < steps; ++stepIndex)
        {
            float fraction = (stepIndex + 0.5 + noise * 0.5) / steps;
            float offset = max(1.5, radiusPixels * fraction * fraction);
            [unroll] for (int side = -1; side <= 1; side += 2)
            {
                int2 samplePixel = int2(floor(float2(fullPixel) + 0.5 + screenDirection * (side * offset)));
                // Never clamp off-screen rays onto the border.
                if (any(samplePixel < 0) || any(samplePixel >= int2(gi_full.xy)))
                    continue;
                float4 samplePacked = gi_position.Load(int3(samplePixel, 0));
                if (samplePacked.z < GI_MIN_DEPTH || samplePacked.z > 9999.0)
                    continue;
                float3 samplePosition = GIPosition(float2(samplePixel) + 0.5, samplePacked.z);
                float3 delta = samplePosition - origin;
                float distanceSquared = dot(delta, delta);
                if (distanceSquared < 0.0004 || distanceSquared > gi_trace.x * gi_trace.x)
                    continue;
                // Rounded screen taps need not lie exactly in the slice plane.
                // Reject the actual receiving hemisphere before quantizing an
                // angular interval, otherwise a flat wall can light itself.
                if (dot(N, delta) <= 0.0)
                    continue;
                float3 direction = delta * rsqrt(distanceSquared);
                float3 back = delta - V * gi_trace.y;
                float frontAngle = side * acos(clamp(dot(direction, V), -1.0, 1.0));
                float backAngle = side * acos(clamp(dot(back, V) * rsqrt(max(dot(back, back), 1e-8)), -1.0, 1.0));
                float low = saturate((min(frontAngle, backAngle) - normalAngle) / GI_PI + 0.5);
                float high = saturate((max(frontAngle, backAngle) - normalAngle) / GI_PI + 0.5);
                uint covered = GISectorMask(low, high);
                uint visible = covered & ~occupied;
                occupied |= covered; // Back-facing/unlit surfaces still block later samples.
                if (visible == 0 || dot(GIDecodeNormal(samplePacked.xy), -direction) <= 0.0)
                    continue;

                float4 source = gi_source.Load(int3(samplePixel, 0));
                float3 radiance = max(0.0, source.rgb + source.a * GILinear(gi_albedo.Load(int3(samplePixel, 0)).rgb));
                // Limit pathological emissive outliers before temporal accumulation.
                radiance *= min(1.0, 32.0 / max(0.001, max(radiance.r, max(radiance.g, radiance.b))));
                float distanceFade = 1.0 - smoothstep(gi_trace.x * 0.75, gi_trace.x, sqrt(distanceSquared));
                float2 edge = min(float2(samplePixel) + 0.5, gi_full.xy - float2(samplePixel) - 0.5) * gi_full.zw;
                float edgeFade = saturate(min(edge.x, edge.y) * 20.0);
                indirect += radiance * GISectorWeight(visible, normalAngle, normalLength) * distanceFade * edgeFade;
            }
        }
    }
    indirect *= smoothstep(GI_MIN_DEPTH, 1.0, P.z) / slices;
    gi_noisy[pixel] = float4(indirect, selected);
}
