#include "app.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    HANDLE app_mutex = CreateMutex(NULL, TRUE, L"UltrawideAmbientlightAppInstance");
    if (!app_mutex)
        return static_cast<int>(GetLastError());

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        // another instance is running
        PresentWindow::FindAndShow();
        CloseHandle(app_mutex);
        return 0;
    }

    AmbientLight ambientLight;
    PresentWindow present;
    present.Create(hInstance, &ambientLight);
    present.Run();
    CloseHandle(app_mutex);
    return 0;
}
