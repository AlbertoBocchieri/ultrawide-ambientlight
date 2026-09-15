#include "present.h"
#include "resources.h"
#include "shellapi.h"

#define APP_WINDOW_CLASS_NAME L"ambientlightapp"

static constexpr UINT_PTR DISPLAY_CHANGE_TIMER_ID = 1;
static constexpr UINT DISPLAY_CHANGE_DEBOUNCE_MS = 500;

PresentWindow::PresentWindow()
{
}

PresentWindow::~PresentWindow()
{
}

void PresentWindow::FindAndShow()
{
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
        {
            wchar_t className[256];
            GetClassName(hwnd, className, 256);
            if (wcscmp(className, APP_WINDOW_CLASS_NAME) == 0)
            {
                ShowWindow(hwnd, SW_SHOWNORMAL);
                SetForegroundWindow(hwnd);

                SendMessage(hwnd, WM_TOGGLE_CONFIG_WINDOW, 1, 0);

                return FALSE; // stop enumeration
            }
            return TRUE; // continue enumeration
        }, 0);
}

void PresentWindow::Create(HINSTANCE hInstance, AmbientLight* render)
{
    m_render = render;

    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);

    // Create a window class
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = APP_WINDOW_CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hIcon = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDB_APPICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    RegisterClassEx(&wc);

    // use direct composition
    DWORD dwExStyle = WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TOPMOST;

    // frameless
    DWORD dwStyle = WS_POPUP;

    // Create a window with the screen size
    RECT desktopRect;
    GetWindowRect(GetDesktopWindow(), &desktopRect);
    HWND hwnd = CreateWindowEx(dwExStyle,
        APP_WINDOW_CLASS_NAME,
        L"ambientlight",
        dwStyle,
        0, 0, desktopRect.right, desktopRect.bottom,
        0,
        0,
        hInstance,
        render
    );

    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);

    // do not capture
    SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);

    m_hwnd = hwnd;

    NOTIFYICONDATA nid;
    memset(&nid, 0, sizeof(nid)); // Initialize to zeros
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd; // Handle to your application's main window
    nid.uID = IDB_APPICON; // Unique ID for your icon (from resource file or a constant)
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; // Flags indicating what information is provided
    nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDB_APPICON)); // Handle to your icon
    wcscpy_s(nid.szTip, L"Ambientlight"); // Tooltip text
    nid.uCallbackMessage = WM_USER_SHELLICON; // Your custom message ID

    Shell_NotifyIcon(NIM_ADD, &nid);

    return;
}

void PresentWindow::Run()
{
    // Show the window
    ShowWindow(m_hwnd, SW_SHOWNORMAL);

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (m_render)
            m_render->Render();
    }
}

LRESULT CALLBACK PresentWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    AmbientLight* pRender = reinterpret_cast<AmbientLight*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    if (pRender && pRender->WndProc(hwnd, message, wParam, lParam))
        return true;

    switch (message)
    {
    case WM_CREATE:
    {
        // Save the DXSample* passed in to CreateWindow.
        LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));

        PostMessage(hwnd, WM_DISPLAYCHANGE, 0, 0);
    }
    return 0;

    case WM_DISPLAYCHANGE:
    {
        // HDR toggles emit several mode-change notifications while DWM and the
        // driver are still rebuilding the display path. Reinitialize once the
        // notifications have settled instead of repeatedly replacing devices.
        SetTimer(hwnd, DISPLAY_CHANGE_TIMER_ID, DISPLAY_CHANGE_DEBOUNCE_MS, nullptr);
    }
    return 0;

    case WM_TIMER:
    {
        if (wParam != DISPLAY_CHANGE_TIMER_ID)
            break;

        KillTimer(hwnd, DISPLAY_CHANGE_TIMER_ID);
        RECT desktopRect = pRender->GetPresentRect();
        SetWindowPos(hwnd, nullptr, desktopRect.left, desktopRect.top,
            desktopRect.right - desktopRect.left, desktopRect.bottom - desktopRect.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        if (FAILED(pRender->Initialize(hwnd)))
        {
            // The display path can remain unavailable briefly after a mode
            // switch. Retry without rendering partially initialized resources.
            SetTimer(hwnd, DISPLAY_CHANGE_TIMER_ID, 1000, nullptr);
        }
    }
    return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }


    return DefWindowProc(hwnd, message, wParam, lParam);
}
