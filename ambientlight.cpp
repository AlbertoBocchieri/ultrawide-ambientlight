// ambientlight.cpp : Defines the entry point for the application.
//

#include "ambientlight.h"
#include "ui.h"

#include <algorithm>
#include <bit>
#include <cmath>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "Winmm.lib")

#define IS_BOX_EMPTY(box) ((box).left >= (box).right || (box).top >= (box).bottom)

D3D11_BOX GetMirroredBox(D3D11_BOX box, UINT width, UINT height)
{
    D3D11_BOX mirrored = box;
    if (RECT_HEIGHT(box) == height)
    {
        // left/right
        mirrored.left = width - box.right;
        mirrored.right = width - box.left;
    }
    else if (RECT_WIDTH(box) == width)
    {
        // top/bottom
        mirrored.top = height - box.bottom;
        mirrored.bottom = height - box.top;
    }
    return mirrored;
}

static HRESULT FindAdapterForMonitor(IDXGIFactory1* factory, HMONITOR monitor, ComPtr<IDXGIAdapter1>& result)
{
    if (!factory || !monitor)
        return E_INVALIDARG;

    for (UINT adapterIndex = 0; ; ++adapterIndex)
    {
        ComPtr<IDXGIAdapter1> adapter;
        HRESULT hr = factory->EnumAdapters1(adapterIndex, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND)
            return hr;
        RETURN_IF_FAILED(hr);

        for (UINT outputIndex = 0; ; ++outputIndex)
        {
            ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(outputIndex, &output);
            if (hr == DXGI_ERROR_NOT_FOUND)
                break;
            RETURN_IF_FAILED(hr);

            DXGI_OUTPUT_DESC desc = {};
            hr = output->GetDesc(&desc);
            RETURN_IF_FAILED(hr);
            if (desc.Monitor == monitor)
            {
                result = adapter;
                return S_OK;
            }
        }
    }
}

AmbientLight::AmbientLight()
    : m_hwnd(nullptr),
    m_resetUiPosition(false),
    m_ready(false),
    m_effectRendered(false),
    m_presented(false),
    m_zoomRendered(false),
    m_gameWidth(0),
    m_gameHeight(0),
    m_windowWidth(0),
    m_windowHeight(0),
    m_effectZoom(0),
    m_frameRate(60),
    m_lastPresentTime(0),
    m_perfFreq(0),
    m_showConfigWindow(false),
    m_clearConfigWindow(false)
{
    m_dirtyRects[0] = { 0, 0, 0, 0 };
    m_dirtyRects[1] = { 0, 0, 0, 0 };
    timeBeginPeriod(1);
}

AmbientLight::~AmbientLight()
{
    timeEndPeriod(1);
}

LRESULT AmbientLight::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_TOGGLE_CONFIG_WINDOW:
    {
        bool toggle = (lParam != 0);
        bool show = toggle ? !m_showConfigWindow : (wParam != 0);
        ShowConfigWindow(show);
        if (show)
            SetForegroundWindow(hwnd);
        return 0;
    }
    case WM_WINDOW_ACTIVATED:
    {
        if (m_settings.popupConfigOnFocus && !m_showConfigWindow)
        {
            ShowConfigWindow(true);
            SetForegroundWindow(hwnd);
        }
        return 0;
    }
    }

    return UiWndProc(hwnd, message, wParam, lParam);
}

RECT AmbientLight::GetPresentRect()
{
    RefreshDisplays();
    ReadSettings(m_settings);
    return GetDisplayRect(m_settings.display);
}

HRESULT AmbientLight::UpdateSettings()
{
    ValidateSettings();

    if (m_hwnd)
    {
        HRESULT hr = S_OK;
        //m_blurPre.Initialize(m_device,
        //    m_deferred,
        //    m_gameWidth,
        //    m_gameHeight,
        //    m_settings.blurSamples);

        UINT mipWidth = std::max(1u, m_gameWidth >> m_settings.mipmapLevels);
        UINT mipHeight = std::max(1u, m_gameHeight >> m_settings.mipmapLevels);
        hr = m_blurDownscale.Initialize(m_device,
            m_deferred,
            mipWidth,
            mipHeight,
            m_settings.blurSamples);
        RETURN_IF_FAILED(hr);

        float windowAspect = (float)m_windowWidth / (float)m_windowHeight;
        hr = m_vignette.Initialize(m_device,
            m_deferred,
            m_settings.vignetteIntensity,
            m_settings.vignetteRadius,
            m_settings.vignetteSmoothness,
            windowAspect);
        RETURN_IF_FAILED(hr);

        auto df = GetDesktopFormat();
        hr = CreateOffscreen(df.format, df.outputFormat);
        RETURN_IF_FAILED(hr);

        DXGI_COLOR_SPACE_TYPE colorSpace = df.colorSpace;
        hr = m_detection.Initialize(m_device,
            m_immediate,
            m_windowWidth,
            m_windowHeight,
            m_settings.autoDetectionBrightnessThreshold,
            m_settings.autoDetectionBlackRatio,
            m_settings.autoDetectionSymmetricBars,
            m_settings.autoDetectionReservedArea ? m_settings.autoDetectionReservedWidth : 0,
            m_settings.autoDetectionReservedArea ? m_settings.autoDetectionReservedHeight : 0,
            df.format,
            colorSpace);
        RETURN_IF_FAILED(hr);

        InitUI(m_hwnd, m_device.Get(), m_deferred.Get(), m_settings);
    }

    return S_OK;
}

