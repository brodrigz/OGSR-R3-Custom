// Headless D3D11/WARP checks for the actual compiled RSM shaders.
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
    Pixel map, sun;
    float worldToLight[16]{}, lightToWorld[16]{}, worldToView[16]{};
};
static_assert(sizeof(Constants) == 448);

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
    bool fullPrecision;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    Texture(ID3D11Device* device, unsigned w, unsigned h, bool fp32=false) : width(w), height(h), fullPrecision(fp32)
    {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w; d.Height = h; d.ArraySize = d.MipLevels = d.SampleDesc.Count = 1;
        d.Format = fp32 ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&d, nullptr, &texture));
        check(device->CreateShaderResourceView(texture.Get(), nullptr, &srv));
        check(device->CreateUnorderedAccessView(texture.Get(), nullptr, &uav));
    }
    void upload(ID3D11DeviceContext* context, const std::vector<Pixel>& pixels)
    {
        require(pixels.size() == size_t(width) * height, "Wrong upload size");
        if (fullPrecision)
        {
            context->UpdateSubresource(texture.Get(),0,nullptr,pixels.data(),width*sizeof(Pixel),0);
            return;
        }
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
            if (fullPrecision)
            {
                std::memcpy(result.data()+y*width,static_cast<const char*>(map.pData)+y*map.RowPitch,width*sizeof(Pixel));
                continue;
            }
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
            check(D3DReadFileToBlob((directory + L"/ogsr_rsm_" + names[i] + L".cs-rsm.cso").c_str(), &blob));
            check(device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shaders[i]));
            ComPtr<ID3D11ShaderReflection> reflection;
            check(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&reflection)));
            D3D11_SHADER_BUFFER_DESC desc{};
            check(reflection->GetConstantBufferByName("RSMConstants")->GetDesc(&desc));
            require(desc.Size == sizeof(Constants), "C++/HLSL constant buffer size mismatch");
            for (unsigned v = 0; v < desc.Variables; ++v)
            {
                D3D11_SHADER_TYPE_DESC type{};
                check(reflection->GetConstantBufferByName("RSMConstants")->GetVariableByIndex(v)->GetType()->GetDesc(&type));
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
    void draw(Texture& output, bool additive=false, Texture* second=nullptr, ID3D11DepthStencilView* depthTarget=nullptr)
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
        ComPtr<ID3D11RenderTargetView> rtv2;
        if(second) check(r.device->CreateRenderTargetView(second->texture.Get(),nullptr,&rtv2));
        ID3D11RenderTargetView* views[]={rtv.Get(),rtv2.Get()};
        ctx->OMSetRenderTargets(second ? 2 : 1,views,depthTarget);
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
void testHistory(Runner& r, unsigned w, unsigned h)
{
    auto* d = r.device.Get(); auto* ctx = r.context.Get();
    unsigned hw = (w + 1) / 2, hh = (h + 1) / 2;
    Texture position(d,w,h), velocity(d,w,h);
    Texture noisy(d,hw,hh), geometry(d,hw,hh), oldGeometry(d,hw,hh), oldHistory(d,hw,hh), history(d,hw,hh), filtered(d,hw,hh), resolved(d,w,h);
    Constants c = makeConstants(w,h);
    velocity.fill(ctx, {});
    auto temporal = [&] { r.run(1,c,{&noisy,&geometry,&oldHistory,&oldGeometry,&velocity},{&history}); };
    auto read = [&](Texture& t) { return t.read(d,ctx); };
    auto black = [&](Texture& t, const char* label) { for (auto p : read(t)) require(std::abs(p.x)+std::abs(p.y)+std::abs(p.z)<1e-5f,label); };

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
    std::cout << "PASS: WARP RSM " << w << 'x' << h << " lighting, history, filtering, bounds and finite outputs\n";
}

float testBounce(Runner& r,unsigned width,unsigned height,unsigned mapWidth,bool blocker=false)
{
    auto* d=r.device.Get(); auto* ctx=r.context.Get();
    unsigned hw=(width+1)/2,hh=(height+1)/2;
    Texture position(d,width,height), albedo(d,mapWidth,mapWidth), capture(d,mapWidth,mapWidth,true), depth(d,mapWidth,mapWidth,true);
    Texture noisy(d,hw,hh), geometry(d,hw,hh), diagnostics(d,hw,hh);
    Constants c=makeConstants(width,height);
    c.ndc={1,-1,-0.5f,0.5f}; c.trace={4,0.25f,0,2};
    c.map={float(mapWidth),1.f/mapWidth,8,0.1f}; c.sun={1,1,1,0};
    // Sun at +Z: source plane at z=1, small receiver at z=3, facing -Z.
    for (int i=0;i<16;i+=5) c.worldToView[i]=1;
    c.worldToLight[0]=c.worldToLight[5]=0.25f; c.worldToLight[10]=-0.1f; c.worldToLight[14]=0.5f; c.worldToLight[15]=1;
    c.lightToWorld[0]=c.lightToWorld[5]=4; c.lightToWorld[10]=-10; c.lightToWorld[14]=5; c.lightToWorld[15]=1;
    std::vector<Pixel> p(width*height), a(mapWidth*mapWidth), cap(mapWidth*mapWidth), dep(mapWidth*mapWidth,Pixel{1,0,0,0});
    for (unsigned y=height/3;y<height*2/3;++y) for (unsigned x=width/3;x<width*2/3;++x) p[y*width+x]={1,1,3,0};
    // Every emitter lies outside the camera's horizontal frustum at z=1.
    // No emitting surface appears anywhere in the camera gbuffer.
    for (unsigned y=0;y<mapWidth;++y) for (unsigned x=0;x<mapWidth;++x)
    {
        float wx=((x+0.5f)/mapWidth*2-1)*4, wy=(1-(y+0.5f)/mapWidth*2)*4;
        if (wx>=-2.5f && wx<=-1 && std::abs(wy)<2)
        {
            a[y*mapWidth+x]={1,0,0,1}; cap[y*mapWidth+x]={float(512+512*1024),0.4f,0,0}; dep[y*mapWidth+x].x=0.4f;
        }
        // An unlit opaque blocker at z=2, between receiver and red source.
        if (blocker && wx>-1.5f && wx<-0.35f && std::abs(wy)<2)
        {
            a[y*mapWidth+x]={}; cap[y*mapWidth+x]={0,0.3f,0,0}; dep[y*mapWidth+x].x=0.3f;
        }
    }
    position.upload(ctx,p); albedo.upload(ctx,a); capture.upload(ctx,cap); depth.upload(ctx,dep);
    auto evaluate=[&] { r.run(0,c,{&position,&albedo,&capture,&depth},{&noisy,&geometry,&diagnostics}); };
    evaluate(); auto result=noisy.read(d,ctx);
    for(auto diagnostic:diagnostics.read(d,ctx))
        if(diagnostic.z>0)
            require(std::abs(diagnostic.x+diagnostic.y-1)<0.001f,"Accepted/rejected diagnostics use different denominators");
    float sum=0; unsigned count=0;
    for (unsigned y=hh*9/20;y<hh*11/20;++y) for (unsigned x=hw*9/20;x<hw*11/20;++x)
    {
        auto value=result[y*hw+x]; sum+=value.x; ++count;
        require(value.y==0 && value.z==0,"Red RSM source contaminated other channels");
    }
    float average=sum/std::max(1u,count);
    std::cout << "MEASURE: off-camera red source " << width << 'x' << height << " map " << mapWidth
              << (blocker ? " blocked" : " open") << " mean bounce=" << average << '\n';
    auto black=[&](const char* message) { for (auto value:noisy.read(d,ctx)) require(std::abs(value.x)+std::abs(value.y)+std::abs(value.z)<1e-6f,message); };
    c.sun={}; evaluate(); black("Zero sunlight retained RSM bounce"); c.sun={1,1,1,0};
    // Simulate an unsupported, closer depth-only caster replacing a lit texel.
    for (auto& value:dep) if (value.x<1) value.x-=0.01f;
    depth.upload(ctx,dep); evaluate(); black("Depth-only caster inherited stale source albedo");
    depth.fill(ctx,{1,0,0,0}); evaluate(); black("Empty shadow map emitted light");
    depth.upload(ctx,std::vector<Pixel>(mapWidth*mapWidth,Pixel{0.4f,0,0,0}));
    position.fill(ctx,{}); evaluate(); black("Sky received bounce");
    position.fill(ctx,{1,1,0.2f,0}); evaluate(); black("HUD received bounce");
    r.checkMessages();
    return average;
}

void testCorner(Runner& r, float sunElevation, float wallDistance, float terrainShadowOffset=0, float thickness=0.2f)
{
    // Continuous floor and wall, both present in camera and sun depth. Unlike
    // the isolated-emitter test, every visibility sample sees real geometry.
    auto* d=r.device.Get(); auto* ctx=r.context.Get();
    const unsigned w=256,h=144,m=256,hw=w/2,hh=h/2;
    Texture position(d,w,h), albedo(d,m,m), capture(d,m,m,true), depth(d,m,m,true);
    Texture noisy(d,hw,hh), geometry(d,hw,hh), diagnostics(d,hw,hh);
    Constants c=makeConstants(w,h); c.ndc={2,-1.125f,-1,0.5625f}; c.trace={6,thickness,0,2};
    const float sn=std::sin(sunElevation),cs=std::cos(sunElevation),mapSize=40,range=600;
    c.map={float(m),1.f/m,mapSize,1/range}; c.sun={1,1,1,0};
    for(int i=0;i<16;i+=5) c.worldToView[i]=1;
    c.worldToLight[0]=2/mapSize;
    c.worldToLight[5]=cs*2/mapSize; c.worldToLight[9]=sn*2/mapSize;
    c.worldToLight[6]=-sn/range; c.worldToLight[10]=cs/range;
    c.worldToLight[13]=-wallDistance*sn*2/mapSize;
    c.worldToLight[14]=(500-wallDistance*cs)/range; c.worldToLight[15]=1;
    c.lightToWorld[0]=mapSize/2;
    c.lightToWorld[5]=cs*mapSize/2; c.lightToWorld[9]=-sn*range;
    c.lightToWorld[6]=sn*mapSize/2; c.lightToWorld[10]=cs*range;
    c.lightToWorld[13]=500*sn; c.lightToWorld[14]=wallDistance-500*cs; c.lightToWorld[15]=1;
    std::vector<Pixel> p(w*h),a(m*m),cap(m*m),dep(m*m,Pixel{1,0,0,0});
    for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x)
    {
        float vy=(y+0.5f)/h*c.ndc.y+c.ndc.w;
        float z=vy<0 ? -1.5f/vy : 1e10f;
        p[y*w+x]=z<wallDistance ? Pixel{0.5f,1,z,0} : Pixel{1,1,wallDistance,0};
    }
    for(unsigned y=0;y<m;++y) for(unsigned x=0;x<m;++x)
    {
        float v=(1-(y+0.5f)/m*2)*mapSize/2;
        float oy=v*cs+500*sn, oz=wallDistance+v*sn-500*cs;
        float floorT=(oy+1.5f+terrainShadowOffset)/sn, wallT=(wallDistance-oz)/cs;
        bool floor=floorT<wallT; float z=std::min(floorT,wallT)/range;
        a[y*m+x]=floor ? Pixel{1,0,0,1} : Pixel{0,0,0,1};
        cap[y*m+x]={float(floor ? 512+1023*1024 : 1023+1023*1024),z,0,0};
        dep[y*m+x].x=z;
    }
    position.upload(ctx,p); albedo.upload(ctx,a); capture.upload(ctx,cap); depth.upload(ctx,dep);
    r.run(0,c,{&position,&albedo,&capture,&depth},{&noisy,&geometry,&diagnostics});
    auto values=noisy.read(d,ctx),diags=diagnostics.read(d,ctx);
    float sum=0,rejection=0; unsigned count=0;
    for(unsigned y=hh/2;y<hh;++y) for(unsigned x=hw*2/5;x<hw*3/5;++x)
    {
        float vy=(y*2+0.5f)/h*c.ndc.y+c.ndc.w;
        float worldY=vy*wallDistance;
        if(worldY<-1.25f || worldY>-0.25f) continue;
        sum+=values[y*hw+x].x; rejection+=diags[y*hw+x].x; ++count;
    }
    require(count>0,"Corner test has no receivers");
    std::cout << "MEASURE: continuous corner sun=" << sunElevation << " wall=" << wallDistance
              << " terrain offset=" << terrainShadowOffset << " thickness=" << thickness << " bounce=" << sum/count << " rejection=" << rejection/count << '\n';
    require(sum/count>0.005f,"Continuous sunlit floor failed to light adjacent wall");
    r.checkMessages();
}

