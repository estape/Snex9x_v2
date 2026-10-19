#include "AppController.hpp"
#include <windows.h>
#include <shobjidl.h>
#include <objbase.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

namespace snex9x::controller {

AppController* AppController::s_instance = nullptr;

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return "";
    std::string out(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), &out[0], sizeNeeded, nullptr, nullptr);
    return out;
}

static std::string UrlDecodeUtf8(const std::string& encoded) {
    std::string out;
    out.reserve(encoded.size());
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            char hex[3] = { encoded[i + 1], encoded[i + 2], 0 };
            char* endPtr = nullptr;
            long val = std::strtol(hex, &endPtr, 16);
            if (endPtr == hex + 2) {
                out.push_back(static_cast<char>(val));
                i += 2;
                continue;
            }
        } else if (encoded[i] == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(encoded[i]);
    }
    return out;
}

static const wchar_t* GuessMimeType(const std::filesystem::path& filePath) {
    std::string ext = filePath.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".html" || ext == ".htm") return L"text/html; charset=utf-8";
    if (ext == ".css") return L"text/css; charset=utf-8";
    if (ext == ".js" || ext == ".mjs") return L"text/javascript; charset=utf-8";
    if (ext == ".json") return L"application/json; charset=utf-8";
    if (ext == ".png") return L"image/png";
    if (ext == ".ico") return L"image/x-icon";
    if (ext == ".jpg" || ext == ".jpeg") return L"image/jpeg";
    if (ext == ".svg") return L"image/svg+xml";
    if (ext == ".woff2") return L"font/woff2";
    return L"application/octet-stream";
}

AppController::AppController()
    : m_libraryModel(m_configModel, m_boxartService),
      m_emulatorModel(m_configModel),
      m_inputController(m_configModel, m_emulatorModel) {
    s_instance = this;
}

