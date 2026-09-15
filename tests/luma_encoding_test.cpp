#include "shaders/detect.h"

#include <cassert>

int main()
{
    using Encoding = Detection::LumaEncoding;

    assert(Detection::GetLumaEncoding(
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) == Encoding::SCRGB);
    assert(Detection::GetLumaEncoding(
        DXGI_FORMAT_R10G10B10A2_UNORM,
        DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) == Encoding::HDR10);
    assert(Detection::GetLumaEncoding(
        DXGI_FORMAT_R10G10B10A2_UNORM,
        DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) == Encoding::SDR);
    assert(Detection::GetLumaEncoding(
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) == Encoding::SDR);
}
