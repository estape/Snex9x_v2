#pragma once

#include "ConfigModel.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <cstdint>

struct SDL_AudioStream;

namespace snex9x::model {

// Button IDs matching Snes9x mapping in EmulatorModel
enum SnesJoypadButton : int {
    SNES_BTN_B = 0,
    SNES_BTN_Y = 1,
    SNES_BTN_SELECT = 2,
    SNES_BTN_START = 3,
    SNES_BTN_UP = 4,
    SNES_BTN_DOWN = 5,
    SNES_BTN_LEFT = 6,
    SNES_BTN_RIGHT = 7,
    SNES_BTN_A = 8,
    SNES_BTN_X = 9,
    SNES_BTN_L = 10,
    SNES_BTN_R = 11
};

class EmulatorModel {
public:
    static constexpr size_t kVideoHeaderBytes = 16; // [frameCounter, width, height, flags]
    static constexpr size_t kMaxFrameWidth = 1024;
    static constexpr size_t kMaxFrameHeight = 960;
    static constexpr size_t kMaxVideoBufferBytes = kVideoHeaderBytes + (kMaxFrameWidth * kMaxFrameHeight * 4);

    using InputPollCallback = std::function<void()>;
    using StatusMessageCallback = std::function<void(const std::string& message)>;

private:
    static EmulatorModel* s_instance;

    ConfigModel& m_config;
    mutable std::mutex m_coreMutex;
    mutable std::mutex m_frameMutex;

    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};
    std::atomic<uint32_t> m_frameCounter{0};
    std::atomic<double> m_currentFps{60.0};

    std::thread m_emuThread;
    std::string m_activeGameId;
    std::filesystem::path m_activeRomPath;

    uint8_t* m_sharedVideoPtr = nullptr;
    std::vector<uint8_t> m_localVideoBuffer;
    std::vector<uint16_t> m_filterScratch16;
    std::vector<uint32_t> m_filterScratch32;
    std::vector<int16_t> m_audioMixBuffer;

    SDL_AudioStream* m_audioStream = nullptr;
    InputPollCallback m_inputPollCallback;
    StatusMessageCallback m_statusCallback;

public:
    explicit EmulatorModel(ConfigModel& configModel);
    ~EmulatorModel();

    bool Initialize();
    void Shutdown();

    // Attaches external shared memory buffer (from WebView2 CreateSharedBuffer)
    void SetSharedVideoBufferPtr(uint8_t* sharedPtr);

    // Copies the latest frame (16-byte header + RGBA8 payload) for custom scheme fallback
    void CopyLatestFrameBuffer(std::vector<uint8_t>& outBuffer) const;

    void SetInputPollCallback(InputPollCallback cb) { m_inputPollCallback = std::move(cb); }
    void SetStatusMessageCallback(StatusMessageCallback cb) { m_statusCallback = std::move(cb); }

    bool LoadAndStartRom(const std::filesystem::path& romPath, const std::string& gameId);
    void StopAndCloseRom();

    void SetPaused(bool paused);
    bool IsPaused() const { return m_paused.load(); }
    bool IsRunning() const { return m_running.load(); }
    std::string GetActiveGameId() const;
    std::filesystem::path GetActiveRomPath() const;
    double GetCurrentFps() const { return m_currentFps.load(); }

    void SoftReset();
    void HardReset();

    bool SaveStateSlot(int slot);
    bool LoadStateSlot(int slot);
    bool SaveStateFile(const std::filesystem::path& filePath);
    bool LoadStateFile(const std::filesystem::path& filePath);

    bool SaveSram();
    bool LoadSram();

    void ApplyRuntimeSettings();
    void ConfigureControllers(bool enableMultitap);
    void ReportButtonState(int playerIndex1Based, int snesButtonId, bool pressed);

    // Called from Snes9x core callbacks
    void OnVideoFrameReady(const uint16_t* srcRgb565, int width, int height, int pitchBytes);
    void OnSyncAudioSamples();
    void OnAutoSaveSram();

    static EmulatorModel* Instance() { return s_instance; }

private:
    void EmulationLoop();
    void InitSnes9xCore();
    void MapSnesButtons();
    std::filesystem::path GetSramPath() const;
    std::filesystem::path GetSlotStatePath(int slot) const;
};

} // namespace snex9x::model

