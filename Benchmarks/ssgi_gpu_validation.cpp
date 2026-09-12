// Headless D3D11/WARP checks for the actual compiled SSGI shaders.
#define NOMINMAX
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using DirectX::PackedVector::XMConvertFloatToHalf;
using DirectX::PackedVector::XMConvertHalfToFloat;
struct Pixel { float x{}, y{}, z{}, w{}; };
struct Constants
{
    Pixel full, half, ndc, trace, history, jitter;
    float inverseView[16]{}, previousView[16]{};
};
static_assert(sizeof(Constants) == 224);

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void check(HRESULT result)
{
    if (FAILED(result)) throw std::runtime_error("D3D11 error: " + std::to_string(result));
}

struct Texture
{
    unsigned width, height;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    Texture(ID3D11Device* device, unsigned w, unsigned h) : width(w), height(h)
    {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w; d.Height = h; d.ArraySize = d.MipLevels = d.SampleDesc.Count = 1;
        d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&d, nullptr, &texture));
        check(device->CreateShaderResourceView(texture.Get(), nullptr, &srv));
        check(device->CreateUnorderedAccessView(texture.Get(), nullptr, &uav));
    }
    void upload(ID3D11DeviceContext* context, const std::vector<Pixel>& pixels)
    {
        require(pixels.size() == size_t(width) * height, "Wrong upload size");
        std::vector<uint16_t> packed(pixels.size() * 4);
        for (size_t i = 0; i < pixels.size(); ++i)
        {
            const float* value = &pixels[i].x;
            for (int c = 0; c < 4; ++c) packed[i * 4 + c] = XMConvertFloatToHalf(value[c]);
        }
        context->UpdateSubresource(texture.Get(), 0, nullptr, packed.data(), width * 8, 0);
    }
    void fill(ID3D11DeviceContext* context, Pixel value)
    {
        upload(context, std::vector<Pixel>(size_t(width) * height, value));
    }
    std::vector<Pixel> read(ID3D11Device* device, ID3D11DeviceContext* context)
    {
        D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d);
        d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        check(device->CreateTexture2D(&d, nullptr, &staging));
        context->CopyResource(staging.Get(), texture.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map));
        std::vector<Pixel> result(size_t(width) * height);
        for (unsigned y = 0; y < height; ++y)
        {
            const auto* row = reinterpret_cast<const uint16_t*>(static_cast<const char*>(map.pData) + y * map.RowPitch);
            for (unsigned x = 0; x < width; ++x)
            {
                float* value = &result[y * width + x].x;
                for (int c = 0; c < 4; ++c) value[c] = XMConvertHalfToFloat(row[x * 4 + c]);
            }
        }
        context->Unmap(staging.Get(), 0);
        for (auto p : result) require(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.w), "Non-finite shader output");
        return result;
    }
};

struct Runner
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11InfoQueue> messages;
    ComPtr<ID3D11Buffer> cb;
    std::array<ComPtr<ID3D11ComputeShader>, 4> shaders;
    Runner(const std::wstring& directory)
    {
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
        if (FAILED(hr)) check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
        device.As(&messages);
        const wchar_t* names[] = {L"evaluate", L"temporal", L"filter", L"resolve"};
        for (unsigned i = 0; i < 4; ++i)
        {
            ComPtr<ID3DBlob> blob;
            check(D3DReadFileToBlob((directory + L"/ogsr_ssgi_" + names[i] + L".cs-ssgi.cso").c_str(), &blob));
            check(device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shaders[i]));
            ComPtr<ID3D11ShaderReflection> reflection;
            check(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&reflection)));
            D3D11_SHADER_BUFFER_DESC desc{};
            check(reflection->GetConstantBufferByName("SSGIConstants")->GetDesc(&desc));
            require(desc.Size == sizeof(Constants), "C++/HLSL constant buffer size mismatch");
            for (unsigned v = 0; v < desc.Variables; ++v)
            {
                D3D11_SHADER_TYPE_DESC type{};
                check(reflection->GetConstantBufferByName("SSGIConstants")->GetVariableByIndex(v)->GetType()->GetDesc(&type));
                require(type.Type == D3D_SVT_FLOAT && (type.Class == D3D_SVC_VECTOR || type.Class == D3D_SVC_MATRIX_ROWS), "Unsupported engine constant reflection type");
            }
        }
        D3D11_BUFFER_DESC d{}; d.ByteWidth = sizeof(Constants); d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&d, nullptr, &cb));
    }
    void run(unsigned shader, const Constants& constants, std::initializer_list<Texture*> inputs, std::initializer_list<Texture*> outputs)
    {
        context->ClearState();
        context->UpdateSubresource(cb.Get(), 0, nullptr, &constants, 0, 0);
        ID3D11Buffer* buffer = cb.Get();
        context->CSSetConstantBuffers(0, 1, &buffer);
        std::vector<ID3D11ShaderResourceView*> srvs;
        std::vector<ID3D11UnorderedAccessView*> uavs;
        for (auto* texture : inputs) srvs.push_back(texture->srv.Get());
        for (auto* texture : outputs) uavs.push_back(texture->uav.Get());
        context->CSSetShaderResources(0, unsigned(srvs.size()), srvs.data());
        context->CSSetUnorderedAccessViews(0, unsigned(uavs.size()), uavs.data(), nullptr);
        context->CSSetShader(shaders[shader].Get(), nullptr, 0);
        auto* target = *outputs.begin();
        context->Dispatch((target->width + 7) / 8, (target->height + 7) / 8, 1);
        context->ClearState();
    }
    void checkMessages()
    {
        if (!messages) return;
        for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i)
        {
            SIZE_T size{}; messages->GetMessage(i, nullptr, &size);
            std::vector<char> data(size);
            auto* message = reinterpret_cast<D3D11_MESSAGE*>(data.data());
            check(messages->GetMessage(i, message, &size));
            if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
                throw std::runtime_error(message->pDescription);
        }
    }
};

