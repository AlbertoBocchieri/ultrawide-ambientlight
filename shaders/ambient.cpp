#include "ambient.h"
#include "ambient_main_bin.h"
#include <cmath>

struct alignas(16) AmbientParams
{
    float rect[4] = {};
    float step[2] = {};
    float mix = 1, strength = 1;
    UINT mode = 0, encoding = 0, outputLinear = 0;
    float mip = 0;
};

HRESULT AmbientBackground::Initialize(ComPtr<ID3D11Device> device)
{
    if (m_device == device && m_shader && m_params && m_sampler) return S_OK;
    m_device = device;
    m_linear.Clear();
    for (auto& t : m_levels) t.Clear();
    for (auto& t : m_history) t.Clear();
    Reset();
    HRESULT hr = device->CreateComputeShader(g_ambient_main, sizeof(g_ambient_main), nullptr, &m_shader);
    RETURN_IF_FAILED(hr);
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(AmbientParams);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = device->CreateBuffer(&bd, nullptr, &m_params);
    RETURN_IF_FAILED(hr);
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_MIRROR;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    return device->CreateSamplerState(&sd, &m_sampler);
}

HRESULT AmbientBackground::Render(ID3D11DeviceContext* ctx, TextureView desktop, TextureView output,
                                  RECT video, UINT encoding, float radius, float strength, int transitionMs)
{
    D3D11_TEXTURE2D_DESC out = {}, src = {};
    output.GetTexture()->GetDesc(&out);
    desktop.GetTexture()->GetDesc(&src);
    if (video.left < 0 || video.top < 0 || video.right > LONG(src.Width) ||
        video.bottom > LONG(src.Height) || video.right <= video.left || video.bottom <= video.top)
        return E_INVALIDARG;
    const UINT vw = video.right - video.left, vh = video.bottom - video.top;
    const float scale = std::min(1.0f, 320.0f / std::max(out.Width, out.Height));
    const UINT w = std::max(1u, UINT(std::lround(out.Width * scale)));
    const UINT h = std::max(1u, UINT(std::lround(out.Height * scale)));
    D3D11_TEXTURE2D_DESC old = {};
    if (m_levels[0].GetTexture()) m_levels[0].GetTexture()->GetDesc(&old);
    if (!EqualRect(&video, &m_video) || old.Width != w || old.Height != h) Reset();
    m_video = video;
    const auto fp16 = DXGI_FORMAT_R16G16B16A16_FLOAT;
    HRESULT hr = m_linear.RecreateTexture(m_device.Get(), fp16, vw, vh, 0, true);
    RETURN_IF_FAILED(hr);
    hr = m_levels[0].RecreateTexture(m_device.Get(), fp16, w, h);
    RETURN_IF_FAILED(hr);
    for (auto& t : m_history) {
        hr = t.RecreateTexture(m_device.Get(), fp16, w, h);
        RETURN_IF_FAILED(hr);
    }
    AmbientParams p;
    p.encoding = encoding;
    p.outputLinear = out.Format == fp16;
    auto dispatch = [&](TextureView dst, TextureView source, TextureView previous = {}) {
        D3D11_TEXTURE2D_DESC d = {};
        dst.GetTexture()->GetDesc(&d);
        ctx->UpdateSubresource(m_params.Get(), 0, nullptr, &p, 0, 0);
        ctx->CSSetShader(m_shader.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, m_params.GetAddressOf());
        ctx->CSSetSamplers(0, 1, m_sampler.GetAddressOf());
        ID3D11ShaderResourceView* srvs[] = {source.GetSRV(), previous.GetSRV()};
        ID3D11UnorderedAccessView* uav = dst.GetUAV();
        ctx->CSSetShaderResources(0, 2, srvs);
        ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        ctx->Dispatch((d.Width + 15) / 16, (d.Height + 15) / 16, 1);
        srvs[0] = srvs[1] = nullptr;
        uav = nullptr;
        ctx->CSSetShaderResources(0, 2, srvs);
        ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    };
    p.rect[0] = float(video.left); p.rect[1] = float(video.top);
    dispatch(m_linear, desktop);
    ctx->GenerateMips(m_linear.GetSRV());

    const float aspect = float(out.Width) / out.Height;
    const float sx = std::min(1.0f, aspect / (float(vw) / vh));
    const float sy = std::min(1.0f, (float(vw) / vh) / aspect);
    p.mode = 1;
    p.rect[0] = (1 - sx) / 2; p.rect[1] = (1 - sy) / 2;
    p.rect[2] = sx; p.rect[3] = sy;
    p.mip = std::max(0.0f, std::log2(std::max(vw * sx / w, vh * sy / h)));
    dispatch(m_levels[0], m_linear);

    const float r = radius * scale;
    int passes = r > 0 ? std::clamp(int(std::ceil(std::log(1 + r*r / (1.8f*1.8f)) / std::log(4.0f))), 2, 9) : 0;
    float offset = passes ? r / std::sqrt(std::pow(4.0f, passes) - 1) : 0;
    if (offset < 1 && passes > 2) offset = r / std::sqrt(std::pow(4.0f, --passes) - 1);
    if (offset > 1.8f && passes < 10) offset = r / std::sqrt(std::pow(4.0f, ++passes) - 1);
    UINT pw = w, ph = h;
    for (int i = 0; i < passes; ++i) {
        if (pw == 1 && ph == 1) { passes = i; break; }
        p.mode = 2; p.step[0] = offset / pw; p.step[1] = offset / ph;
        pw = std::max(1u, pw / 2); ph = std::max(1u, ph / 2);
        hr = m_levels[i+1].RecreateTexture(m_device.Get(), fp16, pw, ph);
        RETURN_IF_FAILED(hr);
        dispatch(m_levels[i+1], m_levels[i]);
    }
    for (int i = passes - 1; i >= 0; --i) {
        D3D11_TEXTURE2D_DESC d = {};
        m_levels[i+1].GetTexture()->GetDesc(&d);
        p.mode = 3; p.step[0] = offset / d.Width; p.step[1] = offset / d.Height;
        dispatch(m_levels[i], m_levels[i+1]);
    }
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&freq);
    const double elapsed = double(now.QuadPart - m_lastTime) * 1000 / freq.QuadPart;
    p.mode = 4;
    p.mix = m_valid && elapsed > 0 && elapsed <= 500 && transitionMs > 0
        ? float(-std::expm1(-std::min(elapsed, 250.0) / transitionMs)) : 1;
    const UINT next = m_valid ? 1 - m_index : 0;
    dispatch(m_history[next], m_levels[0], m_valid ? m_history[m_index] : TextureView());
    m_index = next; m_valid = true; m_lastTime = now.QuadPart;
    p.mode = 5; p.strength = strength;
    p.rect[0] = float(video.left); p.rect[1] = float(video.top);
    p.rect[2] = float(video.right); p.rect[3] = float(video.bottom);
    dispatch(output, m_history[m_index]);
    return S_OK;
}
