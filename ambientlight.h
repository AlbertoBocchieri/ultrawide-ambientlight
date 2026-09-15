#pragma once

#include "common.h"
#include "capture.h"
#include "dcomp.h"
#include "shaders/copy.h"
#include "shaders/ambient.h"
#include "shaders/blur.h"
#include "shaders/fullscreenquad.h"
#include "shaders/vignette.h"
#include "shaders/detect.h"

class AmbientLight
{
public:
    AmbientLight();
    ~AmbientLight();

    HRESULT Initialize(HWND hwnd);

    void Render();

    LRESULT WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    RECT GetPresentRect();
private:
    struct DesktopFormat
    {
        DXGI_FORMAT format;
        DXGI_COLOR_SPACE_TYPE colorSpace;
        DXGI_FORMAT outputFormat;
        DXGI_COLOR_SPACE_TYPE outputColorSpace;
    };


    AppSettings m_settings;
    HRESULT UpdateSettings();
    HRESULT UpdateGeometry();
    void ValidateSettings();

    DesktopFormat GetDesktopFormat();

    HWND m_hwnd;
    bool m_resetUiPosition;

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_immediate;
    ComPtr<ID3D11DeviceContext> m_deferred;
    ComPtr<IDXGISwapChain1> m_swapchain;
    ComPtr<IDCompositionDevice> m_dcompDevice;
    ComPtr<IDCompositionTarget> m_dcompTarget;
    ComPtr<IDCompositionVisual> m_dcompVisual;

    DesktopCapture m_capture;
    Blur m_blurDownscale;
    Copy m_copy;
    AmbientBackground m_ambient;
    Vignette m_vignette;
    Detection m_detection;
    ElapsedTimer m_detectionTimer;

    bool m_ready;
    bool m_effectRendered;
    bool m_presented;
    bool m_temporalReady = false;
    INT64 m_lastTemporalTime = 0;
    UINT m_temporalIndex = 0;

    UINT m_gameWidth;
    UINT m_gameHeight;
    
    UINT m_windowWidth;
    UINT m_windowHeight;
    
    UINT m_effectZoom;

    std::vector<BlackBar> m_blackBars;


    UINT m_frameRate;
    INT64 m_lastPresentTime;
    INT64 m_perfFreq;

    PerfTimer m_framePerfTimer = { "frame" };
    PerfTimer m_renderPerfTimer = { "render" };
    PerfTimer m_detectPerfTimer = { "detect" };
    PerfTimer m_sleepPerfTimer = { "sleep" };
    PerfTimer m_capturePerfTimer = { "capture" };

    TextureView m_gameTexture;
    TextureView m_downsampledTexture;
    TextureView m_temporalTextures[2];
    TextureView m_processedBlurTexture;
    TextureView m_effectCanvasTexture;

    HRESULT CreateOffscreen(DXGI_FORMAT captureFormat, DXGI_FORMAT outputFormat);

    bool ShouldRenderEffect();
    bool RenderEffects();
    void RenderConfig();
    void RenderBackBuffer();
    void ClearEffects();

    void Present();
    void HandleRuntimeError(HRESULT hr);
    void Detect();
    void Wait();

    void ShowConfigWindow(bool show);
    bool m_showConfigWindow;

    std::string GetDebugString();
};