AppController::~AppController() {
    Shutdown();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool AppController::Initialize(HINSTANCE hInstance, const std::filesystem::path& appRootDir) {
    m_uiThreadId = GetCurrentThreadId();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    m_configModel.Initialize(appRootDir);

    m_boxartService.Initialize(m_configModel.GetBoxartsDir(),
        [this](const std::string& gameId, const std::string& boxartFilename) {
            if (m_shuttingDown.load()) return;
            std::string boxartUrl = "snes://boxarts/" + model::BoxartService::UrlEncodeFilename(boxartFilename);
            SendIpcMessage({
                {"type", "boxartReady"},
                {"gameId", gameId},
                {"boxartFilename", boxartFilename},
                {"boxartUrl", boxartUrl}
            });
        });

    m_libraryModel.Initialize();
    m_emulatorModel.Initialize();
    m_inputController.Initialize();

    m_emulatorModel.SetInputPollCallback([this]() {
        m_inputController.PollAndReportInputs(m_windowFocused.load());
    });

    m_emulatorModel.SetStatusMessageCallback([this](const std::string& msg) {
        SendIpcMessage({
            {"type", "toast"},
            {"message", msg},
            {"level", "info"}
        });
    });

    Photino::Register(hInstance);

    std::filesystem::path iconPath = appRootDir / "assets" / "snex9x.ico";
    std::string iconPathUtf8 = iconPath.u8string();

    PhotinoInitParams initParams{};
    initParams.Size = sizeof(PhotinoInitParams);
    initParams.Title = (AutoString)"Snex9x v2 - Super Nintendo Entertainment System";
    initParams.StartUrl = (AutoString)L"snes://app/index.html";
    if (std::filesystem::exists(iconPath)) {
        initParams.WindowIconFile = (AutoString)iconPathUtf8.c_str();
    }
    initParams.CustomSchemeNames[0] = (AutoString)"snes";
    initParams.CustomSchemeHandler = (WebResourceRequestedCallback*)&AppController::OnWebResourceRequested;
    initParams.WebMessageReceivedHandler = (WebMessageReceivedCallback*)&AppController::OnWebMessageReceived;
    initParams.FocusInHandler = (FocusInCallback*)&AppController::OnWindowFocusIn;
    initParams.FocusOutHandler = (FocusOutCallback*)&AppController::OnWindowFocusOut;
    initParams.ClosingHandler = (ClosingCallback*)&AppController::OnWindowClosing;

    initParams.Width = 1360;
    initParams.Height = 860;
    initParams.MinWidth = 960;
    initParams.MinHeight = 620;
    initParams.MaxWidth = INT_MAX;
    initParams.MaxHeight = INT_MAX;
    initParams.CenterOnInitialize = true;
    initParams.Resizable = true;
    initParams.ContextMenuEnabled = false;
    initParams.DevToolsEnabled = true;
    initParams.GrantBrowserPermissions = true;
    initParams.MediaAutoplayEnabled = true;
    initParams.FileSystemAccessEnabled = true;
    initParams.WebSecurityEnabled = false;
    initParams.SmoothScrollingEnabled = true;
    initParams.Zoom = 100;

    m_photino = new Photino(&initParams);
    if (HWND hwnd = m_photino->getHwnd()) {
        HICON hIconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR | LR_SHARED);
        HICON hIconBig = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON,
            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR | LR_SHARED);
        if (hIconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
        if (hIconBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    }
    m_uiGamepadThread = std::thread(&AppController::UiGamepadNavigationLoop, this);
    return true;
}

void AppController::Run() {
    if (m_photino) {
        m_photino->WaitForExit();
    }
}

void AppController::Shutdown() {
    if (m_shuttingDown.exchange(true)) return;
    if (m_uiGamepadThread.joinable()) {
        m_uiGamepadThread.join();
    }
    m_boxartService.Shutdown();
    m_emulatorModel.Shutdown();
    m_inputController.Shutdown();
    if (m_photino) {
        delete m_photino;
        m_photino = nullptr;
    }
    CoUninitialize();
}

void AppController::OnWebMessageReceived(AutoString message) {
    if (!s_instance || !message) return;
    std::wstring wmsg(message);
    s_instance->HandleWebMessage(WideToUtf8(wmsg));
}

void* AppController::OnWebResourceRequested(AutoString url, int* outNumBytes, AutoString* outContentType) {
    if (!s_instance || !url) return nullptr;
    return s_instance->HandleCustomSchemeRequest(std::wstring(url), outNumBytes, outContentType);
}

void AppController::OnWindowFocusIn() {
    if (s_instance) s_instance->m_windowFocused = true;
}

void AppController::OnWindowFocusOut() {
    if (s_instance) s_instance->m_windowFocused = false;
}

bool AppController::OnWindowClosing() {
    if (s_instance) {
        s_instance->m_shuttingDown = true;
        s_instance->m_boxartService.Shutdown();
        s_instance->m_emulatorModel.StopAndCloseRom();
    }
    return false; // allow close
}

void AppController::FlushPendingIpcStatic() {
    if (s_instance) {
        s_instance->FlushPendingIpc();
    }
}

void AppController::ToggleFullscreenStatic() {
    if (s_instance) {
        s_instance->ToggleFullscreen();
    }
}

void AppController::ToggleFullscreen() {
    auto gfx = m_configModel.GetGraphicsSettings();
    gfx.fullscreen = !gfx.fullscreen;
    m_configModel.SetGraphicsSettings(gfx);
    m_configModel.SaveSettings();
    if (m_photino) {
        m_photino->SetFullScreen(gfx.fullscreen);
    }
    BroadcastAppState();
}

void AppController::UiGamepadNavigationLoop() {
    using clock = std::chrono::steady_clock;
    auto lastRepeatTime = clock::now();
    std::string activeHeldAction = "";
    bool repeatStarted = false;

    bool prevSouth = false;
    bool prevWest = false;
    bool prevEast = false;
    bool prevF11 = false;

    while (!m_shuttingDown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        if (m_shuttingDown.load()) break;

        // Global F11 key edge detection (works in both library and gameplay, even when WebView2 is unfocused)
        if (m_windowFocused.load()) {
            bool f11Pressed = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
            if (f11Pressed && !prevF11) {
                if (m_photino) {
                    m_photino->Invoke(&AppController::ToggleFullscreenStatic);
                }
            }
            prevF11 = f11Pressed;
        }

        // Library gamepad navigation is active only when emulation is NOT running and no modal dialog is open
        if (m_emulatorModel.IsRunning() || m_modalOpen.load()) {
            prevSouth = false;
            prevWest = false;
            prevEast = false;
            activeHeldAction.clear();
            continue;
        }

        auto nav = m_inputController.PollUiGamepadNavigationState();
        auto now = clock::now();

        // Directional navigation with initial delay (260ms) and repeat rate (70ms)
        std::string currentDirection = "";
        if (nav.down) currentDirection = "down";
        else if (nav.up) currentDirection = "up";
        else if (nav.pageDown) currentDirection = "pageDown";
        else if (nav.pageUp) currentDirection = "pageUp";

        if (!currentDirection.empty()) {
            if (activeHeldAction != currentDirection) {
                // Initial press
                activeHeldAction = currentDirection;
                repeatStarted = false;
                lastRepeatTime = now;
                SendIpcMessage({{"type", "navLibrary"}, {"action", currentDirection}});
            } else {
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRepeatTime).count();
                if (!repeatStarted && elapsedMs >= 260) {
                    repeatStarted = true;
                    lastRepeatTime = now;
                    SendIpcMessage({{"type", "navLibrary"}, {"action", currentDirection}});
                } else if (repeatStarted && elapsedMs >= 70) {
                    lastRepeatTime = now;
                    SendIpcMessage({{"type", "navLibrary"}, {"action", currentDirection}});
                }
            }
        } else {
            activeHeldAction.clear();
            repeatStarted = false;
        }

        // DualShock 4 X button (South) edge detection -> Launch selected game
        if (nav.south && !prevSouth) {
            SendIpcMessage({{"type", "navLibrary"}, {"action", "launch"}});
        }
        prevSouth = nav.south;

        // DualShock 4 Square button (West) edge detection -> Toggle Fullscreen in Library view
        if (nav.west && !prevWest) {
            if (m_photino) {
                m_photino->Invoke(&AppController::ToggleFullscreenStatic);
            }
        }
        prevWest = nav.west;

        // DualShock 4 Circle button (East) edge detection -> Close dialogs
        if (nav.east && !prevEast) {
            SendIpcMessage({{"type", "navLibrary"}, {"action", "back"}});
        }
        prevEast = nav.east;
    }
}

