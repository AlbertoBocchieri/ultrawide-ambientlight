#ifdef NDEBUG
#undef NDEBUG
#endif
#include "shaders/ambient.h"
#include "shaders/detect.h"
#include <DirectXPackedVector.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <d3d11sdklayers.h>

using DirectX::PackedVector::XMConvertHalfToFloat;
using DirectX::PackedVector::XMConvertFloatToHalf;

int main(int argc, char** argv)
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    D3D_FEATURE_LEVEL level;
    const bool hardware = argc > 1;
    auto driver = hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
    HRESULT creation = D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_DEBUG,
        nullptr, 0, D3D11_SDK_VERSION, &device, &level, &ctx);
    if (creation == DXGI_ERROR_SDK_COMPONENT_MISSING)
        creation = D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&ctx);
    assert(SUCCEEDED(creation));
    ComPtr<ID3D11InfoQueue> messages;
    device.As(&messages);
    constexpr UINT w = 320, h = 180;
    TextureView desktop, output;
    const auto fp16 = DXGI_FORMAT_R16G16B16A16_FLOAT;
    assert(SUCCEEDED(desktop.RecreateTexture(device.Get(), fp16, w, h)));
    assert(SUCCEEDED(output.RecreateTexture(device.Get(), fp16, w, h)));
    std::vector<uint16_t> pixels(w*h*4);
    auto frame = [&](RECT video, float brightness) {
        for (UINT y=0; y<h; ++y) for (UINT x=0; x<w; ++x) {
            const bool inside = x>=UINT(video.left) && x<UINT(video.right) && y>=UINT(video.top) && y<UINT(video.bottom);
            for (UINT c=0;c<4;++c) pixels[(y*w+x)*4+c] = XMConvertFloatToHalf(c==3 ? 1 : inside ? brightness : 0);
        }
        ctx->UpdateSubresource(desktop.GetTexture(), 0, nullptr, pixels.data(), w*8, 0);
    };
    D3D11_TEXTURE2D_DESC d = {};
    output.GetTexture()->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    assert(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &staging)));
    auto read = [&]() {
        ctx->CopyResource(staging.Get(), output.GetTexture());
        D3D11_MAPPED_SUBRESOURCE map = {};
        assert(SUCCEEDED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)));
        std::vector<float> result(w*h*4);
        for (UINT y=0; y<h; ++y) {
            auto row = reinterpret_cast<const uint16_t*>(static_cast<const char*>(map.pData) + y*map.RowPitch);
            for (UINT x=0;x<w*4;++x) result[y*w*4+x]=XMConvertHalfToFloat(row[x]);
        }
        ctx->Unmap(staging.Get(),0);
        return result;
    };
    auto protectedVideo = [&](const std::vector<float>& result, RECT video) {
        for (LONG y=video.top;y<video.bottom;++y) for (LONG x=video.left;x<video.right;++x)
            for (UINT c=0;c<4;++c) assert(result[(y*w+x)*4+c] == 0);
    };
    AmbientBackground ambient;
    assert(SUCCEEDED(ambient.Initialize(device)));
    RECT video{40,0,280,180};
    frame(video, 2.0f);
    assert(SUCCEEDED(ambient.Render(ctx.Get(),desktop,output,video,2,360,.32f,500)));
    auto result=read();
    protectedVideo(result,video);
    assert(std::abs(result[0] - .64f)<.004f); // HDR values survive blur/history.
    assert(result[3]==1);
    // Shrinking bands must clear the formerly opaque region immediately.
    video={20,0,300,180}; frame(video,.5f);
    assert(SUCCEEDED(ambient.Render(ctx.Get(),desktop,output,video,2,360,.32f,500)));
    result=read(); protectedVideo(result,video);
    assert(std::abs(result[0] - .16f)<.003f);
    // Orientation and complete removal; no stale alpha from old side bars.
    video={0,30,320,150}; frame(video,1);
    assert(SUCCEEDED(ambient.Render(ctx.Get(),desktop,output,video,2,360,.32f,500)));
    protectedVideo(read(),video);
    video={0,0,320,180}; frame(video,1);
    assert(SUCCEEDED(ambient.Render(ctx.Get(),desktop,output,video,2,360,.32f,500)));
    for(float v:read()) assert(v==0);

    // Current-frame detection and the production mask: even a dim visible
    // pixel must be transparent, not 99.9999% opaque as before.
    Detection detection;
    assert(SUCCEEDED(detection.Initialize(device,ctx,w,h,.03f,.7f,true,0,0,fp16,
                                         DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)));
    video={40,0,280,180}; frame(video,.5f);
    for(int i=0;i<4;++i) assert(SUCCEEDED(detection.Detect(ctx.Get(),desktop)));
    auto bars=detection.GetDetectedBars();
    assert(bars.size()>=2 && bars[0].width==40);
    // Visible edge in the next frame releases all affected columns without waiting.
    video={20,0,300,180}; frame(video,.5f);
    assert(SUCCEEDED(detection.Detect(ctx.Get(),desktop,false)));
    assert(detection.GetDetectedBars()[0].width==20);
    const float opaque[4]={.5f,.5f,.5f,1};
    ctx->ClearRenderTargetView(output.GetRTV(),opaque);
    assert(SUCCEEDED(detection.RenderLumaMask(ctx.Get(),output)));
    result=read(); protectedVideo(result,video);
    assert(result[3]==1);
    // Below the old variance cutoff but above the luma threshold.
    frame({0,0,320,180},.001f);
    assert(SUCCEEDED(detection.Detect(ctx.Get(),desktop,false)));
    ctx->ClearRenderTargetView(output.GetRTV(),opaque);
    assert(SUCCEEDED(detection.RenderLumaMask(ctx.Get(),output)));
    for(float v:read()) assert(v==0);

    // SDR input is linearized before smoothing; strength is applied in linear light.
    TextureView sdr;
    assert(SUCCEEDED(sdr.RecreateTexture(device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,w,h)));
    std::vector<UINT> gray(w*h,0xff808080u);
    ctx->UpdateSubresource(sdr.GetTexture(),0,nullptr,gray.data(),w*4,0);
    ambient.Reset(); video={40,0,280,180};
    assert(SUCCEEDED(ambient.Render(ctx.Get(),sdr,output,video,0,0,.32f,0)));
    result=read(); protectedVideo(result,video);
    assert(std::abs(result[0] - std::pow(128.0f/255,2.2f)*.32f)<.002f);
    // Exercise the deferred-context path used by the application, too.
    ComPtr<ID3D11DeviceContext> deferred;
    assert(SUCCEEDED(device->CreateDeferredContext(0,&deferred)));
    ambient.Reset();
    assert(SUCCEEDED(ambient.Render(deferred.Get(),sdr,output,video,0,360,.32f,500)));
    ComPtr<ID3D11CommandList> commands;
    assert(SUCCEEDED(deferred->FinishCommandList(FALSE,&commands)));
    ctx->ExecuteCommandList(commands.Get(),TRUE);
    result=read(); protectedVideo(result,video);
    assert(std::abs(result[0] - std::pow(128.0f/255,2.2f)*.32f)<.002f);
    if (messages) {
        for (UINT64 i=0;i<messages->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
            SIZE_T size=0;
            messages->GetMessage(i,nullptr,&size);
            std::vector<char> storage(size);
            auto message=reinterpret_cast<D3D11_MESSAGE*>(storage.data());
            messages->GetMessage(i,message,&size);
            if (message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR) {
                std::fprintf(stderr,"D3D11: %s\n",message->pDescription);
                return 1;
            }
        }
    }
    std::printf("PASS (%s): HDR, SDR, mask, current-frame shrink, orientation, fullscreen transparency\n", hardware ? "hardware" : "WARP");
    std::printf("Deferred context passed; debug layer %s\n", messages ? "checked" : "unavailable");
}
