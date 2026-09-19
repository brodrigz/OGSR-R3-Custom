// Test doubles for the texture/driver boundary; the cache and remapping methods
// below are extracted verbatim from the engine by Test-PostprocessTextureRemap.ps1.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

using u32 = uint32_t;
#define R_ASSERT(x) do { if (!(x)) std::abort(); } while (false)
#define VERIFY(x) R_ASSERT(x)
#define PGO(x)

template<class T> struct Ref
{
    T* p{};
    T* _get() const { return p; }
    T* operator->() const { return p; }
    T& operator*() const { return *p; }
    explicit operator bool() const { return p != nullptr; }
};

class CBackend;
struct CTexture
{
    enum { mtMaxPixelShaderTextures=16, mtMaxVertexShaderTextures=4,
        mtMaxGeometryShaderTextures=16, mtMaxHullShaderTextures=16,
        mtMaxDomainShaderTextures=16, mtMaxComputeShaderTextures=16,
        rstVertex=256, rstGeometry=512, rstHull=768, rstDomain=1024, rstCompute=1280, rstInvalid=1536 };
    int last_slice{}, curr_slice{};
    std::function<void(CBackend&, u32)> bind;
    CTexture();
};
using ref_texture = Ref<CTexture>;
using STextureList = std::vector<std::pair<u32, ref_texture>>;
using ID3DShaderResourceView = CTexture;
struct Resources
{
    CTexture* ps[16]{};
    void SetPSResource(u32 i, CTexture* p) { ps[i] = p; }
    void SetVSResource(u32, CTexture*) {}
    void SetGSResource(u32, CTexture*) {}
    void SetHSResource(u32, CTexture*) {}
    void SetDSResource(u32, CTexture*) {}
    void SetCSResource(u32, CTexture*) {}
};
class CBackend
{
public:
    STextureList* T{};
    CTexture *textures_ps[16]{}, *textures_vs[4]{}, *textures_gs[16]{},
        *textures_hs[16]{}, *textures_ds[16]{}, *textures_cs[16]{};
    struct { u32 textures{}; } stat;
    Resources SRVSManager;
    void set_Textures(STextureList*);
    void override_PS_texture(u32, CTexture*);
};
CTexture::CTexture()
{
    bind = [this](CBackend& backend, u32 slot) { backend.SRVSManager.SetPSResource(slot, this); };
}
struct SPass { Ref<STextureList> T; };
struct ShaderElement { std::vector<Ref<SPass>> passes; };
struct Target { ref_texture pTexture; };
struct CRenderTarget
{
    bool m_pp_remap_enabled{true}, current_combine{true};
    Ref<Target> rt_Postprocess_0, rt_Generic_combine;
    Ref<Target> pp_src() const { return current_combine ? rt_Generic_combine : rt_Postprocess_0; }
    void pp_remap_scene_srv(CBackend&, ShaderElement*) const;
};

#include "postprocess-production.inl"

int main()
{
    CTexture pp0, combine, unrelated;
    Target a{{&pp0}}, b{{&combine}};
    CRenderTarget target;
    target.rt_Postprocess_0 = {&a}; target.rt_Generic_combine = {&b};
    CBackend backend;
    STextureList nv{{0, {&pp0}}, {1, {&unrelated}}};
    STextureList final_pass = nv; // A distinct shader, sharing the scene slot.
    SPass nv_pass{{&nv}}, final_binding{{&final_pass}};
    ShaderElement nv_element{{{&nv_pass}}}, final_element{{{&final_binding}}};

    backend.set_Textures(&nv);
    target.pp_remap_scene_srv(backend, &nv_element);
    if (backend.SRVSManager.ps[0] != &combine) return 1;
    target.current_combine = false;
    backend.set_Textures(&final_pass);
    target.pp_remap_scene_srv(backend, &final_element);
    if (backend.SRVSManager.ps[0] != &pp0)
    {
        std::puts("FAIL: NV -> combine kept the output target bound as scene input");
        return 1;
    }

    // Reuse the same shader across alternating odd/even postprocess chains.
    for (u32 step = 0; step < 16; ++step)
    {
        target.current_combine = (step % 2 == 0);
        backend.set_Textures(&nv);
        target.pp_remap_scene_srv(backend, &nv_element);
        CTexture* expected = target.current_combine ? &combine : &pp0;
        if (backend.SRVSManager.ps[0] != expected || backend.textures_ps[0] != expected ||
            backend.SRVSManager.ps[1] != &unrelated)
            return 2;
    }
    target.current_combine = true;
    backend.set_Textures(&nv);
    target.pp_remap_scene_srv(backend, &nv_element);
    // Leaving postprocessing must restore a shader's original texture list,
    // including when that list's address is identical to the previous one.
    target.m_pp_remap_enabled = false;
    backend.set_Textures(&nv);
    target.pp_remap_scene_srv(backend, &nv_element);
    if (backend.SRVSManager.ps[0] != &pp0 || backend.textures_ps[0] != &pp0) return 3;
    backend.override_PS_texture(0, nullptr);
    if (backend.SRVSManager.ps[0] || backend.textures_ps[0]) return 4;
    backend.set_Textures(&nv);
    if (backend.SRVSManager.ps[0] != &pp0) return 5;
    std::puts("PASS: NV -> combine, repeated ping-pong, unrelated textures, shader restoration and unbinding");
}
