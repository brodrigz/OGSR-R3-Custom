#ifndef OGSR_SSGI_LIGHT_H
#define OGSR_SSGI_LIGHT_H

// Only deferred accumulation emits the optional second render target.
// Both targets use the existing additive blend and color write masks.
#ifdef USE_SSGI
struct SSGILightOutput
{
    float4 lighting : SV_Target0;
    float4 diffuse : SV_Target1;
};
#define SSGI_LIGHT_SEMANTIC
SSGILightOutput SSGIPackLight(float4 lighting, float4 diffuse)
{
    SSGILightOutput o;
    o.lighting = lighting;
    o.diffuse = diffuse;
    return o;
}
#else
#define SSGILightOutput float4
#define SSGI_LIGHT_SEMANTIC : SV_Target
float4 SSGIPackLight(float4 lighting, float4 diffuse) { return lighting; }
#endif
#endif
