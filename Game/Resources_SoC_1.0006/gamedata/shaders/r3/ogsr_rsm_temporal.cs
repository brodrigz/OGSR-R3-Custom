#include "ogsr_rsm_common.h"

Texture2D<float4> gi_noisy : register(t0);
Texture2D<float4> gi_geometry : register(t1);
Texture2D<float4> gi_previous : register(t2);
Texture2D<float4> gi_previous_geometry : register(t3);
Texture2D<float2> gi_velocity : register(t4);
RWTexture2D<float4> gi_output : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchId.xy);
    if (any(pixel >= int2(gi_half.xy)))
        return;
    float4 current = gi_noisy.Load(int3(pixel, 0));
    float4 geometry = gi_geometry.Load(int3(pixel, 0));
    if (geometry.w < GI_MIN_DEPTH || geometry.w > 9999.0)
    {
        gi_output[pixel] = 0;
        return;
    }
    uint selected = (uint)current.a;
    int2 fullPixel = min(pixel * 2 + int2(selected & 1, selected >> 1), int2(gi_full.xy) - 1);
    float2 velocity = gi_velocity.Load(int3(fullPixel, 0)); // current minus previous NDC, unjittered
    float2 uv = (float2(fullPixel) + 0.5) * gi_full.zw;
    float2 previousUV = uv - velocity * float2(0.5, -0.5) + (gi_history.yz - gi_jitter.xy) * float2(0.5, -0.5);
    float3 position = GIPosition(float2(fullPixel) + 0.5, geometry.w);
    float expectedDepth = mul(float4(position, 1), gi_current_to_previous_view).z;
    float4 previous = 0;
    float totalWeight = 0;
    if (gi_history.x < 0.5 && expectedDepth >= GI_MIN_DEPTH && all(previousUV > 0) && all(previousUV < 1))
    {
        // Use physical 2x2 footprints, including odd internal dimensions.
        float2 previousPixel = previousUV * gi_full.xy * 0.5 - 0.5;
        int2 base = int2(floor(previousPixel));
        [unroll] for (int y = 0; y < 2; ++y)
        {
            [unroll] for (int x = 0; x < 2; ++x)
            {
                int2 tap = base + int2(x, y);
                if (any(tap < 0) || any(tap >= int2(gi_half.xy)))
                    continue;
                float4 oldGeometry = gi_previous_geometry.Load(int3(tap, 0));
                float depthTolerance = max(0.05, expectedDepth * 0.015);
                if (oldGeometry.w < GI_MIN_DEPTH || abs(oldGeometry.w - expectedDepth) > depthTolerance || dot(oldGeometry.xyz, geometry.xyz) < 0.9)
                    continue;
                float4 old = gi_previous.Load(int3(tap, 0));
                // History alpha stores integer age + the selected 2x2 offset
                // in quarter units. Weight the actual surface sample location,
                // not the block center, to avoid drifting/blurring stationary GI.
                uint oldSelected = (uint)round(frac(old.a) * 4.0);
                float2 oldPixel = min(float2(tap * 2 + int2(oldSelected & 1, oldSelected >> 1)), gi_full.xy - 1.0) + 0.5;
                float2 tapWeight = saturate(1.0 - abs(previousUV * gi_full.xy - oldPixel) * 0.5);
                float weight = tapWeight.x * tapWeight.y;
                old.a = floor(old.a);
                previous += old * weight;
                totalWeight += weight;
            }
        }
    }

    float historyLength = 1;
    float3 result = current.rgb;
    if (totalWeight > 0.05)
    {
        previous /= totalWeight;
        float3 low = current.rgb, high = current.rgb;
        [unroll] for (int y = -1; y <= 1; ++y)
        {
            [unroll] for (int x = -1; x <= 1; ++x)
            {
                int2 tap = clamp(pixel + int2(x, y), 0, int2(gi_half.xy) - 1);
                if (GIGeometryWeight(geometry, gi_geometry.Load(int3(tap, 0))) < 0.1)
                    continue;
                float3 sampleColor = gi_noisy.Load(int3(tap, 0)).rgb;
                low = min(low, sampleColor);
                high = max(high, sampleColor);
            }
        }
        // Clamp stale illumination when sources move or switch off. The small
        // expansion keeps stochastic near-zero estimates from erasing history.
        float3 expansion = (high - low) * 0.1;
        previous.rgb = clamp(previous.rgb, max(0.0, low - expansion), high + expansion);
        float motionPixels = length(velocity * gi_full.xy * 0.5);
        float maxHistory = lerp(24.0, 8.0, saturate(motionPixels / 8.0));
        historyLength = min(previous.a + 1.0, maxHistory);
        float historyWeight = (1.0 - 1.0 / historyLength) * saturate(totalWeight * 2.0);
        result = lerp(current.rgb, previous.rgb, historyWeight);
    }
    gi_output[pixel] = float4(max(0.0, result), floor(historyLength) + selected * 0.25);
}