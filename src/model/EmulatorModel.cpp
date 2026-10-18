#include "EmulatorModel.hpp"

#include "snes9x.h"
#include "memmap.h"
#include "srtc.h"
#include "apu/apu.h"
#include "gfx.h"
#include "snapshot.h"
#include "controls.h"
#include "cheats.h"
#include "movie.h"
#include "display.h"
#include "conffile.h"
#include "crosshairs.h"
#include "filter/snes_ntsc.h"
#include "filter/epx.h"
#include "filter/2xsai.h"
#include "filter/hq2x.h"
#include "filter/xbrz.h"

#include <SDL3/SDL.h>
#include <chrono>
#include <future>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <cstring>

#define MAKE_SNES_BTN(pad, btn) (((pad) << 4) | (btn))
#define MAP_SNES_BTN(id, name) S9xMapButton((id), S9xGetCommandT((name)), false)

namespace snex9x::model {

EmulatorModel* EmulatorModel::s_instance = nullptr;

static uint32_t s_rgb565ToRgbaLut[65536];
static uint32_t s_rgb565ToArgbLut[65536];
static bool s_lutInitialized = false;
static snes_ntsc_t* s_snesNtsc = nullptr;
static std::string s_currentNtscMode = "";
static int s_ntscBurstPhase = 0;

static void InitColorLuts() {
    if (s_lutInitialized) return;
    for (uint32_t c = 0; c < 65536; ++c) {
        uint32_t r5 = (c >> 11) & 0x1Fu;
        uint32_t g6 = (c >> 5) & 0x3Fu;
        uint32_t b5 = c & 0x1Fu;

        uint32_t r8 = (r5 << 3) | (r5 >> 2);
        uint32_t g8 = (g6 << 2) | (g6 >> 4);
        uint32_t b8 = (b5 << 3) | (b5 >> 2);

        // Little-endian RGBA8888 (R at byte 0, G at byte 1, B at byte 2, A=255 at byte 3)
        s_rgb565ToRgbaLut[c] = r8 | (g8 << 8) | (b8 << 16) | 0xFF000000u;
        // ARGB8888 for xBRZ (B at byte 0, G at byte 1, R at byte 2, A=255 at byte 3)
        s_rgb565ToArgbLut[c] = b8 | (g8 << 8) | (r8 << 16) | 0xFF000000u;
    }
    s_lutInitialized = true;
}

static inline uint32_t ArgbToRgba(uint32_t argb) {
    uint32_t b = argb & 0xFFu;
    uint32_t g = (argb >> 8) & 0xFFu;
    uint32_t r = (argb >> 16) & 0xFFu;
    return r | (g << 8) | (b << 16) | 0xFF000000u;
}

EmulatorModel::EmulatorModel(ConfigModel& configModel)
    : m_config(configModel) {
    s_instance = this;
    InitColorLuts();
    m_localVideoBuffer.resize(kMaxVideoBufferBytes, 0);
    m_filterScratch16.resize(kMaxFrameWidth * kMaxFrameHeight, 0);
    m_filterScratch32.resize(kMaxFrameWidth * kMaxFrameHeight, 0);
    m_audioMixBuffer.resize(8192, 0);
}

EmulatorModel::~EmulatorModel() {
    Shutdown();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool EmulatorModel::Initialize() {
    if (m_initialized.load()) return true;

    // Initialize SDL3 Audio subsystem
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            std::cerr << "[EmulatorModel] SDL_InitSubSystem(SDL_INIT_AUDIO) failed: " << SDL_GetError() << "\n";
        }
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = 32040;
    m_audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (m_audioStream) {
        SDL_ResumeAudioStreamDevice(m_audioStream);
    }

    InitSnes9xCore();
    ApplyRuntimeSettings();
    m_initialized = true;
    return true;
}

void EmulatorModel::Shutdown() {
    StopAndCloseRom();

    if (m_initialized.load()) {
        std::lock_guard<std::mutex> lock(m_coreMutex);
        S9xBlit2xSaIFilterDeinit();
        S9xBlitHQ2xFilterDeinit();
        S9xDeinitAPU();
        Memory.Deinit();
        S9xGraphicsDeinit();
        S9xUnmapAllControls();
        if (s_snesNtsc) {
            delete s_snesNtsc;
            s_snesNtsc = nullptr;
        }
        m_initialized = false;
    }

    if (m_audioStream) {
        SDL_DestroyAudioStream(m_audioStream);
        m_audioStream = nullptr;
    }
}

void EmulatorModel::InitSnes9xCore() {
    std::lock_guard<std::mutex> lock(m_coreMutex);

    std::memset(&Settings, 0, sizeof(Settings));
    Settings.MouseMaster = TRUE;
    Settings.SuperScopeMaster = TRUE;
    Settings.JustifierMaster = TRUE;
    Settings.MultiPlayer5Master = TRUE;
    Settings.MacsRifleMaster = TRUE;
    Settings.FrameTimePAL = 20000;
    Settings.FrameTimeNTSC = 16667;
    Settings.SixteenBitSound = TRUE;
    Settings.Stereo = TRUE;
    Settings.SoundPlaybackRate = 32040;
    Settings.SoundInputRate = 32040;
    Settings.Transparency = TRUE;
    Settings.AutoDisplayMessages = FALSE;
    Settings.InitialInfoStringTimeout = 120;
    Settings.HDMATimingHack = 100;
    Settings.BlockInvalidVRAMAccessMaster = TRUE;
    Settings.SeparateEchoBuffer = FALSE;
    Settings.AutoSaveDelay = 1;
    Settings.DontSaveOopsSnapshot = TRUE;
    Settings.SuperFXClockMultiplier = 100;
    Settings.OneClockCycle = 6;
    Settings.OneSlowClockCycle = 8;
    Settings.TwoClockCycles = 12;
    Settings.MaxSpriteTilesPerLine = 34;
    Settings.InterpolationMethod = DSP_INTERPOLATION_GAUSSIAN;

    CPU.Flags = 0;

    if (!Memory.Init() || !S9xInitAPU()) {
        std::cerr << "[EmulatorModel] Failed to initialize Snes9x Memory or APU!\n";
        return;
    }

    S9xInitSound(32);
    S9xSetSoundMute(FALSE);
    S9xSetSamplesAvailableCallback(nullptr, nullptr);

    S9xGraphicsInit();
    S9xBlit2xSaIFilterInit();
    S9xBlitHQ2xFilterInit();

    if (!s_snesNtsc) {
        s_snesNtsc = new snes_ntsc_t;
        snes_ntsc_init(s_snesNtsc, &snes_ntsc_composite);
        s_currentNtscMode = "ntsc_composite";
    }

    S9xUnmapAllControls();
    MapSnesButtons();
    ConfigureControllers(false);
}

void EmulatorModel::MapSnesButtons() {
    // Map Joypad 1 to 4 (and 5) buttons to Snes9x command system
    for (int pad = 1; pad <= 5; ++pad) {
        std::string prefix = "Joypad" + std::to_string(pad) + " ";
        MAP_SNES_BTN(MAKE_SNES_BTN(pad, SNES_BTN_A), (prefix + "A").c_str());
        MAP_SNES_BTN(MAKE_SNES_BTN(pad, SNES_BTN_B), (prefix + "B").c_str());
        MAP_SNES_BTN(MAKE_SNES_BTN(pad, SNES_BTN_X), (prefix + "X").c_str());
        MAP_SNES_BTN(MAKE_SNES_BTN(pad, SNES_BTN_Y), (prefix + "Y").c_str());
        MAP_SNES_BTN(MAKE_SNES_BTN(pad, SNES_BTN_L), (prefix + "L").c_str());
        MAP_SNES_BTN(MAKE_SNES_BTN(pad, SNES_BTN_R), (prefix + "R").c_str());
        MAP_SNES_BTN(MAKE_SNES_BTN(pad, SNES_BTN_START), (prefix + "Start").c_str());
        MAP_SNES_BTN(MAKE_SNES_BTN(pad, SNES_BTN_SELECT), (prefix + "Select").c_str());
        MAP_SNES_BTN(MAKE_SNES_BTN(pad, SNES_BTN_UP), (prefix + "Up").c_str());
        MAP_SNES_BTN(MAKE_SNES_BTN(pad, SNES_BTN_DOWN), (prefix + "Down").c_str());
        MAP_SNES_BTN(MAKE_SNES_BTN(pad, SNES_BTN_LEFT), (prefix + "Left").c_str());
        MAP_SNES_BTN(MAKE_SNES_BTN(pad, SNES_BTN_RIGHT), (prefix + "Right").c_str());
    }
}

void EmulatorModel::ConfigureControllers(bool enableMultitap) {
    S9xSetController(0, CTL_JOYPAD, 0, 0, 0, 0);
    if (enableMultitap) {
        // Super Multitap (MP5) on Port 2 connects Joypads 2, 3, and 4 (indices 1, 2, 3)
        S9xSetController(1, CTL_MP5, 1, 2, 3, -1);
    } else {
        S9xSetController(1, CTL_JOYPAD, 1, 0, 0, 0);
    }
    S9xControlsSoftReset();
}

void EmulatorModel::ReportButtonState(int playerIndex1Based, int snesButtonId, bool pressed) {
    if (playerIndex1Based < 1 || playerIndex1Based > 4) return;
    S9xReportButton(MAKE_SNES_BTN(playerIndex1Based, snesButtonId), pressed);
}

void EmulatorModel::SetSharedVideoBufferPtr(uint8_t* sharedPtr) {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    m_sharedVideoPtr = sharedPtr;
    if (m_sharedVideoPtr && !m_localVideoBuffer.empty()) {
        std::memcpy(m_sharedVideoPtr, m_localVideoBuffer.data(), m_localVideoBuffer.size());
    }
}

void EmulatorModel::CopyLatestFrameBuffer(std::vector<uint8_t>& outBuffer) const {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    uint32_t w = 256, h = 224;
    if (m_localVideoBuffer.size() >= kVideoHeaderBytes) {
        const uint32_t* hdr = reinterpret_cast<const uint32_t*>(m_localVideoBuffer.data());
        w = std::clamp<uint32_t>(hdr[1], 1, kMaxFrameWidth);
        h = std::clamp<uint32_t>(hdr[2], 1, kMaxFrameHeight);
    }
    size_t totalBytes = kVideoHeaderBytes + static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    totalBytes = std::min(totalBytes, m_localVideoBuffer.size());
    outBuffer.assign(m_localVideoBuffer.begin(), m_localVideoBuffer.begin() + totalBytes);
}

void EmulatorModel::ApplyRuntimeSettings() {
    auto emu = m_config.GetEmulationSettings();
    auto gfx = m_config.GetGraphicsSettings();

    if (m_audioStream) {
        float gain = emu.mute ? 0.0f : (static_cast<float>(std::clamp(emu.volume, 0, 100)) / 100.0f);
        SDL_SetAudioStreamGain(m_audioStream, gain);
    }

    Settings.SuperFXClockMultiplier = std::clamp(emu.superFxOverclock, 50, 400);
    Settings.MaxSpriteTilesPerLine = emu.reduceSpriteFlicker ? 128 : 34;

    if (emu.region == "ntsc") {
        Settings.ForceNTSC = TRUE;
        Settings.ForcePAL = FALSE;
    } else if (emu.region == "pal") {
        Settings.ForceNTSC = FALSE;
        Settings.ForcePAL = TRUE;
    } else {
        Settings.ForceNTSC = FALSE;
        Settings.ForcePAL = FALSE;
    }

    if (emu.audioInterpolation == "linear") {
        Settings.InterpolationMethod = DSP_INTERPOLATION_LINEAR;
    } else if (emu.audioInterpolation == "cubic") {
        Settings.InterpolationMethod = DSP_INTERPOLATION_CUBIC;
    } else if (emu.audioInterpolation == "sinc") {
        Settings.InterpolationMethod = DSP_INTERPOLATION_SINC;
    } else if (emu.audioInterpolation == "none") {
        Settings.InterpolationMethod = DSP_INTERPOLATION_NONE;
    } else {
        Settings.InterpolationMethod = DSP_INTERPOLATION_GAUSSIAN;
    }

    // Check Multitap mode
    auto bindings = m_config.GetPlayerBindings();
    bool p3OrP4Active = (bindings.size() >= 4) &&
                        (bindings[2].deviceType != "none" || bindings[3].deviceType != "none");
    bool enableMultitap = (emu.multitapMode == "enabled") ||
                          (emu.multitapMode == "auto" && p3OrP4Active);
    ConfigureControllers(enableMultitap);

    // Update Blargg NTSC preset if selected
    if (s_snesNtsc && gfx.cpuFilter.rfind("ntsc_", 0) == 0 && gfx.cpuFilter != s_currentNtscMode) {
        snes_ntsc_setup_t setup = snes_ntsc_composite;
        if (gfx.cpuFilter == "ntsc_svideo") setup = snes_ntsc_svideo;
        else if (gfx.cpuFilter == "ntsc_rgb") setup = snes_ntsc_rgb;
        snes_ntsc_init(s_snesNtsc, &setup);
        s_currentNtscMode = gfx.cpuFilter;
    }
}

bool EmulatorModel::LoadAndStartRom(const std::filesystem::path& romPath, const std::string& gameId) {
    if (!m_initialized.load()) {
        Initialize();
    }

    StopAndCloseRom();

    std::error_code ec;
    if (!std::filesystem::exists(romPath, ec)) {
        if (m_statusCallback) m_statusCallback("ROM file not found: " + romPath.u8string());
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_coreMutex);
        ApplyRuntimeSettings();

        // Read file into memory so Unicode paths on Windows work reliably
        std::ifstream f(romPath, std::ios::binary | std::ios::ate);
        if (!f.is_open()) return false;
        std::streamsize fsize = f.tellg();
        if (fsize <= 0) return false;
        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> romData(static_cast<size_t>(fsize));
        if (!f.read(reinterpret_cast<char*>(romData.data()), fsize)) return false;

        // Strip 512-byte SMC copier header if present
        const uint8_t* ptr = romData.data();
        size_t size = romData.size();
        size_t calcSize = (size / 0x2000) * 0x2000;
        if (size - calcSize == 512) {
            ptr += 512;
            size -= 512;
        }

        std::string romName = romPath.stem().u8string();
        if (!Memory.LoadROMMem(ptr, static_cast<uint32>(size), romName.c_str())) {
            if (m_statusCallback) m_statusCallback("Failed to load SNES ROM: " + romPath.filename().u8string());
            return false;
        }

        Memory.ClearSRAM();
        m_activeRomPath = romPath;
        m_activeGameId = gameId;
        LoadSram();
    }