void AmbientLight::ValidateSettings()
{
    if (m_settings.loaded && m_settings.useAutoDetection)
    {
        m_blackBars = m_detection.GetDetectedBars();
    }
    else
    {
        // Validate game width and height
        UINT width = m_settings.gameWidth;
        UINT height = m_settings.gameHeight;

        if (!m_settings.loaded)
        {
            // initial value
            width = m_windowWidth;
            height = m_windowHeight;
        }
        else if (width == 0 || height == 0)
        {
            // if missing config, setting default game size to 16:9
            width = 16;
            height = 9;
        }

        m_blackBars = m_detection.GetFixedBars(m_windowWidth, m_windowHeight, width, height);
    }


    // use the width/height as aspect ratio, and calculate the game size base on the desktop size
    m_gameWidth = m_windowWidth;
    m_gameHeight = m_windowHeight;
    if (m_blackBars.size() >= 2)
    {
        if (m_windowWidth > m_blackBars[0].width)
            m_gameWidth = m_windowWidth - m_blackBars[0].width - m_blackBars[1].width;

        if (m_windowHeight > m_blackBars[0].height)
            m_gameHeight = m_windowHeight - m_blackBars[0].height - m_blackBars[1].height;
    }

    // Validate blur settings
    m_settings.blurPasses = std::clamp(m_settings.blurPasses, 0u, 128u);
    UINT maxMipLevel = m_gameWidth || m_gameHeight
        ? static_cast<UINT>(std::bit_width(std::max(m_gameWidth, m_gameHeight)) - 1)
        : 0;
    m_settings.mipmapLevels = std::min(m_settings.mipmapLevels, std::min(12u, maxMipLevel));
    m_settings.blurSamples = std::clamp(m_settings.blurSamples / 2 * 2 + 1, 1u, 63u);

    // Validate vignette settings
    m_settings.vignetteIntensity = std::clamp(m_settings.vignetteIntensity, 0.0f, 1.0f);
    m_settings.vignetteRadius = std::clamp(m_settings.vignetteRadius, 0.0f, 1.0f);
    m_settings.vignetteSmoothness = std::clamp(m_settings.vignetteSmoothness, 0.0f, 1.0f);

    if (!std::isfinite(m_settings.stretchFactor))
        m_settings.stretchFactor = DEFAULT_STRETCH_FACTOR;
    m_settings.stretchFactor = std::clamp(m_settings.stretchFactor, 0.1f, 5.0f);

    if (!std::isfinite(m_settings.autoDetectionBrightnessThreshold))
        m_settings.autoDetectionBrightnessThreshold = DEFAULT_AUTO_DETECTION_BRIGHTNESS_THRESHOLD;
    m_settings.autoDetectionBrightnessThreshold = std::clamp(m_settings.autoDetectionBrightnessThreshold, 0.01f, 1.0f);

    if (!std::isfinite(m_settings.autoDetectionBlackRatio))
        m_settings.autoDetectionBlackRatio = DEFAULT_AUTO_DETECTION_BLACK_RATIO;
    m_settings.autoDetectionBlackRatio = std::clamp(m_settings.autoDetectionBlackRatio, 0.01f, 1.0f);
    m_settings.autoDetectionTime = std::clamp(m_settings.autoDetectionTime, 1, 3000);

    if (!std::isfinite(m_settings.uiScale))
        m_settings.uiScale = DEFAULT_UI_SCALE;
    m_settings.uiScale = std::clamp(m_settings.uiScale, 0.5f, 3.0f);

    // Validate frame rate
    m_settings.frameRate = std::clamp(m_settings.frameRate, 10u, 1000u);
    m_frameRate = m_settings.frameRate;

    m_settings.transitionTimeMs = std::clamp(m_settings.transitionTimeMs, 0, 5000);

    // Validate zoom
    m_settings.zoom = std::clamp(m_settings.zoom, 0u, 16u);
    m_effectZoom = m_settings.zoom * 4;
}

