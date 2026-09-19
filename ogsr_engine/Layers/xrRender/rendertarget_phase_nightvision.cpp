#include "stdafx.h"
#include <DirectXPackedVector.h>

// Explicitly requested captures only. Thirteen pixels cover the diagnostic
// grid's twelve tile centers plus a fixed cyan border near the upper-left.
// Read the GPU result, not the Lua values or the intended texture bindings.
void CRenderTarget::debug_nvg_readback(CBackend& cmd_list, const char* stage, ID3D11Resource* resource) const
{
    auto* context = HW.get_context(cmd_list.context_id);
    if (!resource || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
    {
        Msg("[NVDBG E1] %s: readback unavailable (resource=%p context=%u)", stage, resource, cmd_list.context_id);
        return;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
    HRESULT hr = resource->QueryInterface(IID_PPV_ARGS(source.GetAddressOf()));
    if (FAILED(hr))
    {
        Msg("[NVDBG E1] %s: texture query failed hr=%08x", stage, static_cast<u32>(hr));
        return;
    }

    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    Msg("[NVDBG E1] %s: resource=%p size=%ux%u format=%u", stage, resource, desc.Width, desc.Height, static_cast<u32>(desc.Format));
    if (desc.SampleDesc.Count != 1 ||
        (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
         desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB && desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
         desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB))
    {
        Msg("[NVDBG E1] %s: unsupported readback format or sample count", stage);
        return;
    }

    D3D11_TEXTURE2D_DESC readbackDesc = desc;
    readbackDesc.Width = 13;
    readbackDesc.Height = readbackDesc.MipLevels = readbackDesc.ArraySize = 1;
    readbackDesc.Usage = D3D11_USAGE_STAGING;
    readbackDesc.BindFlags = readbackDesc.MiscFlags = 0;
    readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
    hr = HW.pDevice->CreateTexture2D(&readbackDesc, nullptr, readback.GetAddressOf());
    if (FAILED(hr))
    {
        Msg("[NVDBG E1] %s: staging allocation failed hr=%08x", stage, static_cast<u32>(hr));
        return;
    }

    for (u32 sample = 0; sample < 13; ++sample)
    {
        const u32 x = sample == 12 ? 0 : (2 * (sample % 4) + 1) * desc.Width / 8;
        const u32 y = sample == 12 ? 0 : (2 * (sample / 4) + 1) * desc.Height / 6;
        const D3D11_BOX box{x, y, 0, x + 1, y + 1, 1};
        context->CopySubresourceRegion(readback.Get(), 0, sample, 0, 0, source.Get(), 0, &box);
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
    {
        Msg("[NVDBG E1] %s: staging map failed hr=%08x", stage, static_cast<u32>(hr));
        return;
    }
    for (u32 sample = 0; sample < 13; ++sample)
    {
        float rgba[4]{};
        if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
        {
            const auto* pixel = static_cast<const u16*>(mapped.pData) + sample * 4;
            for (u32 channel = 0; channel < 4; ++channel)
                rgba[channel] = DirectX::PackedVector::XMConvertHalfToFloat(pixel[channel]);
        }
        else
        {
            const auto* pixel = static_cast<const u8*>(mapped.pData) + sample * 4;
            const bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            rgba[0] = pixel[bgra ? 2 : 0] / 255.f;
            rgba[1] = pixel[1] / 255.f;
            rgba[2] = pixel[bgra ? 0 : 2] / 255.f;
            rgba[3] = pixel[3] / 255.f;
        }
        Msg("[NVDBG E1] %s sample=%u rgba=%.6g,%.6g,%.6g,%.6g", stage, sample + 1, rgba[0], rgba[1], rgba[2], rgba[3]);
    }
    context->Unmap(readback.Get(), 0);
}

void CRenderTarget::phase_nightvision(CBackend& cmd_list)
{
    PIX_EVENT(phase_nightvision);

    RenderScreenTriangle(cmd_list, pp_dst(), s_nightvision->E[0]);
    pp_flip();
}