    m_paused = false;
    m_running = true;
    m_emuThread = std::thread(&EmulatorModel::EmulationLoop, this);
    return true;
}

void EmulatorModel::StopAndCloseRom() {
    if (m_running.load()) {
        m_running = false;
        if (m_emuThread.joinable()) {
            m_emuThread.join();
        }
        if (m_config.GetEmulationSettings().autoSaveSram) {
            SaveSram();
        }
    }
    if (m_audioStream) {
        SDL_ClearAudioStream(m_audioStream);
    }
    m_paused = false;
}

void EmulatorModel::SetPaused(bool paused) {
    m_paused = paused;
    if (paused && m_audioStream) {
        SDL_ClearAudioStream(m_audioStream);
    }
}

std::string EmulatorModel::GetActiveGameId() const {
    return m_activeGameId;
}

std::filesystem::path EmulatorModel::GetActiveRomPath() const {
    return m_activeRomPath;
}

void EmulatorModel::SoftReset() {
    if (!m_running.load()) return;
    std::lock_guard<std::mutex> lock(m_coreMutex);
    S9xSoftReset();
}

void EmulatorModel::HardReset() {
    if (!m_running.load()) return;
    std::lock_guard<std::mutex> lock(m_coreMutex);
    S9xReset();
}

std::filesystem::path EmulatorModel::GetSramPath() const {
    if (m_activeRomPath.empty()) return {};
    return m_config.GetSavesDir() / (m_activeRomPath.stem().u8string() + ".srm");
}