Constants makeConstants(unsigned w, unsigned h)
{
    Constants c{};
    c.full = {float(w), float(h), 1.f / w, 1.f / h};
    c.half = {float((w + 1) / 2), float((h + 1) / 2), 1.f / ((w + 1) / 2), 1.f / ((h + 1) / 2)};
    c.ndc = {2, -2, -1, 1};
    c.trace = {3, 0.25f, 0, 2};
    for (int i = 0; i < 16; i += 5) c.inverseView[i] = c.previousView[i] = 1;
    return c;
}

void testSize(Runner& r, unsigned w, unsigned h)
{
    auto* d = r.device.Get(); auto* ctx = r.context.Get();
    unsigned hw = (w + 1) / 2, hh = (h + 1) / 2;
    Texture position(d,w,h), source(d,w,h), albedo(d,w,h), velocity(d,w,h);
    Texture noisy(d,hw,hh), geometry(d,hw,hh), oldGeometry(d,hw,hh), oldHistory(d,hw,hh), history(d,hw,hh), filtered(d,hw,hh), resolved(d,w,h);
    Constants c = makeConstants(w,h);
    albedo.fill(ctx, {1,1,1,0}); velocity.fill(ctx, {});
    auto evaluate = [&] { r.run(0,c,{&position,&source,&albedo},{&noisy,&geometry}); };
    auto temporal = [&] { r.run(1,c,{&noisy,&geometry,&oldHistory,&oldGeometry,&velocity},{&history}); };
    auto read = [&](Texture& t) { return t.read(d,ctx); };
    auto black = [&](Texture& t, const char* label) { for (auto p : read(t)) require(std::abs(p.x)+std::abs(p.y)+std::abs(p.z)<1e-5f,label); };

    // No light may be invented from empty or coplanar geometry, even with bright sources.
    source.fill(ctx,{4,4,4,1}); position.fill(ctx,{}); evaluate(); black(noisy,"Sky received bounce");
    position.fill(ctx,{1,1,3,0}); evaluate(); black(noisy,"Coplanar surface self-lit"); // octahedral -Z normal

    if (w >= 32 && h >= 32)
    {
        // Two perpendicular walls: red wall at x=-1 and receiving wall at z=3.
        std::vector<Pixel> p(w*h), light(w*h);
        for (unsigned y=0; y<h; ++y) for (unsigned x=0; x<w; ++x)
        {
            float rayX=(x+0.5f)*2/w-1;
            float sideDepth=rayX < 0 ? -1/rayX : 1e10f;
            bool side=sideDepth<3;
            p[y*w+x]=side ? Pixel{1,0.5f,sideDepth,0} : Pixel{1,1,3,0};
            light[y*w+x]=side ? Pixel{4,0,0,0} : Pixel{};
        }
        position.upload(ctx,p); source.upload(ctx,light);
        for (int quality=1; quality<=3; ++quality)
        {
            c.trace.w=float(quality); evaluate();
            float maxRed=0;
            const auto output=read(noisy), normals=read(geometry);
            for (size_t i=0; i<output.size(); ++i)
            {
                if (normals[i].z < -0.9f) maxRed=std::max(maxRed,output[i].x);
                require(output[i].y==0 && output[i].z==0,"Red source contaminated another channel");
            }
            require(maxRed>0.001f,"Lit perpendicular wall produced no bounce");
        }
        source.fill(ctx,{}); evaluate(); black(noisy,"Unlit corner received bounce");
        // Emissive alpha follows the same linear albedo conversion as combine.
        for (auto& value:light) if (value.x>0) value={0,0,0,4};
        source.upload(ctx,light); evaluate();
        float emissiveMax=0;
        for (auto value:read(noisy)) emissiveMax=std::max(emissiveMax,value.x);
        require(emissiveMax>0.001f,"Emissive alpha produced no bounce");
        // Reverse only emitter normals: backfaces still occlude but emit no light.
        for (auto& value:p) if (value.y==0.5f) value.x=0;
        position.upload(ctx,p); source.upload(ctx,light); evaluate(); black(noisy,"Back-facing emitter contributed bounce");
    }

    // Check temporal acceptance, explicit reset, disocclusion, and normal rejection.
    geometry.fill(ctx,{0,0,-1,3}); oldGeometry.fill(ctx,{0,0,-1,3});
    noisy.fill(ctx,{0.5f,0.25f,0.125f,0}); oldHistory.fill(ctx,{0.5f,0.25f,0.125f,8});
    c.history.x=0; temporal();
    auto centerIndex=(hh/2)*hw+hw/2;
    require(read(history)[centerIndex].w>1,"Valid history was rejected");
    c.history.x=1; temporal(); require(read(history)[centerIndex].w==1,"Explicit history reset failed");
    c.history.x=0; oldGeometry.fill(ctx,{0,0,-1,10}); temporal(); require(read(history)[centerIndex].w==1,"Disocclusion retained history");
    oldGeometry.fill(ctx,{0,1,0,3}); temporal(); require(read(history)[centerIndex].w==1,"Normal discontinuity retained history");
    oldGeometry.fill(ctx,{0,0,-1,3}); velocity.fill(ctx,{4,4,0,0}); temporal(); require(read(history)[centerIndex].w==1,"Off-screen reprojection retained history");
    velocity.fill(ctx,{});
    // Camera translation: history depth must be compared in the previous view.
    c.previousView[14]=2; oldGeometry.fill(ctx,{0,0,-1,5}); temporal(); require(read(history)[centerIndex].w>1,"Previous-view depth transform failed");
    c.previousView[14]=0;
    oldGeometry.fill(ctx,{0,0,-1,3}); noisy.fill(ctx,{0,0,0,0});
    oldHistory.fill(ctx,{5,4,3,24}); temporal(); black(history,"Extinguished light left stale history");

    // A stationary gradient must not drift toward the neighboring 2x2 cell.
    // History samples are located at selected full pixels, not block centers.
    std::vector<Pixel> gradient(hw*hh), gradientHistory(hw*hh);
    for (unsigned y=0; y<hh; ++y) for (unsigned x=0; x<hw; ++x)
    {
        float value=float(x)/std::max(1u,hw-1);
        gradient[y*hw+x]={value,0,0,0};
        gradientHistory[y*hw+x]={value,0,0,8};
    }
    noisy.upload(ctx,gradient); oldHistory.upload(ctx,gradientHistory); temporal();
    auto stable=read(history);
    for (size_t i=0; i<stable.size(); ++i) require(std::abs(stable[i].x-gradient[i].x)<0.002f,"Stationary history drifted between cells");

    // The complete spatial chain preserves constant irradiance at odd/tiny sizes.
    history.fill(ctx,{0.5f,0.25f,0.125f,8}); geometry.fill(ctx,{0,0,-1,3}); position.fill(ctx,{1,1,3,0});
    r.run(2,c,{&history,&geometry},{&filtered});
    r.run(3,c,{&filtered,&geometry,&position},{&resolved});
    for (auto value:read(resolved)) require(std::abs(value.x-0.5f)<0.002f && std::abs(value.y-0.25f)<0.002f,"Resolve lost constant irradiance at boundary");
    position.fill(ctx,{}); r.run(3,c,{&filtered,&geometry,&position},{&resolved}); black(resolved,"Resolve filled sky");
    position.fill(ctx,{1,1,0.2f,0}); r.run(3,c,{&filtered,&geometry,&position},{&resolved}); black(resolved,"Resolve filled HUD");
    // A bright surface with an incompatible normal/depth must not bleed into
    // a receiver whose matching half-resolution geometry has disappeared.
    geometry.fill(ctx,{0,1,0,8}); position.fill(ctx,{1,1,3,0});
    r.run(3,c,{&filtered,&geometry,&position},{&resolved}); black(resolved,"Resolve leaked across surfaces");
    r.checkMessages();
    std::cout << "PASS: WARP SSGI " << w << 'x' << h << " lighting, history, filtering, bounds and finite outputs\n";
}

