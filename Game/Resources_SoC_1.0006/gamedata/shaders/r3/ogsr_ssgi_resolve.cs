#include "ogsr_ssgi_common.h"

Texture2D<float4> gi_input : register(t0);
Texture2D<float4> gi_geometry : register(t1);
Texture2D<float4> gi_position : register(t2);
RWTexture2D<float4> gi_output : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchId.xy);
    if (any(pixel >= int2(gi_full.xy)))
        return;
    float4 packed = gi_position.Load(int3(pixel, 0));
    if (packed.z < GI_MIN_DEPTH || packed.z > 9999.0)
    {
        gi_output[pixel] = 0;
        return;
    }
    float4 center = float4(GIWorldNormal(packed.xy), packed.z);
    float2 halfPixel = (float2(pixel) + 0.5) * 0.5 - 0.5;
    int2 base = int2(floor(halfPixel));
    float4 sum = 0;
    float total = 0;
    [unroll] for (int y = -1; y <= 2; ++y)
    {
        [unroll] for (int x = -1; x <= 2; ++x)
        {
            int2 tap = base + int2(x, y);
            if (any(tap < 0) || any(tap >= int2(gi_half.xy)))
                continue;
            float weight = GIGeometryWeight(center, gi_geometry.Load(int3(tap, 0)));
            // A wider reconstruction footprint fills holes caused by choosing
            // the nearest surface in each half-resolution cell.
            float2 delta = float2(tap) - halfPixel;
            weight *= exp2(-dot(delta, delta) * 2.0);
            sum += gi_input.Load(int3(tap, 0)) * weight;
            total += weight;
        }
    }
    // If no matching surface exists, retain the engine's ambient fallback.
    gi_output[pixel] = total > 0.001 ? sum / total : 0;
}