std::filesystem::path EmulatorModel::GetSlotStatePath(int slot) const {
    if (m_activeRomPath.empty()) return {};
    int clampedSlot = std::clamp(slot, 1, 10);
    std::ostringstream ext;
    ext << ".s" << std::setw(2) << std::setfill('0') << clampedSlot;
    return m_config.GetSavesDir() / (m_activeRomPath.stem().u8string() + ext.str());
}

bool EmulatorModel::SaveSram() {
    std::filesystem::path sramPath = GetSramPath();
    if (sramPath.empty()) return false;
    size_t sramSize = Memory.SRAMSize ? (static_cast<size_t>(1) << (Memory.SRAMSize + 3)) * 128 : 0;
    if (sramSize > 0x20000) sramSize = 0x20000;
    if (sramSize == 0 || !Memory.SRAM) return false;

    std::ofstream out(sramPath, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(Memory.SRAM), static_cast<std::streamsize>(sramSize));
    return true;
}

bool EmulatorModel::LoadSram() {
    std::filesystem::path sramPath = GetSramPath();
    std::error_code ec;
    if (sramPath.empty() || !std::filesystem::exists(sramPath, ec)) return false;
    size_t sramSize = Memory.SRAMSize ? (static_cast<size_t>(1) << (Memory.SRAMSize + 3)) * 128 : 0;
    if (sramSize > 0x20000) sramSize = 0x20000;
    if (sramSize == 0 || !Memory.SRAM) return false;

    std::ifstream in(sramPath, std::ios::binary);
    if (!in.is_open()) return false;
    in.read(reinterpret_cast<char*>(Memory.SRAM), static_cast<std::streamsize>(sramSize));
    return true;
}

