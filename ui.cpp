#include "ui.h"
#include "common.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <algorithm>
#include "windows.h"

const std::wstring IMGUI_FILE_NAME = L"ui.ini";
std::string imguiFilePath = "";

// Helper to display a little (?) mark which shows a tooltip when hovered.
// In your own code you may want to display an actual icon if you are using a merged icon fonts (see docs/FONTS.md)
static void HelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT UiWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui::GetCurrentContext() == nullptr)
        return 0;

    switch (message)
    {
    case WM_KEYDOWN:
    {
        int pressed = (int)wParam;
        if (pressed == VK_ESCAPE)
        {
            PostMessage(hwnd, WM_TOGGLE_CONFIG_WINDOW, 0, 1);
            return 0;
        }
    }
    break;

    case WM_USER_SHELLICON:
    {
        if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP)
        {
            PostMessage(hwnd, WM_TOGGLE_CONFIG_WINDOW, 1, 0);
            return 0;
        }
    }
    break;

    case WM_ACTIVATE:
    {
        // hide config window when lost focus
        if (LOWORD(wParam) == WA_INACTIVE)
        {
            PostMessage(hwnd, WM_TOGGLE_CONFIG_WINDOW, 0, 0);
        }
        else
        {
            PostMessage(hwnd, WM_WINDOW_ACTIVATED, 0, 0);
        }
    }
    break;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    {
        if (!ImGui::GetIO().WantCaptureMouse)
        {
            PostMessage(hwnd, WM_TOGGLE_CONFIG_WINDOW, 0, 0);
        }
    }
    break;

    }

    return ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
}

void InitUI(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* device_context, AppSettings& settings)
{
    IMGUI_CHECKVERSION();
    static bool platformInitialized = false;
    static ID3D11Device* rendererDevice = nullptr;
    static float loadedFontSize = 0.0f;

    if (ImGui::GetCurrentContext() == nullptr)
        ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    if (imguiFilePath.empty())
    {
        imguiFilePath = GetDataFile(IMGUI_FILE_NAME).string();
    }

    io.IniFilename = imguiFilePath.c_str();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // load Segoe UI font
    float fontSize = 20.0f * settings.uiScale;
    bool fontChanged = loadedFontSize != fontSize;
    if (fontChanged)
    {
        io.Fonts->Clear();
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", fontSize);
        io.Fonts->Build();
        loadedFontSize = fontSize;
    }

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // The Win32 backend is window-bound and only needs one initialization.
    if (!platformInitialized)
    {
        ImGui_ImplWin32_Init(hwnd);
        platformInitialized = true;
    }

    // Recreate only the renderer backend when the D3D device changes.
    if (rendererDevice != device)
    {
        if (rendererDevice)
            ImGui_ImplDX11_Shutdown();
        ImGui_ImplDX11_Init(device, device_context);
        rendererDevice = device;
    }
    else if (fontChanged)
    {
        ImGui_ImplDX11_InvalidateDeviceObjects();
    }

    // first initialization
    static bool firstInit = true;
    if (firstInit)
    {
        PostMessage(hwnd, WM_TOGGLE_CONFIG_WINDOW, 1, 0);
    }
    firstInit = false;

    UpdateWindowFlags(hwnd, settings);
}