void AppController::FlushPendingIpc() {
    if (!m_photino || m_shuttingDown.load()) return;
    std::vector<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(m_ipcMutex);
        batch.swap(m_pendingIpcQueue);
    }
    for (const auto& msg : batch) {
        m_photino->SendWebMessage((AutoString)msg.c_str());
    }
}

void AppController::SendIpcMessage(const json& payload) {
    if (!m_photino || m_shuttingDown.load()) return;
    std::string serialized = payload.dump();
    if (GetCurrentThreadId() == m_uiThreadId) {
        m_photino->SendWebMessage((AutoString)serialized.c_str());
    } else {
        {
            std::lock_guard<std::mutex> lock(m_ipcMutex);
            m_pendingIpcQueue.push_back(std::move(serialized));
        }
        m_photino->Invoke(&AppController::FlushPendingIpcStatic);
    }
}

void AppController::BroadcastAppState() {
    json state = {
        {"type", "appState"},
        {"library", m_libraryModel.ToJson()["games"]},
        {"settings", m_configModel.SettingsToJson()},
        {"controls", m_configModel.ControlsToJson()},
        {"devices", m_inputController.GetConnectedDevicesJson()},
        {"emulation", {
            {"running", m_emulatorModel.IsRunning()},
            {"paused", m_emulatorModel.IsPaused()},
            {"activeGameId", m_emulatorModel.GetActiveGameId()},
            {"fps", m_emulatorModel.GetCurrentFps()}
        }}
    };
    SendIpcMessage(state);
}