bool EmulatorModel::SaveStateSlot(int slot) {
    return SaveStateFile(GetSlotStatePath(slot));
}

bool EmulatorModel::LoadStateSlot(int slot) {
    return LoadStateFile(GetSlotStatePath(slot));
}

bool EmulatorModel::SaveStateFile(const std::filesystem::path& filePath) {
    if (!m_running.load() || filePath.empty()) return false;
    std::lock_guard<std::mutex> lock(m_coreMutex);
    uint32_t freezeSize = S9xFreezeSize();
    if (freezeSize == 0) return false;
    std::vector<uint8_t> buf(freezeSize);
    if (S9xFreezeGameMem(buf.data(), freezeSize) == FALSE) {
        return false;
    }
    std::ofstream out(filePath, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    return true;
}

bool EmulatorModel::LoadStateFile(const std::filesystem::path& filePath) {
    std::error_code ec;
    if (!m_running.load() || filePath.empty() || !std::filesystem::exists(filePath, ec)) return false;
    std::lock_guard<std::mutex> lock(m_coreMutex);
    std::ifstream in(filePath, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return false;
    std::streamsize size = in.tellg();
    if (size <= 0) return false;
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char*>(buf.data()), size)) return false;
    return S9xUnfreezeGameMem(buf.data(), static_cast<uint32>(size)) == SUCCESS;
}