void testComposition(Runner& r,const std::wstring& directory)
{
    auto* d=r.device.Get(); auto* ctx=r.context.Get();
    Texture position(d,16,16), albedo(d,16,16), accum(d,16,16), gi(d,16,16), scene(d,16,16), overlay(d,32,32);
    position.fill(ctx,{1,1,3,2}); albedo.fill(ctx,{1,1,1,0.5f}); accum.fill(ctx,{}); gi.fill(ctx,{0.1f,0.05f,0.025f,12});
    D3D11_TEXTURE2D_DESC cd{}; cd.Width=cd.Height=cd.MipLevels=cd.SampleDesc.Count=1; cd.ArraySize=6;
    cd.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; cd.BindFlags=D3D11_BIND_SHADER_RESOURCE; cd.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
    std::array<uint16_t,4> black{}; std::array<D3D11_SUBRESOURCE_DATA,6> faces{};
    for(auto& face:faces) {face.pSysMem=black.data();face.SysMemPitch=8;}
    ComPtr<ID3D11Texture2D> cube; ComPtr<ID3D11ShaderResourceView> cubeSRV;
    check(d->CreateTexture2D(&cd,faces.data(),&cube)); check(d->CreateShaderResourceView(cube.Get(),nullptr,&cubeSRV));
    auto configure=[&](PixelPass& pass) {
        ctx->ClearState(); auto c=makeConstants(16,16); pass.set("m_inv_V",c.inverseView,12*sizeof(float));
        pass.set("pos_decompression_params",{1,-1,2.f/16,-2.f/16}); pass.set("pos_decompression_params2",{16,16,1.f/16,1.f/16});
        pass.bind("s_position",position.srv.Get()); pass.bind("s_diffuse",albedo.srv.Get()); pass.bind("s_accumulator",accum.srv.Get());
        pass.bind("s_rsm",gi.srv.Get()); pass.bind("env_s0",cubeSRV.Get()); pass.bind("env_s1",cubeSRV.Get());
    };
    PixelPass combine(r,directory+L"/combine_1.ps-enabled-1-off.cso");
    float values[3]{}; int i=0;
    for(float intensity:{0.f,1.f,4.f})
    {
        configure(combine); combine.set("rsm_params",{intensity,0,0,0}); combine.draw(scene);
        values[i++]=scene.read(d,ctx)[136].x;
    }
    require(values[0]==0 && values[1]>0.1f && values[2]>values[1]*1.5f,"Normal composition lost bounce or intensity control");
    PixelPass debug(r,directory+L"/ogsr_rsm_debug.ps-rsm.cso");
    configure(debug); debug.bind("s_rsm_albedo",albedo.srv.Get()); debug.bind("s_rsm_diagnostics",gi.srv.Get());
    debug.set("rsm_params",{1,1,1,0}); debug.draw(overlay);
    for(auto value:overlay.read(d,ctx)) require(std::abs(value.x-values[1])<0.002f,"Debug bounce differs from normal contribution at display resolution");
    r.checkMessages(); std::cout << "PASS: normal composition intensity 0/1/4 and display-sized bounce diagnostic\n";
}

