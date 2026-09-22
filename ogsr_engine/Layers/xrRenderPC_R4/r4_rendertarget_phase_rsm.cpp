#include "stdafx.h"
#include "../xrRender/dxRenderDeviceRender.h"
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class RSMResources
{
public:
    struct Texture
    {
        ComPtr<ID3D11Texture2D> surface;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11UnorderedAccessView> uav;

        void create(u32 width, u32 height)
        {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            CHK_DX(HW.pDevice->CreateTexture2D(&desc, nullptr, surface.GetAddressOf()));
            CHK_DX(HW.pDevice->CreateShaderResourceView(surface.Get(), nullptr, srv.GetAddressOf()));
            CHK_DX(HW.pDevice->CreateUnorderedAccessView(surface.Get(), nullptr, uav.GetAddressOf()));
        }
    };

    // Flat fields match ogsr_rsm_common.h; Fmatrix stores row-vector transforms.
    struct Constants
    {
        Fvector4 full, half, ndc, trace, history, jitter;
        Fmatrix inverseView, currentToPreviousView;
        Fvector4 map, sun;
        Fmatrix worldToLight, lightToWorld, worldToView;
    };
    static_assert(sizeof(Constants) == 28 * 16);

    u32 width{}, height{}, index{}, lastFrame{u32(-1)}, quality{};
    float radius{}, thickness{}, fov{}, aspect{};
    Fvector cameraPosition{}, cameraDirection{}, cameraUp{};
    Fvector2 jitter{};
    Fmatrix view{};
    Fmatrix lightTransform{};
    Fvector sunColor{};
    float mapSize{};
    bool valid{};
    Texture noisy, geometry[2], history[2], filtered, resolved, diagnostics;
    ComPtr<ID3D11Buffer> constants;
    ref_cs evaluate, temporal, filter, resolve;
    ref_texture output;
    ref_texture diagnosticOutput;

    ~RSMResources()
    {
        if (output)
            output->surface_set(nullptr);
        if (diagnosticOutput)
            diagnosticOutput->surface_set(nullptr);
    }
};

void CRenderTarget::InitRSM()
{
    R_ASSERT(!m_rsm);
    const u32 mapWidth = RImplementation.o.sun_cascades_smapsize[0];
    rt_rsm_albedo.create("$user$rsm_albedo", mapWidth, mapWidth, DXGI_FORMAT_R8G8B8A8_UNORM);
    rt_rsm_geometry.create("$user$rsm_geometry", mapWidth, mapWidth, DXGI_FORMAT_R32G32_FLOAT);
    m_rsm = xr_new<RSMResources>();
    auto& gi = *m_rsm;
    gi.width = (m_renderWidth + 1) / 2;
    gi.height = (m_renderHeight + 1) / 2;
    gi.noisy.create(gi.width, gi.height);
    gi.filtered.create(gi.width, gi.height);
    gi.diagnostics.create(gi.width, gi.height);
    for (u32 i = 0; i < 2; ++i)
    {
        gi.geometry[i].create(gi.width, gi.height);
        gi.history[i].create(gi.width, gi.height);
    }
    gi.resolved.create(m_renderWidth, m_renderHeight);
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(RSMResources::Constants);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    CHK_DX(HW.pDevice->CreateBuffer(&cb, nullptr, gi.constants.GetAddressOf()));
    gi.evaluate = DEV->_CreateCS("ogsr_rsm_evaluate");
    gi.temporal = DEV->_CreateCS("ogsr_rsm_temporal");
    gi.filter = DEV->_CreateCS("ogsr_rsm_filter");
    gi.resolve = DEV->_CreateCS("ogsr_rsm_resolve");
    gi.output.create("$user$rsm");
    gi.output->surface_set(gi.resolved.surface.Get());
    gi.diagnosticOutput.create("$user$rsm_diagnostics");
    gi.diagnosticOutput->surface_set(gi.diagnostics.surface.Get());
    s_rsm_debug.create("ogsr_rsm_debug");
    Msg("* RSM prototype: %ux%u sun capture, %ux%u gather; replaces SSFX indirect light. r_rsm changes require cfg_save and full restart.",
        mapWidth, mapWidth, gi.width, gi.height);
}