void EmulatorModel::EmulationLoop() {
    using clock = std::chrono::steady_clock;
    auto nextFrameTime = clock::now();
    auto fpsTimer = clock::now();
    int framesSinceLastMeasure = 0;

    while (m_running.load()) {
        if (m_paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            nextFrameTime = clock::now();
            continue;
        }

        if (m_inputPollCallback) {
            m_inputPollCallback();
        }

        auto emu = m_config.GetEmulationSettings();
        int speedMult = std::clamp(emu.speedMultiplier, 1, 8);
        double baseFps = Settings.PAL ? (21281370.0 / 425568.0) : (21477272.0 / 357366.0);
        double targetFps = baseFps * static_cast<double>(speedMult);
        auto frameDuration = std::chrono::microseconds(static_cast<int64_t>(1000000.0 / targetFps));

        {
            std::lock_guard<std::mutex> lock(m_coreMutex);
            IPPU.RenderThisFrame = TRUE;
            S9xMainLoop();
        }

        ++framesSinceLastMeasure;
        auto now = clock::now();
        double elapsedSec = std::chrono::duration<double>(now - fpsTimer).count();
        if (elapsedSec >= 0.5) {
            m_currentFps.store(static_cast<double>(framesSinceLastMeasure) / elapsedSec);
            framesSinceLastMeasure = 0;
            fpsTimer = now;
        }

        nextFrameTime += frameDuration;
        if (now < nextFrameTime) {
            std::this_thread::sleep_until(nextFrameTime);
        } else if (now - nextFrameTime > std::chrono::milliseconds(100)) {
            nextFrameTime = now;
        }
    }
}