AmbientLight::DesktopFormat AmbientLight::GetDesktopFormat()
{
    AmbientLight::DesktopFormat f = {
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
    };
    if (m_settings.hdrSupport)
    {
        f.format = m_capture.GetDesktopDesc().ModeDesc.Format;
        f.colorSpace = m_capture.GetOutputDesc1().ColorSpace;
        // DirectComposition needs alpha blending. FP16 scRGB is the universal
        // Advanced Color path for that scenario and avoids ambiguous RGB10 PQ.
        f.outputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        f.outputColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    }

    return f;
}

HRESULT AmbientLight::Initialize(HWND hwnd)
{
    m_ready = false;
    m_capture.ReleaseFrame();

    m_hwnd = hwnd;

    if (!m_settings.loaded)
    {
        ReadSettings(m_settings);
    }

    m_resetUiPosition = true;

    QueryPerformanceFrequency((LARGE_INTEGER*)&m_perfFreq);

    HRESULT hr = S_OK;
    ComPtr<IDXGIFactory2> dxgiFactory2;
    hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), &dxgiFactory2);
    RETURN_IF_FAILED(hr);

    RECT windowRect = { 0 };
    GetWindowRect(hwnd, &windowRect);
    m_windowWidth = RECT_WIDTH(windowRect);
    m_windowHeight = RECT_HEIGHT(windowRect);

    HMONITOR monitor = GetDisplayMonitor(m_settings.display);
    ComPtr<IDXGIAdapter1> targetAdapter;
    hr = FindAdapterForMonitor(dxgiFactory2.Get(), monitor, targetAdapter);
    RETURN_IF_FAILED(hr);

    bool deviceChanged = !m_device || FAILED(m_device->GetDeviceRemovedReason());
    if (!deviceChanged)
    {
        ComPtr<IDXGIDevice> currentDxgiDevice;
        ComPtr<IDXGIAdapter> currentAdapter;
        DXGI_ADAPTER_DESC currentDesc = {};
        DXGI_ADAPTER_DESC1 targetDesc = {};
        hr = m_device.As(&currentDxgiDevice);
        RETURN_IF_FAILED(hr);
        hr = currentDxgiDevice->GetAdapter(&currentAdapter);
        RETURN_IF_FAILED(hr);
        hr = currentAdapter->GetDesc(&currentDesc);
        RETURN_IF_FAILED(hr);
        hr = targetAdapter->GetDesc1(&targetDesc);
        RETURN_IF_FAILED(hr);
        deviceChanged = currentDesc.AdapterLuid.HighPart != targetDesc.AdapterLuid.HighPart ||
            currentDesc.AdapterLuid.LowPart != targetDesc.AdapterLuid.LowPart;
    }

    if (deviceChanged)
    {
        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL featureLevel;
        UINT creationFlags = 0;
#if defined(_DEBUG)
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        hr = D3D11CreateDevice(targetAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, creationFlags,
            featureLevels, 1, D3D11_SDK_VERSION, &m_device, &featureLevel, &m_immediate);
        RETURN_IF_FAILED(hr);

        hr = m_device->CreateDeferredContext(0, &m_deferred);
        RETURN_IF_FAILED(hr);
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_device.As(&dxgiDevice);
    RETURN_IF_FAILED(hr);

    hr = m_capture.Initialize(m_device, monitor, m_settings.hdrSupport);
    RETURN_IF_FAILED(hr);

    // create swap chain
    auto df = GetDesktopFormat();

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = m_windowWidth;
    scd.Height = m_windowHeight;
    scd.Format = df.outputFormat;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    scd.Scaling = DXGI_SCALING_STRETCH;

    DXGI_SWAP_CHAIN_DESC1 currentScd = {};
    bool recreateSwapChain = deviceChanged || !m_swapchain ||
        FAILED(m_swapchain->GetDesc1(&currentScd)) ||
        currentScd.Width != scd.Width || currentScd.Height != scd.Height || currentScd.Format != scd.Format;

    if (recreateSwapChain)
    {
        ComPtr<IDXGISwapChain1> newSwapchain;
        hr = dxgiFactory2->CreateSwapChainForComposition(m_device.Get(), &scd, nullptr, &newSwapchain);
        RETURN_IF_FAILED(hr);

        if (deviceChanged || !m_dcompDevice)
        {
            hr = DCompositionCreateDevice(dxgiDevice.Get(), __uuidof(IDCompositionDevice), &m_dcompDevice);
            RETURN_IF_FAILED(hr);
            hr = m_dcompDevice->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcompTarget);
            RETURN_IF_FAILED(hr);
            hr = m_dcompDevice->CreateVisual(&m_dcompVisual);
            RETURN_IF_FAILED(hr);
            hr = m_dcompTarget->SetRoot(m_dcompVisual.Get());
            RETURN_IF_FAILED(hr);
        }

        hr = m_dcompVisual->SetContent(newSwapchain.Get());
        RETURN_IF_FAILED(hr);
        hr = m_dcompDevice->Commit();
        RETURN_IF_FAILED(hr);
        m_swapchain = newSwapchain;
    }

    ComPtr<IDXGISwapChain3> swapchain3;
    hr = m_swapchain.As(&swapchain3);
    RETURN_IF_FAILED(hr);
    UINT colorSpaceSupport = 0;
    hr = swapchain3->CheckColorSpaceSupport(df.outputColorSpace, &colorSpaceSupport);
    RETURN_IF_FAILED(hr);
    if ((colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0)
        return DXGI_ERROR_UNSUPPORTED;
    hr = swapchain3->SetColorSpace1(df.outputColorSpace);
    RETURN_IF_FAILED(hr);

    hr = m_copy.Initialize(m_device, m_deferred.Get());
    RETURN_IF_FAILED(hr);

    if (deviceChanged)
    {
        m_gameTexture.Clear();
        m_downsampledTexture.Clear();
        m_temporalTextures[0].Clear();
        m_temporalTextures[1].Clear();
        m_processedBlurTexture.Clear();
        m_effectCanvasTexture.Clear();
    }

    hr = UpdateSettings();
    RETURN_IF_FAILED(hr);

    m_ready = true;

    return S_OK;
}

