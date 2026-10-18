#include "controller/AppController.hpp"
#include <windows.h>
#include <timeapi.h>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <thread>

// ============================================================================
// File-scope Constants and Variables
// ============================================================================
static const char kSelfTestFlag[] = "--self-test";
static const char kDemoRomFileName[] = "Snex9x_Interactive_Test.sfc";
static const char kDemoGameId[] = "builtin_snex9x_test";
static const char kTestSmwCrc32[] = "B19ED489";
static const size_t kMinDatabaseGamesExpected = 4000;

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

static int RunSelfTest(const std::filesystem::path& rootDir) {
    std::cout << "=== Snex9x v2 Automated Self-Test ===\n";
    std::cout << "[1/5] Initializing ConfigModel at: " << rootDir.u8string() << "\n";

    snex9x::model::ConfigModel config;
    if (!config.Initialize(rootDir)) {
        std::cerr << "FAIL: ConfigModel::Initialize failed\n";
        return 1;
    }

    size_t dbSize = config.GetAllDatabaseGames().size();
    std::cout << "      Loaded games_db.json entries: " << dbSize << "\n";
    if (dbSize < kMinDatabaseGamesExpected) {
        std::cerr << "FAIL: Expected >" << kMinDatabaseGamesExpected << " games in games_db.json\n";
        return 1;
    }

    const auto* smw = config.FindGameByCrc32(kTestSmwCrc32);
    if (!smw || smw->name.find("Super Mario World") == std::string::npos) {
        std::cerr << "FAIL: CRC32 lookup for Super Mario World (" << kTestSmwCrc32 << ") failed\n";
        return 1;
    }
    std::cout << "      Verified CRC32 lookup: " << smw->name << " (" << smw->year << ") -> " << smw->boxartFilename << "\n";

    std::cout << "[2/5] Testing BoxartService & GameLibraryModel...\n";
    std::atomic<bool> boxartDownloaded{false};
    snex9x::model::BoxartService boxart;
    boxart.Initialize(config.GetBoxartsDir(), [&](const std::string& gameId, const std::string& filename) {
        std::cout << "      Boxart ready callback: " << gameId << " -> " << filename << "\n";
        boxartDownloaded = true;
    });

    snex9x::model::GameLibraryModel library(config, boxart);
    library.Initialize();
    auto games = library.GetGames();
    if (games.empty()) {
        library.ScanRomDirectory(config.GetConfigDir() / "roms");
        games = library.GetGames();
    }
    std::cout << "      Library initialized with " << games.size() << " games.\n";
    if (games.empty()) {
        std::cerr << "FAIL: Library is empty\n";
        return 1;
    }

    // Wait up to 6 seconds for at least one boxart download from libretro-thumbnails
    for (int i = 0; i < 60 && !boxartDownloaded.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "      Boxart cache status: " << (boxartDownloaded.load() ? "Downloaded/Cached OK" : "Offline/Skipped") << "\n";

    std::cout << "[3/5] Initializing EmulatorModel (Snes9x Core + SDL3 Audio) & InputController (SDL3 Gamepad)...\n";
    snex9x::model::EmulatorModel emulator(config);
    if (!emulator.Initialize()) {
        std::cerr << "FAIL: EmulatorModel::Initialize failed\n";
        return 1;
    }

    snex9x::controller::InputController input(config, emulator);
    input.Initialize();
    auto devices = input.GetConnectedDevices();
    std::cout << "      Connected input devices detected: " << devices.size() << "\n";

    std::cout << "[4/5] Booting built-in SNES ROM and running 30 frames of 65c816 + PPU emulation...\n";
    std::filesystem::path demoRom = config.GetConfigDir() / "roms" / kDemoRomFileName;
    if (!emulator.LoadAndStartRom(demoRom, kDemoGameId)) {
        std::cerr << "FAIL: Failed to load and start built-in SNES ROM\n";
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(550));

    std::vector<uint8_t> frameBuf;
    emulator.CopyLatestFrameBuffer(frameBuf);
    if (frameBuf.size() <= snex9x::model::EmulatorModel::kVideoHeaderBytes) {
        std::cerr << "FAIL: Video frame buffer empty\n";
        return 1;
    }
    const uint32_t* hdr = reinterpret_cast<const uint32_t*>(frameBuf.data());
    const uint32_t* pixels = reinterpret_cast<const uint32_t*>(frameBuf.data() + snex9x::model::EmulatorModel::kVideoHeaderBytes);
    uint32_t samplePixel = pixels[(224 / 2) * 256 + (256 / 2)];
    std::cout << "      Emulated Frame #" << hdr[0] << " | Dimensions: " << hdr[1] << "x" << hdr[2]
              << " | Sample RGBA8888: 0x" << std::hex << samplePixel << std::dec << "\n";
    if (hdr[0] == 0 || hdr[1] < 256 || hdr[2] < 224 || (samplePixel & 0x00FFFFFFu) == 0) {
        std::cerr << "FAIL: Frame counter did not advance or PPU output was black\n";
        return 1;
    }

    // Test xBRZ 2x CPU filter dynamically
    auto gfx = config.GetGraphicsSettings();
    gfx.cpuFilter = "xbrz_2x";
    config.SetGraphicsSettings(gfx);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    emulator.CopyLatestFrameBuffer(frameBuf);
    hdr = reinterpret_cast<const uint32_t*>(frameBuf.data());
    std::cout << "      xBRZ 2x Filter Dimensions: " << hdr[1] << "x" << hdr[2] << "\n";
    gfx.cpuFilter = "none";
    config.SetGraphicsSettings(gfx);

    std::cout << "[5/5] Testing Save State & Load State (Slot 1)...\n";
    if (!emulator.SaveStateSlot(1) || !emulator.LoadStateSlot(1)) {
        std::cerr << "FAIL: SaveStateSlot(1) / LoadStateSlot(1) failed\n";
        return 1;
    }
    std::cout << "      Save State & Load State Slot 1 succeeded!\n";

    emulator.StopAndCloseRom();
    input.Shutdown();
    emulator.Shutdown();
    boxart.Shutdown();

    std::cout << "=== ALL SELF-TESTS PASSED SUCCESSFULLY ===\n";
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int) {
    std::filesystem::path rootDir = ResolveAppRootDirectory();

    if (lpCmdLine && std::string(lpCmdLine).find(kSelfTestFlag) != std::string::npos) {
        return RunSelfTest(rootDir);
    }

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

int main(int argc, char** argv) {
    std::filesystem::path rootDir = ResolveAppRootDirectory();
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == kSelfTestFlag) {
            return RunSelfTest(rootDir);
        }
    }
    return WinMain(GetModuleHandleW(nullptr), nullptr, GetCommandLineA(), SW_SHOWDEFAULT);
}