void EmulatorModel::OnVideoFrameReady(const uint16_t* srcRgb565, int width, int height, int pitchBytes) {
    if (!srcRgb565 || width <= 0 || height <= 0) return;

    auto gfx = m_config.GetGraphicsSettings();
    int overscanOffsetLines = 0;
    if (gfx.overscanCrop) {
        if (height > SNES_HEIGHT * 2) {
            overscanOffsetLines = 14;
            height = SNES_HEIGHT * 2;
        } else if (height > SNES_HEIGHT && height != SNES_HEIGHT * 2) {
            overscanOffsetLines = 7;
            height = SNES_HEIGHT;
        }
    }

    const uint16_t* activeSrc = srcRgb565 + (pitchBytes / 2) * overscanOffsetLines;
    int outWidth = width;
    int outHeight = height;

    const std::string& filter = gfx.cpuFilter;
    const uint16_t* finalRgb565 = activeSrc;
    int finalPitchBytes = pitchBytes;
    bool usedXbrz32 = false;

    if (s_snesNtsc && filter.rfind("ntsc_", 0) == 0 && height <= 480) {
        s_ntscBurstPhase = (s_ntscBurstPhase + 1) % 3;
        outWidth = SNES_NTSC_OUT_WIDTH(256);
        int dstPitchBytes = outWidth * 2;
        if (width == 512) {
            snes_ntsc_blit_hires(s_snesNtsc, activeSrc, pitchBytes / 2, s_ntscBurstPhase, width, height, m_filterScratch16.data(), dstPitchBytes);
        } else {
            snes_ntsc_blit(s_snesNtsc, activeSrc, pitchBytes / 2, s_ntscBurstPhase, width, height, m_filterScratch16.data(), dstPitchBytes);
        }
        finalRgb565 = m_filterScratch16.data();
        finalPitchBytes = dstPitchBytes;
    } else if ((filter == "epx" || filter == "2xsai" || filter == "super2xsai" || filter == "hq2x") &&
               width <= 256 && height <= 240) {
        outWidth = width * 2;
        outHeight = height * 2;
        int dstPitchBytes = outWidth * 2;
        uint8_t* srcBytePtr = reinterpret_cast<uint8_t*>(const_cast<uint16_t*>(activeSrc));
        uint8_t* dstBytePtr = reinterpret_cast<uint8_t*>(m_filterScratch16.data());

        if (filter == "epx") {
            EPX_16(srcBytePtr, pitchBytes, dstBytePtr, dstPitchBytes, width, height);
        } else if (filter == "2xsai") {
            _2xSaI(srcBytePtr, pitchBytes, dstBytePtr, dstPitchBytes, width, height);
        } else if (filter == "super2xsai") {
            Super2xSaI(srcBytePtr, pitchBytes, dstBytePtr, dstPitchBytes, width, height);
        } else if (filter == "hq2x") {
            HQ2X_16(srcBytePtr, pitchBytes, dstBytePtr, dstPitchBytes, width, height);
        }
        finalRgb565 = m_filterScratch16.data();
        finalPitchBytes = dstPitchBytes;
    } else if (filter == "xbrz_2x" && width <= 256 && height <= 240) {
        // Convert RGB565 -> ARGB8888 into first half of m_filterScratch32, then xbrz::scale 2x in parallel slices
        uint32_t* src32 = m_filterScratch32.data();
        int srcPixels = width * height;
        int midY = height / 2;

        auto lutFut = std::async(std::launch::async, [=, &src32, &activeSrc]() {
            for (int y = 0; y < midY; ++y) {
                const uint16_t* row = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(activeSrc) + y * pitchBytes);
                uint32_t* dstRow = src32 + y * width;
                for (int x = 0; x < width; ++x) {
                    dstRow[x] = s_rgb565ToArgbLut[row[x]];
                }
            }
        });
        for (int y = midY; y < height; ++y) {
            const uint16_t* row = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(activeSrc) + y * pitchBytes);
            uint32_t* dstRow = src32 + y * width;
            for (int x = 0; x < width; ++x) {
                dstRow[x] = s_rgb565ToArgbLut[row[x]];
            }
        }
        lutFut.get();

        outWidth = width * 2;
        outHeight = height * 2;
        uint32_t* trg32 = m_filterScratch32.data() + srcPixels;

        // Process top and bottom slices in parallel threads (supported by xbrz::scale)
        auto scaleFut = std::async(std::launch::async, [=, &src32, &trg32]() {
            xbrz::scale(2, src32, trg32, width, height, xbrz::ColorFormat::RGB, xbrz::ScalerCfg(), 0, midY);
        });
        xbrz::scale(2, src32, trg32, width, height, xbrz::ColorFormat::RGB, xbrz::ScalerCfg(), midY, height);
        scaleFut.get();

        usedXbrz32 = true;
    }

    outWidth = std::clamp<int>(outWidth, 1, static_cast<int>(kMaxFrameWidth));
    outHeight = std::clamp<int>(outHeight, 1, static_cast<int>(kMaxFrameHeight));

    uint32_t nextFrameNum = m_frameCounter.fetch_add(1) + 1;
    uint32_t fpsFixed = static_cast<uint32_t>(m_currentFps.load() * 10.0);
    uint32_t flags = (m_running.load() ? 1u : 0u) | (m_paused.load() ? 2u : 0u) | ((fpsFixed & 0xFFFFu) << 16);

    std::lock_guard<std::mutex> lock(m_frameMutex);
    uint32_t* localHeader = reinterpret_cast<uint32_t*>(m_localVideoBuffer.data());
    localHeader[1] = static_cast<uint32_t>(outWidth);
    localHeader[2] = static_cast<uint32_t>(outHeight);
    localHeader[3] = flags;

    uint32_t* localPixels = reinterpret_cast<uint32_t*>(m_localVideoBuffer.data() + kVideoHeaderBytes);
    if (usedXbrz32) {
        const uint32_t* trg32 = m_filterScratch32.data() + (width * height);
        int totalPixels = outWidth * outHeight;
        int halfPixels = totalPixels / 2;
        auto convFut = std::async(std::launch::async, [=, &localPixels, &trg32]() {
            for (int i = 0; i < halfPixels; ++i) {
                localPixels[i] = ArgbToRgba(trg32[i]);
            }
        });
        for (int i = halfPixels; i < totalPixels; ++i) {
            localPixels[i] = ArgbToRgba(trg32[i]);
        }
        convFut.get();
    } else {
        for (int y = 0; y < outHeight; ++y) {
            const uint16_t* srcRow = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(finalRgb565) + y * finalPitchBytes);
            uint32_t* dstRow = localPixels + y * outWidth;
            for (int x = 0; x < outWidth; ++x) {
                dstRow[x] = s_rgb565ToRgbaLut[srcRow[x]];
            }
        }
    }
    localHeader[0] = nextFrameNum;

    if (m_sharedVideoPtr) {
        size_t copyBytes = kVideoHeaderBytes + static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4;
        std::memcpy(m_sharedVideoPtr + 4, m_localVideoBuffer.data() + 4, copyBytes - 4);
        // Write frameCounter last so reader sees coherent dimensions & pixels
        reinterpret_cast<volatile uint32_t*>(m_sharedVideoPtr)[0] = nextFrameNum;
    }
}