void CRenderTarget::DestroyRSM()
{
    xr_delete(m_rsm);
}

void CRenderTarget::set_rsm_sun(const Fmatrix& transform, const Fvector& color, float size)
{
    if (!m_rsm)
        return;
    m_rsm->lightTransform = transform;
    m_rsm->sunColor = color;
    m_rsm->mapSize = size;
    m_rsm_frame = Device.dwFrame;
}

void CRenderTarget::phase_rsm(CBackend& cmd_list)
{
    PIX_EVENT_CTX(cmd_list, rsm);
    auto& gi = *m_rsm;
    auto* context = HW.get_context(cmd_list.context_id);
    if (m_rsm_frame != Device.dwFrame)
    {
        // No sun was submitted this frame (night, disabled sun, level transition).
        // Clear the published result immediately instead of retaining daytime GI.
        context->ClearState();
        const float zero[4]{};
        context->ClearUnorderedAccessViewFloat(gi.resolved.uav.Get(), zero);
        context->ClearUnorderedAccessViewFloat(gi.diagnostics.uav.Get(), zero);
        gi.valid = false;
        m_resetRSMHistory = true;
        cmd_list.Invalidate();
        cmd_list.set_ColorWriteEnable();
        return;
    }
    const bool reset = m_resetRSMHistory || !gi.valid || gi.lastFrame + 1 != Device.dwFrame ||
        gi.cameraPosition.distance_to_sqr(Device.vCameraPosition) > 25.f ||
        gi.cameraDirection.dotproduct(Device.vCameraDirection) < 0.707f ||
        gi.cameraUp.dotproduct(Device.vCameraTop) < 0.707f ||
        _abs(gi.fov - Device.fFOV) > 5.f || _abs(gi.aspect - Device.fASPECT) > EPS ||
        gi.radius != ps_r_rsm_radius || gi.thickness != ps_r_rsm_thickness || gi.quality != ps_r_rsm_quality;

    const float vertical = -tanf(deg2rad(Device.fFOV / 2.f));
    const float horizontal = -vertical / Device.fASPECT;
    RSMResources::Constants data{};
    data.full = {float(m_renderWidth), float(m_renderHeight), 1.f / m_renderWidth, 1.f / m_renderHeight};
    data.half = {float(gi.width), float(gi.height), 1.f / gi.width, 1.f / gi.height};
    data.ndc = {2.f * horizontal, 2.f * vertical, -horizontal * (1.f + ps_r_taa_jitter.x), -vertical * (1.f - ps_r_taa_jitter.y)};
    data.trace = {ps_r_rsm_radius, ps_r_rsm_thickness, float(Device.dwFrame % 64), float(ps_r_rsm_quality)};
    data.history = {reset ? 1.f : 0.f, gi.jitter.x, gi.jitter.y, 0.f};
    data.jitter = {ps_r_taa_jitter.x, ps_r_taa_jitter.y, 0.f, 0.f};
    data.inverseView = Device.mInvView;
    data.worldToView = Device.mView;
    data.worldToLight = gi.lightTransform;
    data.lightToWorld.invert(gi.lightTransform);
    const Fvector depthAxis{gi.lightTransform._13, gi.lightTransform._23, gi.lightTransform._33};
    const float depthPerMetre = depthAxis.magnitude();
    data.map = {float(rt_rsm_albedo->dwWidth), 1.f / rt_rsm_albedo->dwWidth, gi.mapSize, depthPerMetre};
    data.sun = {powf(std::max(0.f, gi.sunColor.x), 2.2f), powf(std::max(0.f, gi.sunColor.y), 2.2f), powf(std::max(0.f, gi.sunColor.z), 2.2f), 0.f};
    if (reset)
        data.currentToPreviousView.identity();
    else
        data.currentToPreviousView.mul(gi.view, Device.mInvView);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    CHK_DX(context->Map(gi.constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
    CopyMemory(mapped.pData, &data, sizeof(data));
    context->Unmap(gi.constants.Get(), 0);

    // Direct CS calls must not leave stale bindings in the graphics state cache.
    context->ClearState();
    ID3D11Buffer* cb = gi.constants.Get();
    context->CSSetConstantBuffers(0, 1, &cb);
    ID3D11ShaderResourceView* noSRV[5]{};
    ID3D11UnorderedAccessView* noUAV[3]{};
    auto dispatch = [&](ref_cs& shader, ID3D11ShaderResourceView** inputs, u32 inputCount,
                        ID3D11UnorderedAccessView** outputs, u32 outputCount, u32 width, u32 height)
    {
        context->CSSetShader(shader->sh, nullptr, 0);
        context->CSSetShaderResources(0, inputCount, inputs);
        context->CSSetUnorderedAccessViews(0, outputCount, outputs, nullptr);
        context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
        context->CSSetUnorderedAccessViews(0, outputCount, noUAV, nullptr);
        context->CSSetShaderResources(0, inputCount, noSRV);
    };
    const u32 current = gi.index, previous = current ^ 1;
    if (reset)
    {
        // History textures otherwise have undefined contents on their first use.
        const float zero[4]{};
        context->ClearUnorderedAccessViewFloat(gi.history[previous].uav.Get(), zero);
        context->ClearUnorderedAccessViewFloat(gi.geometry[previous].uav.Get(), zero);
    }
    {
        PIX_EVENT_CTX(cmd_list, rsm_evaluate);
        ID3D11ShaderResourceView* inputs[] = {rt_Position->pTexture->get_SRView(), rt_rsm_albedo->pTexture->get_SRView(),
            rt_rsm_geometry->pTexture->get_SRView(), rt_smap_sun_cascade[0]->pTexture->get_SRView()};
        ID3D11UnorderedAccessView* outputs[] = {gi.noisy.uav.Get(), gi.geometry[current].uav.Get(), gi.diagnostics.uav.Get()};
        dispatch(gi.evaluate, inputs, 4, outputs, 3, gi.width, gi.height);
    }
    {
        PIX_EVENT_CTX(cmd_list, rsm_temporal);
        ID3D11ShaderResourceView* inputs[] = {gi.noisy.srv.Get(), gi.geometry[current].srv.Get(), gi.history[previous].srv.Get(),
            gi.geometry[previous].srv.Get(), rt_Velocity->pTexture->get_SRView()};
        ID3D11UnorderedAccessView* output = gi.history[current].uav.Get();
        dispatch(gi.temporal, inputs, 5, &output, 1, gi.width, gi.height);
    }
    {
        PIX_EVENT_CTX(cmd_list, rsm_filter);
        ID3D11ShaderResourceView* inputs[] = {gi.history[current].srv.Get(), gi.geometry[current].srv.Get()};
        ID3D11UnorderedAccessView* output = gi.filtered.uav.Get();
        dispatch(gi.filter, inputs, 2, &output, 1, gi.width, gi.height);
    }
    {
        PIX_EVENT_CTX(cmd_list, rsm_resolve);
        ID3D11ShaderResourceView* inputs[] = {gi.filtered.srv.Get(), gi.geometry[current].srv.Get(), rt_Position->pTexture->get_SRView()};
        ID3D11UnorderedAccessView* output = gi.resolved.uav.Get();
        dispatch(gi.resolve, inputs, 3, &output, 1, m_renderWidth, m_renderHeight);
    }
    context->ClearState();
    cmd_list.Invalidate();
    cmd_list.set_ColorWriteEnable();

    gi.index ^= 1;
    gi.view = Device.mView;
    gi.cameraPosition = Device.vCameraPosition;
    gi.cameraDirection = Device.vCameraDirection;
    gi.cameraUp = Device.vCameraTop;
    gi.jitter.set(ps_r_taa_jitter.x, ps_r_taa_jitter.y);
    gi.fov = Device.fFOV;
    gi.aspect = Device.fASPECT;
    gi.radius = ps_r_rsm_radius;
    gi.thickness = ps_r_rsm_thickness;
    gi.quality = ps_r_rsm_quality;
    gi.lastFrame = Device.dwFrame;
    gi.valid = true;
    m_resetRSMHistory = false;
}