void testCapture(Runner& r,const std::wstring& directory)
{
    auto* d=r.device.Get(); auto* ctx=r.context.Get();
    Texture base(d,4,4), albedo(d,16,16), geometry(d,16,16,true);
    Texture mask(d,1,1), red(d,1,1), green(d,1,1), blue(d,1,1), fourth(d,1,1);
    mask.fill(ctx,{1,2,3,4}); red.fill(ctx,{1,0.25f,0.125f,1}); green.fill(ctx,{0.125f,1,0.25f,1});
    blue.fill(ctx,{0.25f,0.125f,1,1}); fourth.fill(ctx,{0.5f,0.5f,0.5f,1});
    base.fill(ctx,{0.75f,0.25f,0.125f,1});
    struct Vertex {float p[4],n[3],t[3],b[3],uv[2];};
    const Vertex vertices[]={{{-1,1,0.4f,1},{0,0,1},{1,0,0},{0,1,0},{0,0}},
                             {{3,1,0.4f,1},{0,0,1},{1,0,0},{0,1,0},{2,0}},
                             {{-1,-3,0.4f,1},{0,0,1},{1,0,0},{0,1,0},{0,2}}};
    D3D11_BUFFER_DESC vbDesc{}; vbDesc.ByteWidth=sizeof(vertices); vbDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=vertices;
    ComPtr<ID3D11Buffer> vb; check(d->CreateBuffer(&vbDesc,&initial,&vb));
    // Full static level layout: positions, packed frame vectors, signed UVs.
    // OGF_FASTPATH drops the texture attributes, which cannot drive RSM capture.
    struct StaticVertex {float p[3]; uint32_t n,t,b; int16_t uv[2];};
    const StaticVertex staticVertices[]={{{-1,1,0.4f},0,0,0,{512,512}},
                                        {{3,1,0.4f},0,0,0,{512,512}},
                                        {{-1,-3,0.4f},0,0,0,{512,512}}};
    vbDesc.ByteWidth=sizeof(staticVertices); initial.pSysMem=staticVertices;
    ComPtr<ID3D11Buffer> staticVB; check(d->CreateBuffer(&vbDesc,&initial,&staticVB));
    D3D11_TEXTURE2D_DESC depthDesc{}; depthDesc.Width=depthDesc.Height=16; depthDesc.ArraySize=depthDesc.MipLevels=depthDesc.SampleDesc.Count=1;
    depthDesc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT; depthDesc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depth; ComPtr<ID3D11DepthStencilView> dsv;
    check(d->CreateTexture2D(&depthDesc,nullptr,&depth)); check(d->CreateDepthStencilView(depth.Get(),nullptr,&dsv));
    for(const std::wstring kind:{L"model",L"model_aref",L"base",L"base_aref",L"terrain"})
    {
        const bool aref=kind.find(L"aref")!=std::wstring::npos;
        const bool isStatic=kind.find(L"model")==std::wstring::npos;
        const bool terrain=kind==L"terrain";
        PixelPass pass(r,directory+(terrain ? L"/shadow_direct_terrain.ps-rsm.cso" : aref ? L"/shadow_direct_base_aref.ps-enabled-1.cso" : L"/shadow_direct_base.ps-enabled-1.cso"));
        ComPtr<ID3DBlob> blob;
        check(D3DReadFileToBlob((directory+L"/shadow_direct_"+kind+L".vs-enabled-1"+(isStatic ? L"" : L"-skin-NONE")+L".cso").c_str(),&blob));
        pass.vs.Reset(); check(d->CreateVertexShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&pass.vs));
        D3D11_INPUT_ELEMENT_DESC layout[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,28,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"BINORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,52,D3D11_INPUT_PER_VERTEX_DATA,0}};
        if(isStatic)
        {
            layout[0].Format=DXGI_FORMAT_R32G32B32_FLOAT;
            for(unsigned i=1;i<=3;++i) {layout[i].Format=DXGI_FORMAT_R8G8B8A8_UNORM;layout[i].AlignedByteOffset=12+(i-1)*4;}
            layout[4].Format=DXGI_FORMAT_R16G16_SINT;layout[4].AlignedByteOffset=24;
            // Reproduce the engine mismatch explicitly, then clear only these
            // expected validation messages before checking the corrected draw.
            r.checkMessages();
            ComPtr<ID3D11InputLayout> reducedLayout;
            HRESULT reducedResult=d->CreateInputLayout(layout,1,blob->GetBufferPointer(),blob->GetBufferSize(),&reducedLayout);
            require(FAILED(reducedResult),"RSM unexpectedly accepted a position-only static shadow layout");
            if(r.messages) r.messages->ClearStoredMessages();
        }
        ComPtr<ID3D11InputLayout> il; check(d->CreateInputLayout(layout,5,blob->GetBufferPointer(),blob->GetBufferSize(),&il));
        ComPtr<ID3D11ShaderReflection> vr; check(D3DReflect(blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&vr)));
        auto* reflected=vr->GetConstantBufferByName("dynamic_transforms"); D3D11_SHADER_BUFFER_DESC bdesc{}; check(reflected->GetDesc(&bdesc));
        std::vector<char> data(bdesc.Size);
        float identity[16]{}; for(int i=0;i<16;i+=5) identity[i]=1;
        for(const char* name:{"m_WVP","m_W"})
        {
            D3D11_SHADER_VARIABLE_DESC v{}; check(reflected->GetVariableByName(name)->GetDesc(&v));
            std::memcpy(data.data()+v.StartOffset,identity,v.Size);
        }
        D3D11_BUFFER_DESC cbDesc{}; cbDesc.ByteWidth=bdesc.Size; cbDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        initial.pSysMem=data.data(); ComPtr<ID3D11Buffer> cb; check(d->CreateBuffer(&cbDesc,&initial,&cb));
        D3D11_SHADER_INPUT_BIND_DESC binding{}; check(vr->GetResourceBindingDescByName("dynamic_transforms",&binding));
        for(float alpha:{1.f,0.f})
        {
            ctx->ClearState();
            if(isStatic)
            {
                std::vector<Pixel> texels(16,Pixel{0,0,1,alpha});
                texels[10]={0.75f,0.25f,0.125f,alpha};
                base.upload(ctx,texels); // Correct unpacked UVs must reach this texel.
            }
            else base.fill(ctx,{0.75f,0.25f,0.125f,alpha});
            albedo.fill(ctx,{}); geometry.fill(ctx,{});
            ctx->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,1,0);
            auto* vertexBuffer=isStatic ? staticVB.Get() : vb.Get(); UINT stride=isStatic ? sizeof(StaticVertex) : sizeof(Vertex),offset=0;
            ctx->IASetInputLayout(il.Get()); ctx->IASetVertexBuffers(0,1,&vertexBuffer,&stride,&offset);
            auto* buffer=cb.Get(); ctx->VSSetConstantBuffers(binding.BindPoint,1,&buffer);
            const Pixel direction{0,0,-1,0}; pass.set("L_sun_dir_w",&direction,12);
            pass.set("m_taa_jitter",{0,0,0,0.5f}); pass.bind("s_base",base.srv.Get());
            if(terrain)
            {
                pass.set("dt_params",{8,8,8,1});
                pass.bind("s_mask",mask.srv.Get()); pass.bind("s_dt_r",red.srv.Get()); pass.bind("s_dt_g",green.srv.Get());
                pass.bind("s_dt_b",blue.srv.Get()); pass.bind("s_dt_a",fourth.srv.Get());
            }
            pass.draw(albedo,false,&geometry,dsv.Get());
            auto a=albedo.read(d,ctx)[136],g=geometry.read(d,ctx)[136];
            if(aref && alpha==0)
                require(a.w==0 && g.x==0,"Alpha-tested hole wrote an RSM emitter");
            else
            {
                require(std::abs(a.x-(terrain?0.6f:0.75f))<0.002f && a.w==1,"Capture lost material color/validity");
                if(terrain) require(std::abs(a.y-0.23125f)<0.002f && std::abs(a.z-0.140625f)<0.002f,"Terrain capture lost normalized mask/detail layer tint");
                require(g.x==float(512+512*1024) && std::abs(g.y-0.4f)<1e-7f,"Capture normal/depth encoding failed");
            }
            // Compare the MRT's stored depth against the actual D24 raster depth.
            D3D11_TEXTURE2D_DESC stagingDesc=depthDesc; stagingDesc.BindFlags=0; stagingDesc.Usage=D3D11_USAGE_STAGING; stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging; check(d->CreateTexture2D(&stagingDesc,nullptr,&staging)); ctx->CopyResource(staging.Get(),depth.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{}; check(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
            auto row=reinterpret_cast<const uint32_t*>(static_cast<const char*>(mapped.pData)+8*mapped.RowPitch);
            float z=float(row[8]&0xffffff)/float(0xffffff); ctx->Unmap(staging.Get(),0);
            require(aref && alpha==0 ? z==1 : std::abs(z-g.y)<0.0000003f,"Capture depth differs from shadow depth or alpha hole wrote depth");
        }
    }
    r.checkMessages(); std::cout << "PASS: model, static and terrain capture, packed UVs, reduced-mesh rejection, alpha holes, normal packing and D24 depth agreement\n";
}