void EmulatorModel::OnSyncAudioSamples() {
    auto emu = m_config.GetEmulationSettings();
    if (emu.mute || !m_audioStream) {
        S9xClearSamples();
        return;
    }

    size_t availSamples = S9xGetSampleCount();
    if (availSamples == 0) return;

    if (m_audioMixBuffer.size() < availSamples) {
        m_audioMixBuffer.resize(availSamples);
    }

    S9xMixSamples(reinterpret_cast<uint8*>(m_audioMixBuffer.data()), static_cast<int>(availSamples));

    // Avoid excessive audio latency buildup (> ~120ms queued)
    int queuedBytes = SDL_GetAudioStreamQueued(m_audioStream);
    if (queuedBytes < 32040 * 2 * 2 / 6) {
        SDL_PutAudioStreamData(m_audioStream, m_audioMixBuffer.data(), static_cast<int>(availSamples * sizeof(int16_t)));
    }
}

void EmulatorModel::OnAutoSaveSram() {
    if (m_config.GetEmulationSettings().autoSaveSram) {
        SaveSram();
    }
}

} // namespace snex9x::model

// ============================================================================
// Snes9x Required Host Callbacks
// ============================================================================

bool8 S9xInitUpdate(void) {
    return TRUE;
}

bool8 S9xDeinitUpdate(int width, int height) {
    if (auto* emu = snex9x::model::EmulatorModel::Instance()) {
        emu->OnVideoFrameReady(GFX.Screen, width, height, GFX.Pitch);
    }
    return TRUE;
}