HRESULT AmbientLight::CreateOffscreen(DXGI_FORMAT captureFormat, DXGI_FORMAT outputFormat)
{
    HRESULT hr = S_OK;
    // Create with full mip chain (0) and enable mip generation support
    hr = m_gameTexture.RecreateTexture(m_device.Get(), captureFormat, m_gameWidth, m_gameHeight, 0, true);
    RETURN_IF_FAILED(hr);

    // m_downsampledTexture now matches the selected mip level size
    UINT mipWidth = std::max(1u, m_gameWidth >> m_settings.mipmapLevels);
    UINT mipHeight = std::max(1u, m_gameHeight >> m_settings.mipmapLevels);
    hr = m_downsampledTexture.RecreateTexture(m_device.Get(), captureFormat,
        mipWidth,
        mipHeight);
    RETURN_IF_FAILED(hr);

    for (auto& texture : m_temporalTextures)
    {
        hr = texture.RecreateTexture(m_device.Get(), captureFormat, mipWidth, mipHeight);
        RETURN_IF_FAILED(hr);
    }

    hr = m_processedBlurTexture.RecreateTexture(m_device.Get(), captureFormat,
        m_gameWidth,
        m_gameHeight);
    RETURN_IF_FAILED(hr);

    hr = m_effectCanvasTexture.RecreateTexture(m_device.Get(), outputFormat,
        m_windowWidth,
        m_windowHeight);
    RETURN_IF_FAILED(hr);

    m_temporalReady = false;
    m_lastTemporalTime = 0;
    m_temporalIndex = 0;

    return hr;
}

void AmbientLight::Render()
{
    if (nullptr == m_device || !m_ready)
        return;

    ScopedPerfTimer frameTimer(m_framePerfTimer);

    {
        HRESULT hr = m_capture.ReleaseFrame();
        HandleRuntimeError(hr);
        if (ShouldRenderEffect())
        {
            ScopedPerfTimer captureTimer(m_capturePerfTimer);
            hr = m_capture.Capture();
            if (FAILED(hr) && hr != DXGI_ERROR_WAIT_TIMEOUT)
                HandleRuntimeError(hr);
        }
    }
    if (!m_ready)
        return;

    {
        ScopedPerfTimer renderTimer(m_renderPerfTimer);
        if (ShouldRenderEffect())
        {
            RenderEffects();
            if (!m_ready)
                return;
        }
        else
        {
            ClearEffects();
        }

        RenderConfig();
        RenderBackBuffer();
    }
    if (!m_ready)
        return;

    Present();
    if (!m_ready)
        return;

    {
        ScopedPerfTimer detectTimer(m_detectPerfTimer);
        Detect();
    }

    Wait();

    bool changed = ReadSettings(m_settings);
    if (changed)
    {
        HRESULT hr = UpdateSettings();
        if (FAILED(hr))
            m_ready = false;
    }
}

