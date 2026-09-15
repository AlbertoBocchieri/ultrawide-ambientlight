#pragma once
#include "../common.h"

// A small linear-light background, independent of the video's opaque rectangle.
class AmbientBackground
{
public:
    HRESULT Initialize(ComPtr<ID3D11Device> device);
    void Reset() { m_valid = false; m_lastTime = 0; }
    HRESULT Render(ID3D11DeviceContext* context, TextureView desktop, TextureView output,
                   RECT video, UINT encoding, float radius, float strength, int transitionMs);
private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11ComputeShader> m_shader;
    ComPtr<ID3D11Buffer> m_params;
    ComPtr<ID3D11SamplerState> m_sampler;
    TextureView m_linear, m_levels[11], m_history[2];
    UINT m_index = 0;
    bool m_valid = false;
    INT64 m_lastTime = 0;
    RECT m_video = {};
};