bool8 S9xContinueUpdate(int width, int height) {
    return S9xDeinitUpdate(width, height);
}

void S9xSyncSpeed(void) {
    if (auto* emu = snex9x::model::EmulatorModel::Instance()) {
        emu->OnSyncAudioSamples();
    } else {
        S9xClearSamples();
    }
}

void S9xAutoSaveSRAM(void) {
    if (auto* emu = snex9x::model::EmulatorModel::Instance()) {
        emu->OnAutoSaveSram();
    }
}

bool8 S9xOpenSoundDevice(void) {
    return TRUE;
}

void S9xParsePortConfig(ConfigFile&, int) {}
const char* S9xStringInput(const char* in) { return in; }

std::string S9xGetDirectory(enum s9x_getdirtype) {
    if (auto* emu = snex9x::model::EmulatorModel::Instance()) {
        auto p = emu->GetActiveRomPath();
        if (!p.empty()) return p.parent_path().u8string();
    }
    return ".";
}

std::string S9xGetFilename(const char* ex, enum s9x_getdirtype) {
    if (auto* emu = snex9x::model::EmulatorModel::Instance()) {
        auto p = emu->GetActiveRomPath();
        if (!p.empty()) return (p.parent_path() / (p.stem().u8string() + (ex ? ex : ""))).u8string();
    }
    return std::string("rom") + (ex ? ex : "");
}

std::string S9xGetFilenameInc(std::string, enum s9x_getdirtype) {
    return "";
}

const char* S9xBasename(const char* in) {
    return in;
}

void S9xInitInputDevices(void) {}
void S9xHandlePortCommand(s9xcommand_t, int16, int16) {}
bool S9xPollButton(uint32, bool*) { return false; }
bool S9xPollAxis(uint32, int16*) { return false; }
bool S9xPollPointer(uint32, int16*, int16*) { return false; }
void S9xToggleSoundChannel(int) {}
void S9xExtraUsage(void) {}
void S9xParseArg(char**, int&, int) {}
void S9xExit(void) {}

void S9xMessage(int, int, const char* s) {
    if (s) {
        std::cout << "[Snes9x] " << s << "\n";
    }
}

bool8 S9xOpenSnapshotFile(const char* filepath, bool8 read_only, STREAM* file) {
    if (!filepath || !file) return FALSE;
    if (read_only) {
        if ((*file = OPEN_STREAM(filepath, "rb")) != 0) return TRUE;
    } else {
        if ((*file = OPEN_STREAM(filepath, "wb")) != 0) return TRUE;
    }
    return FALSE;
}

void S9xCloseSnapshotFile(STREAM file) {
    CLOSE_STREAM(file);
}