// Reflect the real pixel shaders so tests also cover source preparation and
// final material/intensity composition, beyond the compute-only test scenes.
struct PixelPass
{
    Runner& r;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11ShaderReflection> reflection;
    std::vector<std::vector<char>> constants;
    std::vector<ComPtr<ID3D11Buffer>> buffers;
    PixelPass(Runner& runner, const std::wstring& file) : r(runner)
    {
        ComPtr<ID3DBlob> blob;
        check(D3DReadFileToBlob(file.c_str(), &blob));
        check(r.device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ps));
        check(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&reflection)));
        const char* vertex = "struct O { float4 p:SV_Position; float2 uv:TEXCOORD0; }; O main(uint i:SV_VertexID) { O o; o.uv=float2((i<<1)&2,i&2); o.p=float4(o.uv*float2(2,-2)+float2(-1,1),0,1); return o; }";
        check(D3DCompile(vertex, std::strlen(vertex), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &blob, nullptr));
        check(r.device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs));
        D3D11_SHADER_DESC desc{}; check(reflection->GetDesc(&desc));
        constants.resize(desc.ConstantBuffers); buffers.resize(desc.ConstantBuffers);
        for (unsigned i=0;i<desc.ConstantBuffers;++i)
        {
            D3D11_SHADER_BUFFER_DESC b{}; check(reflection->GetConstantBufferByIndex(i)->GetDesc(&b));
            constants[i].resize(b.Size);
            D3D11_BUFFER_DESC bd{}; bd.ByteWidth=b.Size; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            check(r.device->CreateBuffer(&bd,nullptr,&buffers[i]));
        }
    }
    void set(const char* name, const void* data, size_t size)
    {
        for (unsigned i=0;i<constants.size();++i)
        {
            D3D11_SHADER_VARIABLE_DESC v{};
            if (SUCCEEDED(reflection->GetConstantBufferByIndex(i)->GetVariableByName(name)->GetDesc(&v)))
            {
                if (size>v.Size) throw std::runtime_error(std::string("Pixel constant too large: ")+name+" size "+std::to_string(v.Size));
                std::memcpy(constants[i].data()+v.StartOffset,data,size);
            }
        }
    }
    void set(const char* name, Pixel p) { set(name,&p,sizeof(p)); }
    void bind(const char* name, ID3D11ShaderResourceView* srv)
    {
        D3D11_SHADER_INPUT_BIND_DESC b{};
        if (SUCCEEDED(reflection->GetResourceBindingDescByName(name,&b))) r.context->PSSetShaderResources(b.BindPoint,1,&srv);
    }
    void draw(Texture& output, bool additive=false)
    {
        auto* ctx=r.context.Get();
        D3D11_SHADER_DESC desc{}; check(reflection->GetDesc(&desc));
        D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler; check(r.device->CreateSamplerState(&sd,&sampler));
        for (unsigned i=0;i<desc.BoundResources;++i)
        {
            D3D11_SHADER_INPUT_BIND_DESC b{}; check(reflection->GetResourceBindingDesc(i,&b));
            if (b.Type==D3D_SIT_SAMPLER) { auto* s=sampler.Get(); ctx->PSSetSamplers(b.BindPoint,1,&s); }
        }
        for (unsigned i=0;i<constants.size();++i)
        {
            D3D11_SHADER_BUFFER_DESC b{}; check(reflection->GetConstantBufferByIndex(i)->GetDesc(&b));
            D3D11_SHADER_INPUT_BIND_DESC binding{}; check(reflection->GetResourceBindingDescByName(b.Name,&binding));
            auto* buffer=buffers[i].Get(); ctx->UpdateSubresource(buffer,0,nullptr,constants[i].data(),0,0);
            ctx->PSSetConstantBuffers(binding.BindPoint,1,&buffer);
        }
        ComPtr<ID3D11RenderTargetView> rtv;
        check(r.device->CreateRenderTargetView(output.texture.Get(),nullptr,&rtv));
        auto* view=rtv.Get(); ctx->OMSetRenderTargets(1,&view,nullptr);
        D3D11_BLEND_DESC blend{}; auto& target=blend.RenderTarget[0];
        target.BlendEnable=additive; target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        target.SrcBlend=target.DestBlend=target.SrcBlendAlpha=target.DestBlendAlpha=D3D11_BLEND_ONE;
        target.BlendOp=target.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        ComPtr<ID3D11BlendState> bs; check(r.device->CreateBlendState(&blend,&bs)); ctx->OMSetBlendState(bs.Get(),nullptr,~0u);
        D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
        ComPtr<ID3D11RasterizerState> rs; check(r.device->CreateRasterizerState(&raster,&rs)); ctx->RSSetState(rs.Get());
        D3D11_VIEWPORT viewport{0,0,float(output.width),float(output.height),0,1}; ctx->RSSetViewports(1,&viewport);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs.Get(),nullptr,0); ctx->PSSetShader(ps.Get(),nullptr,0); ctx->Draw(3,0);
        ctx->ClearState();
    }
};