bool RenderUI(HWND hwnd, AppSettings& settings, UINT gameWidth, UINT gameHeight, bool resetPos, std::string log)
{
    // Start the Dear ImGui frame
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();

    if (resetPos)
    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    }

    bool open = true;
    ImGui::Begin("Ambient Light", &open, 0);

    ImGui::Text("Press Esc or the system tray icon to show or hide the configuration window");

    if (ImGui::BeginTabBar("Configurations"))
    {
        if (ImGui::BeginTabItem("Game/Content Resolution"))
        {
            auto displays = GetAvailableDisplays();

            if (1 < displays.size())
            {
                if (ImGui::BeginCombo("Display", std::to_string(settings.display).c_str(), 0))
                {

                    for (size_t i = 0; i < displays.size(); ++i)
                    {
                        bool selected = (settings.display == static_cast<int>(i));

                        char label[256] = { 0 };
                        sprintf_s(label, "%lld: %d x %d", i, displays[i].width, displays[i].height);
                        if (ImGui::Selectable(label, selected))
                        {
                            settings.display = (int)i;
                            SaveSettings(settings);
                            PostMessage(hwnd, WM_DISPLAYCHANGE, 0, 0);
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            if (ImGui::Checkbox("HDR Support", &settings.hdrSupport))
            {
                SaveSettings(settings);
                PostMessage(hwnd, WM_DISPLAYCHANGE, 0, 0);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Process in HDR format when possible.");
            }

            if (ImGui::CollapsingHeader("Detection", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::Checkbox("Auto Detect", &settings.useAutoDetection))
                    SaveSettings(settings);

                if (settings.useAutoDetection)
                {
                    ImGui::Text("Detected: %d x %d", gameWidth, gameHeight);


                    if (ImGui::Checkbox("Light Peek", &settings.autoDetectionLightMask))
                    {
                        SaveSettings(settings);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        ImGui::SetTooltip(
                            "Preserve bright highlights that appear inside detected black-bar regions.\n"
                            "Enable this when you want small bright UI elements or overlays on letterboxed video\n"
                            "to remain visible instead of being masked out by the black-bar detection.");
                    }

                    int brightnessPercent = (int)(settings.autoDetectionBrightnessThreshold * 100);
                    if (ImGui::DragInt("Detection Brightness", &brightnessPercent, 0.1f, 1, 100, "%d%%"))
                    {
                        settings.autoDetectionBrightnessThreshold = float(brightnessPercent) / 100.0f;
                        SaveSettings(settings);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        ImGui::SetTooltip(
                            "Brightness threshold used to classify pixels as \"black\" for detection.\n"
                            "Pixels darker than this value are considered part of black bars.\n"
                            "Lower values make the detector more permissive (more pixels counted as black).\n"
                            "Higher values make it stricter. Range: 1%% (dark) to 100%% (bright).");
                    }

                    int blackRatioPercent = (int)(settings.autoDetectionBlackRatio * 100);
                    if (ImGui::DragInt("Detection Ratio ", &blackRatioPercent, 0.1f, 1, 100, "%d%%%"))
                    {
                        settings.autoDetectionBlackRatio = float(blackRatioPercent) / 100.0f;
                        SaveSettings(settings);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        ImGui::SetTooltip(
                            "Proportion of an analyzed area that must be below the brightness threshold to\n"
                            "classify that area as a black bar.\n"
                            "Increase this to avoid false positives (requires more of the area to be dark),\n"
                            "or decrease to detect thinner/partial bars.");
                    }
                    if (ImGui::DragInt("Detection Interval", &settings.autoDetectionTime, 0.1f, 1, 3000, "%d ms"))
                    {
                        SaveSettings(settings);
                    }

                    if (ImGui::Checkbox("Symmetric", &settings.autoDetectionSymmetricBars))
                    {
                        SaveSettings(settings);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        ImGui::SetTooltip(
                            "Force detected black bars to be symmetric and align to the smaller side.");
                    }

                    if (ImGui::Checkbox("Reserved Area", &settings.autoDetectionReservedArea))
                    {
                        SaveSettings(settings);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        ImGui::SetTooltip(
                            "Exclude a centered rectangular region from black-bar detection.\n"
                            "You can define it using pixel width/height (1920, 1080) or aspect ratio (e.g. 16, 9)");
                    }
                    if (settings.autoDetectionReservedArea)
                    {
                        int reservedSize[2] = { (int)settings.autoDetectionReservedWidth, (int)settings.autoDetectionReservedHeight };
                        if (ImGui::InputInt2("", reservedSize, ImGuiInputTextFlags_CharsDecimal))
                        {
                            settings.autoDetectionReservedWidth = (UINT)std::max(0, reservedSize[0]);
                            settings.autoDetectionReservedHeight = (UINT)std::max(0, reservedSize[1]);

                            SaveSettings(settings);
                        }
                    }
                    if (ImGui::Checkbox("Inner Detection", &settings.autoDetectionInner))
                    {
                        SaveSettings(settings);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        ImGui::SetTooltip("Perform detection for additional black bars. (experimental)");
                    }
                    if (settings.autoDetectionInner)
                    {
                        if (ImGui::Checkbox("Scale to fit screen", &settings.autoDetectionZoom))
                        {
                            SaveSettings(settings);
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        {
                            ImGui::SetTooltip("Scale the inner area to fit the screen. (experimental)\n"
                                              "Recommended for video only, not for gaming. Mouse input will not be accurate.");
                        }
                    }
                }
                else
                {
                    if (ImGui::BeginCombo("Presets", settings.resolutions.current.c_str(), 0))
                    {
                        for (auto& res : settings.resolutions.available)
                        {
                            bool selected = settings.resolutions.current == res.name;
                            if (ImGui::Selectable(res.name.c_str(), &selected))
                            {
                                settings.resolutions.current = res.name;
                                SaveSettings(settings);
                            }
                        }
                        ImGui::EndCombo();
                    }

                    bool updateResolution = false;
                    int res_input[2] = { (int)settings.gameWidth, (int)settings.gameHeight };
                    if (ImGui::InputInt2("", res_input, ImGuiInputTextFlags_CharsDecimal))
                        updateResolution = true;

                    if (updateResolution)
                    {
                        for (auto& res : settings.resolutions.available)
                        {
                            if (settings.resolutions.current == res.name)
                            {
                                res.width = static_cast<UINT>(std::max(1, res_input[0]));
                                res.height = static_cast<UINT>(std::max(1, res_input[1]));
                                break;
                            }
                        }

                        SaveSettings(settings);
                    }

                    ImGui::Text("Selected: %d x %d", gameWidth, gameHeight);
                }
            }


            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Effects"))
        {
            ImGui::SameLine(); HelpMarker(
                "Click and drag to edit a value.\n"
                "Hold Shift/Alt for faster/slower edits.\n"
                "Double-click or Ctrl+click to enter a value.");

            if (ImGui::Checkbox("VLC ambient background", &settings.vlcAmbient)) SaveSettings(settings);
            if (settings.vlcAmbient) {
                if (ImGui::DragFloat("Background radius", &settings.ambientRadius, 1, 0, 2000, "%.0f px")) SaveSettings(settings);
                if (ImGui::SliderFloat("Background strength", &settings.ambientStrength, 0, 1)) SaveSettings(settings);
            }
            ImGui::BeginDisabled(settings.vlcAmbient);
            ImGui::SeparatorText("Blur");
            {
                int blurPasses = static_cast<int>(settings.blurPasses);
                if (ImGui::DragInt("Passes", &blurPasses, 0.1f, 0, 128))
                {
                    settings.blurPasses = static_cast<UINT>(std::max(0, blurPasses));
                    SaveSettings(settings);
                }

                int blurSamples = static_cast<int>(settings.blurSamples);
                if (ImGui::DragInt("Samples", &blurSamples, 0.1f, 1, 63))
                {
                    settings.blurSamples = static_cast<UINT>(std::max(1, blurSamples));
                    SaveSettings(settings);
                }

                int mipmapLevels = static_cast<int>(settings.mipmapLevels);
                if (ImGui::DragInt("Downsampling Levels", &mipmapLevels, 0.1f, 0, 12))
                {
                    settings.mipmapLevels = static_cast<UINT>(std::max(0, mipmapLevels));
                    SaveSettings(settings);
                }
            }

            ImGui::SeparatorText("Vignette");
            {
                if (ImGui::Checkbox("Enabled", &settings.vignetteEnabled))
                    SaveSettings(settings);

                if (ImGui::DragFloat("Intensity", (float*)&settings.vignetteIntensity, 0.01f, 0.0f, 1.0f))
                {
                    SaveSettings(settings);
                }

                if (ImGui::DragFloat("Radius", (float*)&settings.vignetteRadius, 0.01f, 0.0f, 1.0f))
                {
                    SaveSettings(settings);
                }

                if (ImGui::DragFloat("Smoothness", (float*)&settings.vignetteSmoothness, 0.01f, 0.0f, 1.0f))
                {
                    SaveSettings(settings);
                }
            }

            ImGui::EndDisabled();
            ImGui::SeparatorText("Misc");
            int frameRate = static_cast<int>(settings.frameRate);
            if (ImGui::DragInt("Frame rate", &frameRate, 0.1f, 10, 500))
            {
                settings.frameRate = static_cast<UINT>(std::max(10, frameRate));
                SaveSettings(settings);
            }

            if (ImGui::DragInt("Transition", &settings.transitionTimeMs, 1.0f, 0, 5000, "%d ms"))
            {
                settings.transitionTimeMs = std::clamp(settings.transitionTimeMs, 0, 5000);
                SaveSettings(settings);
            }
            ImGui::SameLine(); HelpMarker("Temporal color smoothing. 0 disables it; 300-1000 ms is a useful range.");

            ImGui::BeginDisabled(settings.vlcAmbient);
            int zoom = static_cast<int>(settings.zoom);
            if (ImGui::DragInt("Zoom", &zoom, 1, 0, 16))
            {
                settings.zoom = static_cast<UINT>(std::clamp(zoom, 0, 16));
                SaveSettings(settings);
            }

            if (ImGui::DragFloat("Stretch", &settings.stretchFactor, 0.01f, 0.1f, 5.0f))
                SaveSettings(settings);

            if (ImGui::Checkbox("Mirrored", &settings.mirrored))
                SaveSettings(settings);
            ImGui::EndDisabled();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("UI"))
        {
            if (ImGui::Checkbox("Show in taskbar", &settings.showInTaskbar))
            {
                SaveSettings(settings);
            }
            if (ImGui::Checkbox("Auto open settings", &settings.popupConfigOnFocus))
            {
                SaveSettings(settings);
            }
            ImGui::SameLine(); HelpMarker("Automatically open the settings window\n"
                "when the application is focused.");
            if (ImGui::DragFloat("UI Scale", &settings.uiScale, 0.1f, 0.5f, 3.0f))
            {
                SaveSettings(settings);
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Log"))
        {
            if (ImGui::Button("Copy"))
            {
                ImGui::SetClipboardText(log.c_str());
            }
            ImGui::TextWrapped("%s", log.c_str());
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Separator();

    if (ImGui::Button("Exit"))
        PostQuitMessage(0);

    ImGui::SameLine();
    ImGui::TextDisabled("%.1f FPS (%.2f ms/frame)", io.Framerate, 1000.0f / io.Framerate);

    ImGui::End();

    ImGui::Render();

    return open;
}

void UpdateWindowFlags(HWND hwnd, AppSettings& settings)
{
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (settings.showInTaskbar)
    {
        exStyle |= WS_EX_APPWINDOW;
        exStyle &= ~WS_EX_TOOLWINDOW;
    }
    else
    {
        exStyle |= WS_EX_TOOLWINDOW;
        exStyle &= ~WS_EX_APPWINDOW;
    }

    if (settings.popupConfigOnFocus)
    {
        exStyle |= WS_EX_NOACTIVATE;
    }
    else
    {
        exStyle &= ~WS_EX_NOACTIVATE;
    }

    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
}
