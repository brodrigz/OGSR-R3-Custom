#ifndef OGSR_RSM_CAPTURE_H
#define OGSR_RSM_CAPTURE_H

struct RSMCapture
{
    float4 albedo : SV_Target0;
    float2 geometry : SV_Target1; // packed 10+10 bit oct normal, float32 depth
};

RSMCapture CaptureRSM(float3 color, float3 worldPosition, float depth)
{
    RSMCapture O;
    // Geometric normal keeps capture independent of each material's bump setup.
    float3 N = cross(ddx(worldPosition), ddy(worldPosition));
    N *= rsqrt(max(dot(N, N), 1e-16));
    N *= dot(N, -L_sun_dir_w) < 0 ? -1 : 1;
    float2 oct = gbuf_pack_normal(N);
    uint2 packed = (uint2)round(saturate(oct) * 1023.0);
    O.geometry = float2(packed.x + packed.y * 1024, depth);
    // Flux per projected area: the gather applies sun radiance and sample area.
    // Do not multiply another sun cosine: projected texel area already includes it.
    O.albedo = float4(saturate(color), 1);
    return O;
}
#endif
