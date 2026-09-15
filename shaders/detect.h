#pragma once
#include "../common.h"
#include <stdint.h>
#include "DirectXMath.h"
#include <memory>
#include <vector>
#include "dxgi1_6.h"
#include "barStabilizer.h"

using namespace DirectX;

class Detection
{
public:
    enum class LumaEncoding
    {
        SDR,
        HDR10,
        SCRGB
    };

    static constexpr LumaEncoding GetLumaEncoding(DXGI_FORMAT format, DXGI_COLOR_SPACE_TYPE colorSpace)
    {
        if (format == DXGI_FORMAT_R16G16B16A16_FLOAT)
            return LumaEncoding::SCRGB;
        if (format == DXGI_FORMAT_R10G10B10A2_UNORM &&
            colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
            return LumaEncoding::HDR10;
        return LumaEncoding::SDR;
    }

    Detection();
    ~Detection();

    HRESULT Initialize(ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> context, UINT width, UINT height,
        float blackThreshold, float blackRatio, bool symmetricBars, UINT reservedWidth, UINT reservedHeight,
        DXGI_FORMAT format, DXGI_COLOR_SPACE_TYPE colorSpace);
    
    HRESULT Detect(ID3D11DeviceContext* context, TextureView target, bool confirmGrowth = true);
    HRESULT RenderLumaMask(ID3D11DeviceContext* context, TextureView target);

    std::vector<BlackBar> GetDetectedBars();
    static std::vector<BlackBar> GetFixedBars(UINT windowWidth, UINT windowHeight, UINT gameWidth, UINT gameHeight);

private:
    HRESULT CreateBuffers();
    HRESULT DispatchLuma(ID3D11DeviceContext* context, TextureView target);
    HRESULT DispatchRowColAnalysis(ID3D11DeviceContext* context);
    HRESULT FetchRowResults(ID3D11DeviceContext* pContext);
    HRESULT FetchColResults(ID3D11DeviceContext* pContext);
    

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;

    ComPtr<ID3D11ComputeShader> m_lumaShader;
    ComPtr<ID3D11ComputeShader> m_lumaHDR10Shader;
    ComPtr<ID3D11ComputeShader> m_lumaSCRGBShader;

    ComPtr<ID3D11ComputeShader> m_rowAnalysisShader;
    ComPtr<ID3D11ComputeShader> m_colAnalysisShader;

    ComPtr<ID3D11ComputeShader> m_lumaMaskShader;
    ComPtr<ID3D11ComputeShader> m_lumaMaskShaderUNorm;

    TextureView m_luma;
    ComPtr<ID3D11Texture2D> m_lumaStaging;

    ComPtr<ID3D11Buffer> m_rowResults;
    ComPtr<ID3D11UnorderedAccessView> m_rowResultsUAV;
    ComPtr<ID3D11Buffer> m_rowStaging;

    ComPtr<ID3D11Buffer> m_colResults;
    ComPtr<ID3D11UnorderedAccessView> m_colResultsUAV;
    ComPtr<ID3D11Buffer> m_colStaging;

    ComPtr<ID3D11Buffer> m_constants;

    float m_blackThreshold;
	float m_blackVariance;
    float m_blackRatio;

    bool m_symmetricBars;

    // captured texture size
    UINT m_width, m_height;

    // detected black bar sizes
    UINT m_topBar, m_bottomBar;
    UINT m_leftBar, m_rightBar;
    BarStabilizer m_topStabilizer, m_bottomStabilizer;
    BarStabilizer m_leftStabilizer, m_rightStabilizer;
    UINT m_reservedWidth;
    UINT m_reservedHeight;

    DXGI_COLOR_SPACE_TYPE m_colorSpace;
    LumaEncoding m_lumaEncoding = LumaEncoding::SDR;
};