bool AmbientLight::ShouldRenderEffect()
{
    return m_gameWidth < m_windowWidth || m_gameHeight < m_windowHeight;
}

bool AmbientLight::RenderEffects()
{
    ComPtr<ID3D11Texture2D> desktopTexture = m_capture.GetDesktopTexture();
    if (!desktopTexture)
        return false;

    ComPtr<IDXGISurface> surface;
    HRESULT hr = desktopTexture.As(&surface);
    if (FAILED(hr) || !surface)
        return false;
    DXGI_SURFACE_DESC desc = {};
    hr = surface->GetDesc(&desc);
    if (FAILED(hr))
        return false;

    if (m_blackBars.size() < 2)
        return false;

    D3D11_BOX game_box = {};
    game_box.front = 0;
    game_box.back = 1;
    if (m_gameHeight == m_windowHeight)
    {
        // black bars on left/right
        game_box.left = m_blackBars[0].width;
        game_box.right = m_windowWidth - m_blackBars[1].width;
        game_box.top = 0;
        game_box.bottom = m_windowHeight;
    }
    else if (m_gameWidth == m_windowWidth)
    {
        // black bars on top/bottom
        game_box.left = 0;
        game_box.right = m_windowWidth;
        game_box.top = m_blackBars[0].height;
        game_box.bottom = m_windowHeight - m_blackBars[1].height;
    }
    else
    {
        return false;
    }

    if (IS_BOX_EMPTY(game_box))
        return false;

    D3D11_TEXTURE2D_DESC gameDesc = {};
    m_gameTexture.GetTexture()->GetDesc(&gameDesc);
    //assert(gameDesc.Format == desc.Format);

    m_deferred->CopySubresourceRegion(m_gameTexture.GetTexture(), 0, 0, 0, 0, desktopTexture.Get(), 0, &game_box);

    // m_blurPre.Render(m_deferred.Get(), m_gameTexture, m_settings.blurPasses);

    // Generate mipmaps for the captured game area
    m_deferred->GenerateMips(m_gameTexture.GetSRV());

    // Extract the specific mip level to the secondary buffer for the final blur/stretch
    m_deferred->CopySubresourceRegion(m_downsampledTexture.GetTexture(), 0, 0, 0, 0, m_gameTexture.GetTexture(), m_settings.mipmapLevels, NULL);

    hr = m_blurDownscale.Render(m_deferred.Get(), m_downsampledTexture, m_settings.blurPasses);
    if (FAILED(hr))
    {
        HandleRuntimeError(hr);
        return false;
    }

    TextureView* effectSource = &m_downsampledTexture;
    if (m_settings.transitionTimeMs > 0)
    {
        INT64 now = 0;
        QueryPerformanceCounter((LARGE_INTEGER*)&now);

        float blend = 1.0f;
        if (m_temporalReady && m_lastTemporalTime != 0)
        {
            const double elapsedMs = (double)(now - m_lastTemporalTime) * 1000.0 / m_perfFreq;
            blend = (float)(1.0 - std::exp(-elapsedMs / m_settings.transitionTimeMs));
        }

        const UINT nextIndex = m_temporalReady ? 1 - m_temporalIndex : 0;
        TextureView previous = m_temporalReady ? m_temporalTextures[m_temporalIndex] : TextureView();
        hr = m_copy.Render(m_deferred.Get(), m_temporalTextures[nextIndex], m_downsampledTexture,
            Copy::FlipNone, blend, previous);
        if (FAILED(hr))
        {
            HandleRuntimeError(hr);
            return false;
        }
        m_temporalIndex = nextIndex;
        effectSource = &m_temporalTextures[m_temporalIndex];
        m_temporalReady = true;
        m_lastTemporalTime = now;
    }
    else
    {
        m_temporalReady = false;
        m_lastTemporalTime = 0;
        m_temporalIndex = 0;
    }

    UINT mipWidth = std::max(1u, m_gameWidth >> m_settings.mipmapLevels);
    UINT mipHeight = std::max(1u, m_gameHeight >> m_settings.mipmapLevels);
    if (mipWidth > m_effectZoom * 2 && mipHeight > m_effectZoom * 2)
    {
        hr = m_copy.Render(m_deferred.Get(), m_processedBlurTexture, 0, 0, m_gameWidth, m_gameHeight,
            *effectSource, m_effectZoom, m_effectZoom, mipWidth - m_effectZoom * 2, mipHeight - m_effectZoom * 2);
    }
    else
    {
        hr = m_copy.Render(m_deferred.Get(), m_processedBlurTexture, *effectSource);
    }
    if (FAILED(hr))
    {
        HandleRuntimeError(hr);
        return false;
    }

    ID3D11RenderTargetView* rtv = m_effectCanvasTexture.GetRTV();
    float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_deferred->ClearRenderTargetView(rtv, color);

    D3D11_TEXTURE2D_DESC desc2 = {};
    m_processedBlurTexture.GetTexture()->GetDesc(&desc2);


    for (int i = 0; i < 2; i++)
    {
        BlackBar srcBar = m_blackBars[i];
        srcBar.parentWidth = m_gameWidth;
        srcBar.parentHeight = m_gameHeight;


        switch (srcBar.position)
        {
        case BlackBarPosition::Top:
        case BlackBarPosition::Bottom:
            srcBar.height = (UINT)((float)srcBar.height / m_settings.stretchFactor);
            break;
        case BlackBarPosition::Left:
        case BlackBarPosition::Right:
            srcBar.width = (UINT)((float)srcBar.width / m_settings.stretchFactor);
            break;
        }

        D3D11_BOX src = srcBar.toBox();
        D3D11_BOX dst = m_blackBars[i].toBox();

        Copy::Flip flip = Copy::FlipNone;
        if (m_settings.mirrored)
        {
            flip = (m_gameWidth == m_windowWidth) ? Copy::FlipVertical : Copy::FlipHorizontal;
        }

        hr = m_copy.Render(m_deferred.Get(), m_effectCanvasTexture, dst.left, dst.top, RECT_WIDTH(dst), RECT_HEIGHT(dst),
            m_processedBlurTexture, src.left, src.top, RECT_WIDTH(src), RECT_HEIGHT(src), flip);
        if (FAILED(hr))
        {
            HandleRuntimeError(hr);
            return false;
        }
    }

    m_effectRendered = true;

    return true;
}