void* AppController::HandleCustomSchemeRequest(const std::wstring& uri, int* outNumBytes, AutoString* outContentType) {
    *outNumBytes = 0;
    *outContentType = nullptr;

    std::string uriUtf8 = WideToUtf8(uri);
    // Strip query string if present
    size_t queryPos = uriUtf8.find('?');
    if (queryPos != std::string::npos) {
        uriUtf8 = uriUtf8.substr(0, queryPos);
    }

    // 1. Check if video frame fallback request: same-origin snes://app/api/frame or snes://frame/
    if (uriUtf8.rfind("snes://app/api/frame", 0) == 0 || uriUtf8.rfind("snes://frame/", 0) == 0) {
        std::vector<uint8_t> frameData;
        m_emulatorModel.CopyLatestFrameBuffer(frameData);
        if (frameData.empty()) return nullptr;
        void* mem = CoTaskMemAlloc(frameData.size());
        if (!mem) return nullptr;
        std::memcpy(mem, frameData.data(), frameData.size());
        *outNumBytes = static_cast<int>(frameData.size());
        *outContentType = (AutoString)L"application/octet-stream";
        return mem;
    }

    // 2. Check if boxart image request: snes://boxarts/<filename>
    if (uriUtf8.rfind("snes://boxarts/", 0) == 0) {
        std::string encodedName = uriUtf8.substr(std::strlen("snes://boxarts/"));
        std::string decodedName = UrlDecodeUtf8(encodedName);
        std::filesystem::path boxartPath = m_boxartService.GetCachedBoxartPath(decodedName);
        std::error_code ec;
        if (std::filesystem::exists(boxartPath, ec)) {
            std::ifstream f(boxartPath, std::ios::binary | std::ios::ate);
            if (f.is_open()) {
                std::streamsize size = f.tellg();
                if (size > 0) {
                    f.seekg(0, std::ios::beg);
                    void* mem = CoTaskMemAlloc(static_cast<size_t>(size));
                    if (mem && f.read(reinterpret_cast<char*>(mem), size)) {
                        *outNumBytes = static_cast<int>(size);
                        *outContentType = (AutoString)L"image/png";
                        return mem;
                    }
                    if (mem) CoTaskMemFree(mem);
                }
            }
        }
        return nullptr;
    }

    // 3. Default UI asset request: snes://app/<path>
    const std::string appPrefix = "snes://app/";
    std::string relPath = "index.html";
    if (uriUtf8.rfind(appPrefix, 0) == 0) {
        relPath = UrlDecodeUtf8(uriUtf8.substr(appPrefix.size()));
        if (relPath.empty() || relPath == "/") {
            relPath = "index.html";
        }
    }

    // Prevent directory traversal
    if (relPath.find("..") != std::string::npos) {
        return nullptr;
    }

    std::filesystem::path fullPath = m_configModel.GetUiDir() / std::filesystem::u8path(relPath);
    std::error_code ec;
    if (!std::filesystem::exists(fullPath, ec) || !std::filesystem::is_regular_file(fullPath, ec)) {
        return nullptr;
    }

    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return nullptr;
    std::streamsize size = file.tellg();
    if (size < 0) return nullptr;
    file.seekg(0, std::ios::beg);

    void* buffer = CoTaskMemAlloc(static_cast<size_t>(std::max<std::streamsize>(size, 1)));
    if (!buffer) return nullptr;
    if (size > 0 && !file.read(reinterpret_cast<char*>(buffer), size)) {
        CoTaskMemFree(buffer);
        return nullptr;
    }

    *outNumBytes = static_cast<int>(size);
    *outContentType = (AutoString)GuessMimeType(fullPath);
    return buffer;
}

