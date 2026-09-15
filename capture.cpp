#include "capture.h"

#pragma comment(lib, "dxgi.lib")

DesktopCapture::DesktopCapture()
{
}

DesktopCapture::~DesktopCapture()
{
    ReleaseFrame();
}

HRESULT DesktopCapture::Initialize(ComPtr<ID3D11Device> device, HMONITOR monitor, bool hdr)
{
    if (!device || !monitor)
        return E_INVALIDARG;

    // A frame belongs to the duplication object that acquired it. Release it
    // before replacing that object during an HDR/display mode change.
    ReleaseFrame();
    m_duplication.Reset();
    m_desktopTexture.Reset();
    m_outputDesc1 = {};

    m_device = device;
    m_device->GetImmediateContext(&m_context);

    m_monitor = monitor;
    m_hdr = hdr;

    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_device.As(&dxgiDevice);
    RETURN_IF_FAILED(hr);

    ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetParent(__uuidof(IDXGIAdapter), &dxgiAdapter);
    RETURN_IF_FAILED(hr);

    ComPtr<IDXGIOutput> dxgiOutput;
    UINT outputIndex = 0;
    while (true) {
        hr = dxgiAdapter->EnumOutputs(outputIndex, dxgiOutput.ReleaseAndGetAddressOf());
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        RETURN_IF_FAILED(hr);

        DXGI_OUTPUT_DESC desc;
        if (SUCCEEDED(dxgiOutput->GetDesc(&desc))) {
            if (desc.Monitor == monitor) {
                break;
            }
        }
        outputIndex++;
    }
    hr = dxgiOutput ? S_OK : E_FAIL;
    RETURN_IF_FAILED(hr);

    ComPtr<IDXGIOutput5> dxgiOutput5;
    hr = dxgiOutput.As(&dxgiOutput5);
    RETURN_IF_FAILED(hr);

    std::vector<DXGI_FORMAT> formats = {
        DXGI_FORMAT_B8G8R8A8_UNORM
    };

    if (hdr)
    {
        formats.insert(formats.begin(), {
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_R10G10B10A2_UNORM
            });
    }

    ComPtr<IDXGIOutput6> dxgiOutput6;
    hr = dxgiOutput.As(&dxgiOutput6);

    if (SUCCEEDED(hr))
    {
        DXGI_OUTPUT_DESC1 desc1 = {};
        hr = dxgiOutput6->GetDesc1(&desc1);
        RETURN_IF_FAILED(hr);
        wchar_t buffer[256];
        swprintf_s(buffer, L"DesktopCapture: Output %s, ColorSpace: %d\n", desc1.DeviceName, desc1.ColorSpace);
        OutputDebugStringW(buffer);

        m_outputDesc1 = desc1;
    }

    hr = dxgiOutput5->DuplicateOutput1(m_device.Get(), 0,
        static_cast<UINT>(formats.size()), formats.data(), &m_duplication);
    RETURN_IF_FAILED(hr);

    return hr;
}

HRESULT DesktopCapture::Capture()
{
    if (!m_duplication)
    {
        HRESULT hr = Initialize(m_device, m_monitor, m_hdr);
        RETURN_IF_FAILED(hr);
    }
    if (!m_desktopTexture && m_duplication)
    {
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        ComPtr<IDXGIResource> desktopResource;
        HRESULT hr = m_duplication->AcquireNextFrame(500, &frameInfo, &desktopResource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        {
            return hr;
        }
        if (FAILED(hr))
        {
            m_duplication = nullptr;
            return hr;
        }

        m_frameAcquired = true;
        hr = desktopResource.As(&m_desktopTexture);
        if (FAILED(hr))
        {
            ReleaseFrame();
            return hr;
        }
    }

    return S_OK;
}

HRESULT DesktopCapture::ReleaseFrame()
{
    m_desktopTexture.Reset();
    if (!m_frameAcquired)
        return S_OK;

    m_frameAcquired = false;
    return m_duplication ? m_duplication->ReleaseFrame() : DXGI_ERROR_ACCESS_LOST;
}