void AmbientLight::ClearEffects()
{
    if (m_effectRendered)
    {
        ID3D11RenderTargetView* rtv = m_effectCanvasTexture.GetRTV();
        float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_deferred->ClearRenderTargetView(rtv, color);

        // force next present
        m_presented = false;
    }
    m_effectRendered = false;
    m_zoomRendered = false;
    m_temporalReady = false;
    m_lastTemporalTime = 0;
    m_temporalIndex = 0;
}

void AmbientLight::RenderConfig()
{
    if (!m_showConfigWindow)
        return;

    bool open = RenderUI(m_hwnd, m_settings, m_gameWidth, m_gameHeight, m_resetUiPosition, GetDebugString());
    ShowConfigWindow(open);

    m_resetUiPosition = false;
}

void AmbientLight::RenderBackBuffer()
{
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);
    if (FAILED(hr))
    {
        HandleRuntimeError(hr);
        return;
    }

    TextureView backview;
    if (FAILED(backview.CreateViews(m_device.Get(), backBuffer.Get(), true, false, false)))
        return;

    ID3D11RenderTargetView* rtv_back = backview.GetRTV();
    float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_deferred->ClearRenderTargetView(rtv_back, color);

    if (m_effectRendered)
    {
        if (m_settings.vignetteEnabled)
        {
            hr = m_vignette.Render(m_deferred.Get(), m_effectCanvasTexture);
            if (FAILED(hr))
            {
                HandleRuntimeError(hr);
                return;
            }
        }

        if (m_settings.useAutoDetection && m_settings.autoDetectionLightMask)
        {
            hr = m_detection.RenderLumaMask(m_deferred.Get(), m_effectCanvasTexture);
            if (FAILED(hr))
            {
                HandleRuntimeError(hr);
                return;
            }
        }

        if (m_settings.autoDetectionInner)
        {
            if (m_blackBars.size() == 4)
            {
                if (m_settings.autoDetectionZoom)
                {
                    RECT innerRect = { 0 };
                    // find the inner box
                    if (m_blackBars[0].position == BlackBarPosition::Left)
                    {
                        innerRect.left = 0;
                        innerRect.right = m_gameWidth;
                        innerRect.top = m_blackBars[2].height;
                        innerRect.bottom = m_gameHeight - m_blackBars[3].height;
                    }
                    else
                    {
                        innerRect.left = m_blackBars[2].width;
                        innerRect.right = m_gameWidth - m_blackBars[3].width;
                        innerRect.top = 0;
                        innerRect.bottom = m_gameHeight;
                    }

                    if (RECT_WIDTH(innerRect) >= LONG(m_windowWidth / 2) && RECT_HEIGHT(innerRect) > LONG(m_windowHeight / 2))
                    {
                        // zoom rect
                        float innerAspect = (float)RECT_WIDTH(innerRect) / (float)RECT_HEIGHT(innerRect);

                        UINT zoomHeight = m_windowHeight;
                        UINT zoomWidth = (UINT)(zoomHeight * innerAspect);

                        if (zoomWidth > m_windowWidth)
                        {
                            zoomWidth = m_windowWidth;
                            zoomHeight = (UINT)(zoomWidth / innerAspect);
                        }

                        RECT zoomedRect = { 
                            LONG(m_windowWidth - zoomWidth) / 2,
                            LONG(m_windowHeight - zoomHeight) / 2,
                            LONG(m_windowWidth - zoomWidth) / 2 + (LONG)zoomWidth,
                            LONG(m_windowHeight - zoomHeight) / 2 + (LONG)zoomHeight
                        };

                        // now copy the zoomed inner box to the effect texture
                        hr = m_copy.Render(m_deferred.Get(), m_effectCanvasTexture, zoomedRect.left, zoomedRect.top, RECT_WIDTH(zoomedRect), RECT_HEIGHT(zoomedRect),
                            m_gameTexture, innerRect.left, innerRect.top, RECT_WIDTH(innerRect), RECT_HEIGHT(innerRect));
                        if (FAILED(hr))
                        {
                            HandleRuntimeError(hr);
                            return;
                        }

                        m_zoomRendered = true;
                    }
                }
                else
                {
                    // clear the inner box in the effect texture
                    ComPtr<ID3D11DeviceContext1> deferred1 = nullptr;
                    m_deferred.As(&deferred1);
                    if (deferred1)
                    {
                        float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                        D3D11_RECT rects[2] = { m_blackBars[2].toRect(), m_blackBars[3].toRect() };
                        deferred1->ClearView(m_effectCanvasTexture.GetRTV(), color, &rects[0], 2);
                    }
                }
            }
        }
    }

    m_deferred->CopyResource(backview.GetTexture(), m_effectCanvasTexture.GetTexture());

    m_deferred->OMSetRenderTargets(1, &rtv_back, nullptr);
    if (m_showConfigWindow)
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    ComPtr<ID3D11CommandList> cmdlist = nullptr;
    hr = m_deferred->FinishCommandList(FALSE, &cmdlist);
    if (FAILED(hr))
    {
        HandleRuntimeError(hr);
        return;
    }

    if (cmdlist)
    {
        m_immediate->ExecuteCommandList(cmdlist.Get(), TRUE);
    }
}