void AppController::HandleWebMessage(const std::string& jsonMessage) {
    try {
        json msg = json::parse(jsonMessage);
        std::string action = msg.value("action", "");

        if (action == "initApp") {
            if (m_photino) {
                uint8_t* sharedPtr = m_photino->InitializeSharedVideoBuffer(model::EmulatorModel::kMaxVideoBufferBytes);
                if (sharedPtr) {
                    m_emulatorModel.SetSharedVideoBufferPtr(sharedPtr);
                }
            }
            BroadcastAppState();
        }
        else if (action == "selectGame") {
            std::string gameId = msg.value("gameId", "");
            const auto* game = m_libraryModel.FindGameById(gameId);
            if (game) {
                m_boxartService.RequestBoxartAsync(game->id, game->boxartFilename, game->name);
            }
        }
        else if (action == "launchGame") {
            std::string gameId = msg.value("gameId", "");
            const auto* game = m_libraryModel.FindGameById(gameId);
            std::filesystem::path romPath;
            if (game && !game->romPath.empty()) {
                romPath = std::filesystem::u8path(game->romPath);
            }

            std::error_code ec;
            if (romPath.empty() || !std::filesystem::exists(romPath, ec)) {
                // Prompt user to select a local ROM file to link & play
                romPath = ShowOpenRomDialog();
                if (romPath.empty()) return;
                auto updatedEntry = m_libraryModel.AddOrUpdateRomFile(romPath);
                gameId = updatedEntry.id;
            }

            if (m_emulatorModel.LoadAndStartRom(romPath, gameId)) {
                m_libraryModel.RecordGamePlayed(gameId);
                BroadcastAppState();
            }
        }
        else if (action == "openRomDialog") {
            std::filesystem::path romPath = ShowOpenRomDialog();
            if (!romPath.empty()) {
                auto entry = m_libraryModel.AddOrUpdateRomFile(romPath);
                bool autoStart = msg.value("autoStart", true);
                if (autoStart && m_emulatorModel.LoadAndStartRom(romPath, entry.id)) {
                    m_libraryModel.RecordGamePlayed(entry.id);
                }
                BroadcastAppState();
                SendIpcMessage({
                    {"type", "gameAdded"},
                    {"gameId", entry.id}
                });
            }
        }
        else if (action == "scanRomFolderDialog") {
            std::filesystem::path folderPath = ShowSelectFolderDialog();
            if (!folderPath.empty()) {
                int count = m_libraryModel.ScanRomDirectory(folderPath);
                BroadcastAppState();
                SendIpcMessage({
                    {"type", "toast"},
                    {"message", "Scanned folder and imported " + std::to_string(count) + " SNES ROM(s)."},
                    {"level", "success"}
                });
            }
        }
        else if (action == "addDatabaseGameToLibrary") {
            std::string dbId = msg.value("id", "");
            std::string noIntroName = msg.value("noIntroName", "");
            const model::GameDbEntry* meta = m_configModel.FindGameByName(noIntroName);
            if (meta) {
                std::filesystem::path romPath = ShowOpenRomDialog();
                if (!romPath.empty()) {
                    auto entry = m_libraryModel.AddOrUpdateRomFile(romPath);
                    BroadcastAppState();
                    SendIpcMessage({
                        {"type", "gameAdded"},
                        {"gameId", entry.id}
                    });
                }
            }
        }
        else if (action == "searchDatabase") {
            std::string query = msg.value("query", "");
            std::string normQuery = model::ConfigModel::NormalizeLookupKey(query);
            json results = json::array();
            const auto& allDb = m_configModel.GetAllDatabaseGames();
            for (const auto& entry : allDb) {
                if (normQuery.empty() ||
                    model::ConfigModel::NormalizeLookupKey(entry.name).find(normQuery) != std::string::npos ||
                    model::ConfigModel::NormalizeLookupKey(entry.noIntroName).find(normQuery) != std::string::npos ||
                    model::ConfigModel::NormalizeLookupKey(entry.year).find(normQuery) != std::string::npos) {
                    results.push_back({
                        {"id", entry.id},
                        {"name", entry.name},
                        {"noIntroName", entry.noIntroName},
                        {"year", entry.year},
                        {"description", entry.description},
                        {"developer", entry.developer},
                        {"publisher", entry.publisher},
                        {"genre", entry.genre},
                        {"players", entry.players},
                        {"region", entry.region},
                        {"hardware", entry.hardware},
                        {"boxartFilename", entry.boxartFilename}
                    });
                    if (results.size() >= 100) break;
                }
            }
            SendIpcMessage({
                {"type", "databaseSearchResults"},
                {"results", results}
            });
        }
        else if (action == "togglePause") {
            if (m_emulatorModel.IsRunning()) {
                m_emulatorModel.SetPaused(!m_emulatorModel.IsPaused());
                BroadcastAppState();
            }
        }
        else if (action == "setPaused") {
            if (m_emulatorModel.IsRunning()) {
                m_emulatorModel.SetPaused(msg.value("paused", false));
                BroadcastAppState();
            }
        }
        else if (action == "softReset") {
            m_emulatorModel.SoftReset();
            SendIpcMessage({{"type", "toast"}, {"message", "SNES Soft Reset executed."}, {"level", "info"}});
        }
        else if (action == "hardReset") {
            m_emulatorModel.HardReset();
            SendIpcMessage({{"type", "toast"}, {"message", "SNES Hard Reset (Power Cycle) executed."}, {"level", "info"}});
        }
        else if (action == "closeRom") {
            m_emulatorModel.StopAndCloseRom();
            BroadcastAppState();
        }
        else if (action == "saveStateSlot") {
            int slot = msg.value("slot", m_configModel.GetEmulationSettings().activeStateSlot);
            bool ok = m_emulatorModel.SaveStateSlot(slot);
            SendIpcMessage({
                {"type", "toast"},
                {"message", ok ? ("Saved State to Slot " + std::to_string(slot)) : "Failed to save state (is a game running?)"},
                {"level", ok ? "success" : "warning"}
            });
        }
        else if (action == "loadStateSlot") {
            int slot = msg.value("slot", m_configModel.GetEmulationSettings().activeStateSlot);
            bool ok = m_emulatorModel.LoadStateSlot(slot);
            SendIpcMessage({
                {"type", "toast"},
                {"message", ok ? ("Loaded State from Slot " + std::to_string(slot)) : ("No saved state found in Slot " + std::to_string(slot))},
                {"level", ok ? "success" : "warning"}
            });
        }
        else if (action == "setStateSlot") {
            int slot = std::clamp(msg.value("slot", 1), 1, 10);
            auto emu = m_configModel.GetEmulationSettings();
            emu.activeStateSlot = slot;
            m_configModel.SetEmulationSettings(emu);
            m_configModel.SaveSettings();
            BroadcastAppState();
        }
        else if (action == "saveStateFileDialog") {
            if (!m_emulatorModel.IsRunning()) {
                SendIpcMessage({{"type", "toast"}, {"message", "Start a game before saving a state file."}, {"level", "warning"}});
                return;
            }
            bool wasPaused = m_emulatorModel.IsPaused();
            m_emulatorModel.SetPaused(true);
            std::filesystem::path path = ShowSaveStateDialog();
            m_emulatorModel.SetPaused(wasPaused);
            if (!path.empty()) {
                bool ok = m_emulatorModel.SaveStateFile(path);
                SendIpcMessage({
                    {"type", "toast"},
                    {"message", ok ? ("Saved state to " + path.filename().u8string()) : "Failed to save state file."},
                    {"level", ok ? "success" : "error"}
                });
            }
        }
        else if (action == "loadStateFileDialog") {
            if (!m_emulatorModel.IsRunning()) {
                SendIpcMessage({{"type", "toast"}, {"message", "Start a game before loading a state file."}, {"level", "warning"}});
                return;
            }
            bool wasPaused = m_emulatorModel.IsPaused();
            m_emulatorModel.SetPaused(true);
            std::filesystem::path path = ShowLoadStateDialog();
            m_emulatorModel.SetPaused(wasPaused);
            if (!path.empty()) {
                bool ok = m_emulatorModel.LoadStateFile(path);
                SendIpcMessage({
                    {"type", "toast"},
                    {"message", ok ? ("Loaded state from " + path.filename().u8string()) : "Failed to load state file."},
                    {"level", ok ? "success" : "error"}
                });
            }
        }
        else if (action == "updateSettings") {
            bool prevFullscreen = m_configModel.GetGraphicsSettings().fullscreen;
            m_configModel.UpdateSettingsFromJson(msg);
            m_configModel.SaveSettings();
            m_emulatorModel.ApplyRuntimeSettings();

            bool newFullscreen = m_configModel.GetGraphicsSettings().fullscreen;
            if (m_photino && newFullscreen != prevFullscreen) {
                m_photino->SetFullScreen(newFullscreen);
            }
            BroadcastAppState();
        }
        else if (action == "toggleFullscreen") {
            ToggleFullscreen();
        }
        else if (action == "updateControls") {
            m_configModel.UpdateControlsFromJson(msg);
            m_configModel.SaveControls();
            m_configModel.SaveSettings();
            m_emulatorModel.ApplyRuntimeSettings();
            BroadcastAppState();
            SendIpcMessage({
                {"type", "toast"},
                {"message", "Controller bindings saved to config/controls.json."},
                {"level", "success"}
            });
        }
        else if (action == "resetControls") {
            m_configModel.ResetDefaultControls();
            m_configModel.SaveControls();
            m_emulatorModel.ApplyRuntimeSettings();
            BroadcastAppState();
            SendIpcMessage({
                {"type", "toast"},
                {"message", "Controller bindings reset to defaults."},
                {"level", "info"}
            });
        }
        else if (action == "refreshDevices") {
            SendIpcMessage({
                {"type", "devicesUpdated"},
                {"devices", m_inputController.GetConnectedDevicesJson()}
            });
        }
        else if (action == "pollGamepadBinding") {
            std::string deviceId = msg.value("deviceId", "");
            std::string pressedBtn = m_inputController.PollFirstPressedGamepadButton(deviceId);
            if (!pressedBtn.empty()) {
                SendIpcMessage({
                    {"type", "gamepadButtonCaptured"},
                    {"button", pressedBtn}
                });
            }
        }
        else if (action == "testRumble") {
            int playerIdx = msg.value("playerIndex", 1);
            bool ok = m_inputController.TriggerPlayerRumble(playerIdx);
            SendIpcMessage({
                {"type", "toast"},
                {"message", ok ? ("Triggered haptic rumble on Player " + std::to_string(playerIdx) + " controller.") : "No rumble-capable gamepad connected for this player."},
                {"level", ok ? "info" : "warning"}
            });
        }
        else if (action == "keyState") {
            std::string code = msg.value("code", "");
            bool pressed = msg.value("pressed", false);
            m_inputController.SetWebKeyState(code, pressed);
        }
        else if (action == "setModalOpen") {
            m_modalOpen = msg.value("open", false);
        }
        else if (action == "exitApp") {
            if (m_photino) {
                m_photino->Close();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[AppController] Error handling web message: " << e.what() << "\n";
    }
}

std::filesystem::path AppController::ShowOpenRomDialog() {
    std::filesystem::path resultPath;
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr) && pFileOpen) {
        COMDLG_FILTERSPEC rgSpec[] = {
            { L"SNES ROM Images (*.sfc;*.smc;*.swc;*.fig;*.bs)", L"*.sfc;*.smc;*.swc;*.fig;*.bs" },
            { L"All Files (*.*)", L"*.*" }
        };
        pFileOpen->SetFileTypes(ARRAYSIZE(rgSpec), rgSpec);
        pFileOpen->SetTitle(L"Open Super Nintendo ROM");
        HWND owner = m_photino ? m_photino->getHwnd() : nullptr;
        if (SUCCEEDED(pFileOpen->Show(owner))) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pFileOpen->GetResult(&pItem)) && pItem) {
                PWSTR pszFilePath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)) && pszFilePath) {
                    resultPath = std::filesystem::path(pszFilePath);
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    return resultPath;
}

