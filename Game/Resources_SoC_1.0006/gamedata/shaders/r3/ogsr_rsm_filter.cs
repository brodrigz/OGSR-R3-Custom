#include "ogsr_rsm_common.h"

Texture2D<float4> gi_input : register(t0);
Texture2D<float4> gi_geometry : register(t1);
RWTexture2D<float4> gi_output : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchId.xy);
    if (any(pixel >= int2(gi_half.xy)))
        return;
    float4 center = gi_geometry.Load(int3(pixel, 0));
    if (center.w < GI_MIN_DEPTH || center.w > 9999.0)
    {
        gi_output[pixel] = 0;
        return;
    }
    float3 sum = 0;
    float total = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            int2 tap = clamp(pixel + int2(x, y), 0, int2(gi_half.xy) - 1);
            float weight = GIGeometryWeight(center, gi_geometry.Load(int3(tap, 0)));
            weight *= (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0);
            sum += gi_input.Load(int3(tap, 0)).rgb * weight;
            total += weight;
        }
    }
    // Spatially filtered output is never fed back into temporal history.
    gi_output[pixel] = float4(sum / max(total, 1e-5), floor(gi_input.Load(int3(pixel, 0)).a));
}