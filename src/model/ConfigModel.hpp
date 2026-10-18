#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <filesystem>
#include <mutex>
#include <json.hpp>

namespace snex9x::model {

struct GraphicsSettings {
    std::string aspectRatio = "4:3";        // "4:3", "8:7", "stretch"
    bool integerScaling = false;
    bool bilinearFiltering = false;
    std::string cpuFilter = "none";         // "none", "ntsc_composite", "ntsc_svideo", "ntsc_rgb", "epx", "2xsai", "super2xsai", "hq2x", "xbrz_2x"
    bool crtShader = false;
    bool overscanCrop = true;
    bool vsync = true;
    bool showFps = false;
    bool fullscreen = false;
};

struct EmulationSettings {
    std::string region = "auto";            // "auto", "ntsc", "pal"
    int speedMultiplier = 1;                // 1, 2, 4
    int superFxOverclock = 100;             // 100, 150, 200, 400
    bool reduceSpriteFlicker = false;
    int volume = 85;                        // 0..100
    bool mute = false;
    std::string audioInterpolation = "gaussian"; // "gaussian", "linear", "cubic", "sinc", "none"
    bool autoSaveSram = true;
    std::string multitapMode = "auto";      // "auto", "enabled", "disabled"
    int activeStateSlot = 1;                // 1..10
    std::string romFolder = "";
};

struct PlayerBinding {
    int playerIndex = 1;                    // 1..4
    std::string deviceType = "none";        // "keyboard", "gamepad", "none"
    std::string deviceId = "";              // "keyboard", "gamepad_0", etc.
    std::string deviceName = "Unassigned";
    bool rumbleEnabled = true;
    float deadzone = 0.20f;
    std::map<std::string, std::string> buttons; // SNES button -> key/button code
};

struct GameDbEntry {
    std::string id;
    std::string name;
    std::string noIntroName;
    std::string year;
    std::string description;
    std::string developer;
    std::string publisher;
    std::string genre;
    std::string players;
    std::string region;
    std::string serial;
    std::string crc32;
    std::string boxartFilename;
    std::string hardware;
};

class ConfigModel {
private:
    mutable std::mutex m_mutex;
    std::filesystem::path m_rootDir;
    std::filesystem::path m_configDir;
    std::filesystem::path m_boxartsDir;
    std::filesystem::path m_savesDir;
    std::filesystem::path m_uiDir;

    GraphicsSettings m_graphics;
    EmulationSettings m_emulation;
    std::vector<PlayerBinding> m_bindings; // size 4

    std::vector<GameDbEntry> m_gamesDb;
    std::unordered_map<std::string, size_t> m_crcIndex;
    std::unordered_map<std::string, size_t> m_noIntroIndex;
    std::unordered_map<std::string, size_t> m_cleanNameIndex;

public:
    ConfigModel();
    ~ConfigModel() = default;

    bool Initialize(const std::filesystem::path& appRootDir);

    bool LoadSettings();
    bool SaveSettings();

    bool LoadControls();
    bool SaveControls();

    bool LoadGamesDatabase();

    const std::filesystem::path& GetRootDir() const { return m_rootDir; }
    const std::filesystem::path& GetConfigDir() const { return m_configDir; }
    const std::filesystem::path& GetBoxartsDir() const { return m_boxartsDir; }
    const std::filesystem::path& GetSavesDir() const { return m_savesDir; }
    const std::filesystem::path& GetUiDir() const { return m_uiDir; }

    GraphicsSettings GetGraphicsSettings() const;
    void SetGraphicsSettings(const GraphicsSettings& gfx);

    EmulationSettings GetEmulationSettings() const;
    void SetEmulationSettings(const EmulationSettings& emu);

    std::vector<PlayerBinding> GetPlayerBindings() const;
    PlayerBinding GetPlayerBinding(int playerIndex) const;
    void SetPlayerBinding(int playerIndex, const PlayerBinding& binding);
    void ResetDefaultControls();

    nlohmann::json SettingsToJson() const;
    void UpdateSettingsFromJson(const nlohmann::json& j);

    nlohmann::json ControlsToJson() const;
    void UpdateControlsFromJson(const nlohmann::json& j);

    const GameDbEntry* FindGameByCrc32(const std::string& crc32) const;
    const GameDbEntry* FindGameByName(const std::string& rawFilenameOrTitle) const;
    const std::vector<GameDbEntry>& GetAllDatabaseGames() const { return m_gamesDb; }

    static std::string NormalizeLookupKey(const std::string& input);
};

} // namespace snex9x::model