std::filesystem::path AppController::ShowSelectFolderDialog() {
    std::filesystem::path resultPath;
    IFileOpenDialog* pFolderOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFolderOpen));
    if (SUCCEEDED(hr) && pFolderOpen) {
        DWORD dwOptions = 0;
        if (SUCCEEDED(pFolderOpen->GetOptions(&dwOptions))) {
            pFolderOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }
        pFolderOpen->SetTitle(L"Select SNES ROMs Directory");
        HWND owner = m_photino ? m_photino->getHwnd() : nullptr;
        if (SUCCEEDED(pFolderOpen->Show(owner))) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pFolderOpen->GetResult(&pItem)) && pItem) {
                PWSTR pszFolderPath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFolderPath)) && pszFolderPath) {
                    resultPath = std::filesystem::path(pszFolderPath);
                    CoTaskMemFree(pszFolderPath);
                }
                pItem->Release();
            }
        }
        pFolderOpen->Release();
    }
    return resultPath;
}

std::filesystem::path AppController::ShowSaveStateDialog() {
    std::filesystem::path resultPath;
    IFileSaveDialog* pFileSave = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_IFileSaveDialog, reinterpret_cast<void**>(&pFileSave));
    if (SUCCEEDED(hr) && pFileSave) {
        COMDLG_FILTERSPEC rgSpec[] = {
            { L"Snes9x Save State (*.frz;*.s01)", L"*.frz;*.s01" },
            { L"All Files (*.*)", L"*.*" }
        };
        pFileSave->SetFileTypes(ARRAYSIZE(rgSpec), rgSpec);
        pFileSave->SetDefaultExtension(L"frz");
        pFileSave->SetTitle(L"Save Snes9x Emulation State");
        HWND owner = m_photino ? m_photino->getHwnd() : nullptr;
        if (SUCCEEDED(pFileSave->Show(owner))) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pFileSave->GetResult(&pItem)) && pItem) {
                PWSTR pszFilePath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)) && pszFilePath) {
                    resultPath = std::filesystem::path(pszFilePath);
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileSave->Release();
    }
    return resultPath;
}

std::filesystem::path AppController::ShowLoadStateDialog() {
    std::filesystem::path resultPath;
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr) && pFileOpen) {
        COMDLG_FILTERSPEC rgSpec[] = {
            { L"Snes9x Save State (*.frz;*.s01;*.s02;*.s03;*.s04;*.s05;*.s06;*.s07;*.s08;*.s09;*.s10)", L"*.frz;*.s01;*.s02;*.s03;*.s04;*.s05;*.s06;*.s07;*.s08;*.s09;*.s10" },
            { L"All Files (*.*)", L"*.*" }
        };
        pFileOpen->SetFileTypes(ARRAYSIZE(rgSpec), rgSpec);
        pFileOpen->SetTitle(L"Load Snes9x Emulation State");
        HWND owner = m_photino ? m_photino->getHwnd() : nullptr;
        if (SUCCEEDED(pFileOpen->Show(owner))) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pFileOpen->GetResult(&pItem)) && pItem) {
                PWSTR pszFilePath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)) && pszFilePath) {
                    resultPath = std::filesystem::path(pszFilePath);
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    return resultPath;
}

} // namespace snex9x::controller