void testSourceAndComposition(Runner& r, const std::wstring& directory)
{
    auto* d=r.device.Get(); auto* ctx=r.context.Get();
    Texture position(d,16,16), albedo(d,16,16), source(d,16,16), ao(d,16,16), gi(d,16,16), accum(d,16,16), scene(d,16,16), output(d,32,32);
    position.fill(ctx,{1,1,3,2}); albedo.fill(ctx,{1,1,1,0.5f}); ao.fill(ctx,{1,0.5f,0.5f,0}); accum.fill(ctx,{});
    gi.fill(ctx,{0.1f,0.05f,0.025f,12});
    // A black cube allows a controlled ambient-only environment source.
    D3D11_TEXTURE2D_DESC cd{}; cd.Width=cd.Height=cd.MipLevels=cd.SampleDesc.Count=1; cd.ArraySize=6;
    cd.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; cd.BindFlags=D3D11_BIND_SHADER_RESOURCE; cd.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
    std::array<uint16_t,4> black{}; std::array<D3D11_SUBRESOURCE_DATA,6> faces{};
    for (auto& f:faces) { f.pSysMem=black.data(); f.SysMemPitch=8; }
    ComPtr<ID3D11Texture2D> cube; ComPtr<ID3D11ShaderResourceView> cubeSRV;
    check(d->CreateTexture2D(&cd,faces.data(),&cube)); check(d->CreateShaderResourceView(cube.Get(),nullptr,&cubeSRV));
    auto configure=[&](PixelPass& pass) {
        ctx->ClearState();
        auto constants=makeConstants(16,16);
        pass.set("m_inv_V",constants.inverseView,12*sizeof(float));
        pass.set("pos_decompression_params",{1,-1,2.f/16,-2.f/16});
        pass.set("pos_decompression_params2",{16,16,1.f/16,1.f/16});
        pass.bind("s_position",position.srv.Get()); pass.bind("s_diffuse",albedo.srv.Get());
        pass.bind("s_ao",ao.srv.Get()); pass.bind("s_accumulator",accum.srv.Get());
        pass.bind("s_ssgi",gi.srv.Get()); pass.bind("s_ssgi_source",source.srv.Get());
        pass.bind("env_s0",cubeSRV.Get()); pass.bind("env_s1",cubeSRV.Get());
    };
    PixelPass prepare(r,directory+L"/ogsr_ssgi_source_ao.ps-bent.cso");
    for (auto* forbidden:{"s_ssgi","s_ssgi_source","s_accumulator"})
    {
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        require(FAILED(prepare.reflection->GetResourceBindingDescByName(forbidden,&binding)),"Source pass reads lighting feedback or its own render target");
    }
    source.fill(ctx,{0.2f,0,0,0.7f}); configure(prepare); prepare.set("L_ambient",{0.5f,0.5f,0.5f,0}); prepare.draw(source,true);
    auto lit=source.read(d,ctx)[136];
    require(lit.x>0.25f && lit.y>0.05f,"Environment diffuse missing from source");
    require(std::abs(lit.w-0.7f)<0.002f,"Source preparation damaged emissive alpha");
    source.fill(ctx,{0.2f,0,0,0.7f}); configure(prepare); prepare.set("L_ambient",{}); prepare.draw(source,true);
    auto dark=source.read(d,ctx)[136]; require(std::abs(dark.x-0.2f)<0.002f && dark.y==0,"Unlit environment invented energy");
    // AO still occludes the environment source. Direct light/emissive survive.
    ao.fill(ctx,{0,0.5f,0.5f,0}); source.fill(ctx,{0.2f,0,0,0.7f}); configure(prepare);
    prepare.set("L_ambient",{0.5f,0.5f,0.5f,0}); prepare.draw(source,true);
    require(source.read(d,ctx)[136].y<lit.y*0.75f,"Environment source ignored AO");
    PixelPass combine(r,directory+L"/combine_1.ps-enabled-1-off.cso");
    float composed[3]{}; int index=0;
    for (float intensity:{0.f,1.f,4.f})
    {
        configure(combine); combine.set("ssgi_params",{intensity,0,0,0}); combine.draw(scene);
        composed[index++]=scene.read(d,ctx)[136].x;
    }
    require(composed[0]==0 && composed[1]>0.1f && composed[2]>composed[1]*1.5f,"Normal composition lost GI or intensity control");
    PixelPass debug(r,directory+L"/ogsr_ssgi_debug.ps-ssgi.cso");
    configure(debug); debug.set("ssgi_params",{1,3,0,0}); debug.draw(output);
    for (auto p:output.read(d,ctx)) require(std::abs(p.x-0.5f)<0.002f,"History overlay failed at display resolution");
    r.checkMessages();
    std::cout << "PASS: diffuse environment source, additive emissive preservation, AO, normal composition intensity 0/1/4, display-sized history overlay\n";
}

