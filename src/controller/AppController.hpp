#pragma once

#include "../model/ConfigModel.hpp"
#include "../model/BoxartService.hpp"
#include "../model/GameLibraryModel.hpp"
#include "../model/EmulatorModel.hpp"
#include "InputController.hpp"
#include <Photino.h>
#include <filesystem>
#include <string>
#include <mutex>
#include <atomic>
#include <thread>
#include <json.hpp>

namespace snex9x::controller {

class AppController {
private:
    static AppController* s_instance;

    model::ConfigModel m_configModel;
    model::BoxartService m_boxartService;
    model::GameLibraryModel m_libraryModel;
    model::EmulatorModel m_emulatorModel;
    InputController m_inputController;

    Photino* m_photino = nullptr;
    DWORD m_uiThreadId = 0;
    std::atomic<bool> m_windowFocused{true};
    std::atomic<bool> m_modalOpen{false};
    std::atomic<bool> m_shuttingDown{false};
    std::mutex m_ipcMutex;
    std::vector<std::string> m_pendingIpcQueue;
    std::thread m_uiGamepadThread;

public:
    AppController();
    ~AppController();

    bool Initialize(HINSTANCE hInstance, const std::filesystem::path& appRootDir);
    void Run();
    void Shutdown();

    static AppController* Instance() { return s_instance; }

    // Photino static callback trampolines
    static void OnWebMessageReceived(AutoString message);
    static void* OnWebResourceRequested(AutoString url, int* outNumBytes, AutoString* outContentType);
    static void OnWindowFocusIn();
    static void OnWindowFocusOut();
    static bool OnWindowClosing();
    static void FlushPendingIpcStatic();
    static void ToggleFullscreenStatic();

private:
    void HandleWebMessage(const std::string& jsonMessage);
    void* HandleCustomSchemeRequest(const std::wstring& uri, int* outNumBytes, AutoString* outContentType);
    void SendIpcMessage(const nlohmann::json& payload);
    void FlushPendingIpc();
    void BroadcastAppState();
    void ToggleFullscreen();
    void UiGamepadNavigationLoop();

    // Native Win32 COM File/Folder dialogs
    std::filesystem::path ShowOpenRomDialog();
    std::filesystem::path ShowSelectFolderDialog();
    std::filesystem::path ShowSaveStateDialog();
    std::filesystem::path ShowLoadStateDialog();
};

} // namespace snex9x::controller