int wmain(int argc,wchar_t** argv)
{
    try
    {
        require(argc==2,"Usage: rsm_gpu_validation <compiled shader directory>"); Runner r(argv[1]);
        testCapture(r,argv[1]);
        float low=testBounce(r,128,72,128), high=testBounce(r,512,288,512), blocked=testBounce(r,128,72,128,true);
        require(low>0.002f && high>0.002f,"Off-camera sources produced negligible bounce");
        require(high/low>0.7f && high/low<1.3f,"RSM brightness depends on resolution");
        require(blocked<low*0.5f,"Visible blocker failed to suppress bounce");
        for(float elevation:{0.15f,0.5f,1.2f}) for(float distance:{6.f,20.f,50.f}) testCorner(r,elevation,distance);
        for(float elevation:{0.15f,0.5f,1.2f}) testCorner(r,elevation,6,0.1f);
        for(float elevation:{0.15f,0.5f,1.2f}) testCorner(r,elevation,6,0,1.f);
        for(auto size:{std::pair{64u,48u},std::pair{65u,49u},std::pair{1u,1u},std::pair{3u,5u}}) testHistory(r,size.first,size.second);
        testComposition(r,argv[1]); r.checkMessages();
        std::cout << "PASS: RSM WARP validation and constant reflection" << (r.messages ? ", debug layer clean" : ", debug layer unavailable") << '\n';
        return 0;
    }
    catch(const std::exception& e) {std::cerr << "FAIL: " << e.what() << '\n'; return 1;}
}