float testGatherRadiance(Runner& r, unsigned w, unsigned h)
{
    auto* d=r.device.Get(); auto* ctx=r.context.Get();
    unsigned hw=(w+1)/2, hh=(h+1)/2;
    Texture position(d,w,h), source(d,w,h), albedo(d,w,h), noisy(d,hw,hh), geometry(d,hw,hh);
    Constants c=makeConstants(w,h);
    // Match square pixels at both resolutions, with a 60 degree vertical FOV.
    const float vertical=std::tan(3.14159265f/6), horizontal=vertical*w/h;
    c.ndc={2*horizontal,-2*vertical,-horizontal,vertical};
    std::vector<Pixel> p(w*h), light(w*h);
    for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x)
    {
        float rayX=(x+0.5f)*c.ndc.x/w+c.ndc.z;
        float sideDepth=rayX<0 ? -1/rayX : 1e10f;
        bool side=sideDepth<3;
        p[y*w+x]=side ? Pixel{1,0.5f,sideDepth,0} : Pixel{1,1,3,0};
        light[y*w+x]=side ? Pixel{1,0,0,0} : Pixel{};
    }
    position.upload(ctx,p); source.upload(ctx,light); albedo.fill(ctx,{1,1,1,0});
    r.run(0,c,{&position,&source,&albedo},{&noisy,&geometry});
    auto values=noisy.read(d,ctx);
    float sum=0; unsigned count=0, lit=0;
    for (unsigned y=hh*9/20;y<hh*11/20;++y) for (unsigned x=hw*9/20;x<hw*11/20;++x)
    {
        float value=values[y*hw+x].x;
        sum+=value; ++count; if (value>0.001f) ++lit;
    }
    float average=sum/std::max(1u,count);
    std::cout << "MEASURE: corner center " << w << 'x' << h << ": irradiance/radiance=" << average
              << ", nonzero coverage=" << float(lit)/std::max(1u,count) << '\n';
    r.checkMessages();
    return average;
}

int wmain(int argc,wchar_t** argv)
{
    try
    {
        require(argc==2,"Usage: ssgi_gpu_validation <compiled shader directory>");
        Runner r(argv[1]);
        testSourceAndComposition(r,argv[1]);
        float lowResolution=testGatherRadiance(r,192,108);
        float fullResolution=testGatherRadiance(r,1920,1080);
        require(lowResolution>0.05f && fullResolution>0.05f,"Corner bounce lost most of its energy");
        require(fullResolution/lowResolution>0.75f && fullResolution/lowResolution<1.25f,"World-space gather changes with resolution");
        for (auto size: {std::pair{64u,48u},std::pair{65u,49u},std::pair{1u,1u},std::pair{3u,5u}}) testSize(r,size.first,size.second);
        std::cout << "PASS: D3D11 constant reflection" << (r.messages ? " and debug layer (no warnings/errors)" : " (debug layer unavailable)") << '\n';
        return 0;
    }
    catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