void AmbientLight::Present()
{
    HRESULT hr = S_OK;
    if (m_showConfigWindow || m_clearConfigWindow)
    {
        m_clearConfigWindow = false;
        hr = m_swapchain->Present(1, 0);
        m_presented = true;
    }
    else if (m_zoomRendered)
    {
        // if zoom is enabled with inner bars, always present the whole backbuffer to avoid artifacts on the zoomed inner box
        hr = m_swapchain->Present(1, 0);
        m_presented = true;
    }
    else
    {
        if (!m_presented)
        {
            memset(m_dirtyRects, 0, sizeof(m_dirtyRects));
            int numRects = 0;
            for (auto& bar : m_blackBars)
            {
                D3D11_BOX box = bar.toBox();
                if (IS_BOX_EMPTY(box))
                    continue;
                m_dirtyRects[numRects].left = box.left;
                m_dirtyRects[numRects].top = box.top;
                m_dirtyRects[numRects].right = box.right;
                m_dirtyRects[numRects].bottom = box.bottom;
                numRects++;
                if (numRects == 2)
                    break;
            }

            DXGI_PRESENT_PARAMETERS param = {};
            param.DirtyRectsCount = numRects;
            param.pDirtyRects = m_dirtyRects;
            param.pScrollOffset = nullptr;
            param.pScrollRect = nullptr;

            hr = m_swapchain->Present1(1, 0, &param);
            if (FAILED(hr))
            {
                // in case of error, try normal present
                hr = m_swapchain->Present(1, 0);
            }
            m_presented = true;
        }
        else
        {
            m_presented = false;
        }
    }

    HandleRuntimeError(hr);
}

void AmbientLight::HandleRuntimeError(HRESULT hr)
{
    if (FAILED(hr))
    {
        m_ready = false;
        PostMessage(m_hwnd, WM_DISPLAYCHANGE, 0, 0);
    }
}

