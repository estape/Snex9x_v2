#include "controller/AppController.hpp"
#include <windows.h>
#include <timeapi.h>
#include <filesystem>

// ============================================================================
// Functions
// ============================================================================
static std::filesystem::path ResolveAppRootDirectory() {
    wchar_t exePathBuf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();

    std::error_code ec;
    if (std::filesystem::exists(exeDir / "ui" / "index.html", ec) &&
        std::filesystem::exists(exeDir / "config" / "games_db.json", ec)) {
        return exeDir;
    }
    if (std::filesystem::exists(exeDir.parent_path() / "ui" / "index.html", ec)) {
        return exeDir.parent_path();
    }
    if (std::filesystem::exists(exeDir.parent_path().parent_path() / "ui" / "index.html", ec)) {
        return exeDir.parent_path().parent_path();
    }
    return std::filesystem::current_path();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    std::filesystem::path rootDir = ResolveAppRootDirectory();

    // Set 1ms high-precision timer resolution on Windows to eliminate frame pacing jitter
    timeBeginPeriod(1);

    snex9x::controller::AppController app;
    int exitCode = 0;
    if (!app.Initialize(hInstance, rootDir)) {
        exitCode = 1;
    } else {
        app.Run();
        app.Shutdown();
    }

    timeEndPeriod(1);
    return exitCode;
}

int main(int, char**) {
    return WinMain(GetModuleHandleW(nullptr), nullptr, GetCommandLineA(), SW_SHOWDEFAULT);
}