void AmbientLight::Detect()
{
    if (m_settings.useAutoDetection)
    {
        if (m_detectionTimer.HasElapsed(m_settings.autoDetectionTime))
        {
            HRESULT hr = m_capture.Capture();
            if (FAILED(hr))
            {
                if (hr != DXGI_ERROR_WAIT_TIMEOUT)
                    HandleRuntimeError(hr);
                return;
            }
            ComPtr<ID3D11Texture2D> desktopTexture = m_capture.GetDesktopTexture();
            if (!desktopTexture)
                return;

            TextureView desktopTextureView;
            if (FAILED(desktopTextureView.CreateViews(m_device.Get(), desktopTexture.Get(), false, true, false)))
                return;
            if (FAILED(m_detection.Detect(m_immediate.Get(), desktopTextureView)))
                return;

            std::vector<BlackBar> detected = m_detection.GetDetectedBars();

            bool updateSettings = false;
            if (detected != m_blackBars)
            {
                updateSettings = true;
            }

            if (updateSettings)
            {
                hr = UpdateSettings();
                if (FAILED(hr))
                    m_ready = false;
            }
        }
    }
}

void AmbientLight::Wait()
{
    // if we presented a frame, use frame rate setting
    if (m_presented || m_effectRendered)
    {
        INT64 now = 0;
        double elapsedMs = 0.0;
        double frameTime = 1000.0 / m_frameRate;

        QueryPerformanceCounter((LARGE_INTEGER*)&now);
        if (m_lastPresentTime != 0)
        {
            ScopedPerfTimer sleepTimer(m_sleepPerfTimer);
            elapsedMs = (double)((now - m_lastPresentTime) * 1000) / m_perfFreq;

            while (elapsedMs < frameTime)
            {
                double remaining = frameTime - elapsedMs;
                if (remaining > 1.5)
                {
                    Sleep(1);
                }
                else
                {
                    Sleep(0);
                }

                QueryPerformanceCounter((LARGE_INTEGER*)&now);
                elapsedMs = (double)((now - m_lastPresentTime) * 1000) / m_perfFreq;
            }
        }

        QueryPerformanceCounter((LARGE_INTEGER*)&now);
        m_lastPresentTime = now;
    }
    else if (m_settings.useAutoDetection)
    {
        // if we didn't render, we can sleep up to 1 second until next detection
		ULONGLONG timeSinceLastDetect = m_detectionTimer.Elapsed();
		if (timeSinceLastDetect < (ULONGLONG)m_settings.autoDetectionTime)
		{
			ULONGLONG remaining = (ULONGLONG)m_settings.autoDetectionTime - timeSinceLastDetect;
			Sleep(std::min<DWORD>(1000, (DWORD)remaining));
            //char logBuffer[128] = { 0 };
            //sprintf_s(logBuffer, "Sleeping for %llu ms until next detection...\n", remaining);
            //OutputDebugStringA(logBuffer);
		}
    }
}

void AmbientLight::ShowConfigWindow(bool show)
{
    if (show != m_showConfigWindow)
    {
        m_clearConfigWindow = true;
        m_showConfigWindow = show;
        DWORD dwExStyle = GetWindowLong(m_hwnd, GWL_EXSTYLE);

        dwExStyle = show ? dwExStyle & ~WS_EX_TRANSPARENT : dwExStyle | WS_EX_TRANSPARENT;
        SetWindowLong(m_hwnd, GWL_EXSTYLE, dwExStyle);
    }
}

std::string AmbientLight::GetDebugString()
{
    static std::string debugStr;
    static ULONGLONG lastLogTime = 0;
    ULONGLONG currentTime = GetTickCount64();
    if (1000 < (currentTime - lastLogTime))
    {
        lastLogTime = currentTime;

        UINT ref = m_device->AddRef();
        ref = m_device->Release();

        auto format = GetDesktopFormat();
        std::string formatStr = (format.format == DXGI_FORMAT_B8G8R8A8_UNORM) ? "BGRA8" :
            (format.format == DXGI_FORMAT_R10G10B10A2_UNORM) ? "RGBA10" :
            (format.format == DXGI_FORMAT_R16G16B16A16_FLOAT) ? "RGBAF16" : "Unknown";
        std::string colorSpaceStr = DXGIColorSpaceToString(format.colorSpace);

        char logBuffer[512] = { 0 };
        sprintf_s(logBuffer, 
            "- Format: %s\n"
            "- ColorSpace: %s\n"
            "- Performance (ms):\n"
            "%s\n%s\n%s\n%s\n%s\n"
            "- DX ref %u\n",
            formatStr.c_str(), colorSpaceStr.c_str(),
            m_framePerfTimer.ToString().c_str(),
            m_capturePerfTimer.ToString().c_str(),
            m_renderPerfTimer.ToString().c_str(),
            m_detectPerfTimer.ToString().c_str(),
            m_sleepPerfTimer.ToString().c_str(),
            ref
        );

        debugStr = logBuffer;

#ifdef _DEBUG
        OutputDebugStringA(logBuffer);
#endif
    }
    return debugStr;
}
