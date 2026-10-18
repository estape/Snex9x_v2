#include "ConfigModel.hpp"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <iostream>

using json = nlohmann::json;

namespace snex9x::model {

static std::map<std::string, std::string> DefaultKeyboardButtonsP1() {
    return {
        {"Up", "ArrowUp"},
        {"Down", "ArrowDown"},
        {"Left", "ArrowLeft"},
        {"Right", "ArrowRight"},
        {"A", "KeyX"},
        {"B", "KeyZ"},
        {"X", "KeyS"},
        {"Y", "KeyA"},
        {"L", "KeyQ"},
        {"R", "KeyW"},
        {"Start", "Enter"},
        {"Select", "ShiftRight"}
    };
}

static std::map<std::string, std::string> DefaultGamepadButtons() {
    return {
        {"Up", "dpup"},
        {"Down", "dpdown"},
        {"Left", "dpleft"},
        {"Right", "dpright"},
        {"A", "east"},
        {"B", "south"},
        {"X", "north"},
        {"Y", "west"},
        {"L", "leftshoulder"},
        {"R", "rightshoulder"},
        {"Start", "start"},
        {"Select", "back"}
    };
}

ConfigModel::ConfigModel() {
    ResetDefaultControls();
}

void ConfigModel::ResetDefaultControls() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_bindings.clear();
    m_bindings.resize(4);

    // Player 1 defaults to Keyboard (also auto-polls Gamepad 0 if connected)
    m_bindings[0].playerIndex = 1;
    m_bindings[0].deviceType = "keyboard";
    m_bindings[0].deviceId = "keyboard";
    m_bindings[0].deviceName = "Keyboard (Arrows + Z/X/A/S/Q/W)";
    m_bindings[0].rumbleEnabled = true;
    m_bindings[0].deadzone = 0.20f;
    m_bindings[0].buttons = DefaultKeyboardButtonsP1();

    for (int i = 1; i < 4; ++i) {
        m_bindings[i].playerIndex = i + 1;
        m_bindings[i].deviceType = "none";
        m_bindings[i].deviceId = "";
        m_bindings[i].deviceName = "Unassigned";
        m_bindings[i].rumbleEnabled = true;
        m_bindings[i].deadzone = 0.20f;
        m_bindings[i].buttons = DefaultGamepadButtons();
    }
}

bool ConfigModel::Initialize(const std::filesystem::path& appRootDir) {
    m_rootDir = appRootDir;
    m_configDir = m_rootDir / "config";
    m_boxartsDir = m_configDir / "boxarts";
    m_savesDir = m_configDir / "saves";
    m_uiDir = m_rootDir / "ui";

    std::error_code ec;
    std::filesystem::create_directories(m_configDir, ec);
    std::filesystem::create_directories(m_boxartsDir, ec);
    std::filesystem::create_directories(m_savesDir, ec);

    LoadSettings();
    LoadControls();
    LoadGamesDatabase();

    return true;
}

bool ConfigModel::LoadSettings() {
    std::filesystem::path settingsPath = m_configDir / "settings.json";
    if (!std::filesystem::exists(settingsPath)) {
        return SaveSettings();
    }

    try {
        std::ifstream f(settingsPath);
        if (!f.is_open()) return false;
        json j = json::parse(f);
        UpdateSettingsFromJson(j);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ConfigModel] Failed to load settings.json: " << e.what() << "\n";
        return false;
    }
}

bool ConfigModel::SaveSettings() {
    std::filesystem::path settingsPath = m_configDir / "settings.json";
    try {
        json j = SettingsToJson();
        std::ofstream f(settingsPath);
        if (!f.is_open()) return false;
        f << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ConfigModel] Failed to save settings.json: " << e.what() << "\n";
        return false;
    }
}

bool ConfigModel::LoadControls() {
    std::filesystem::path controlsPath = m_configDir / "controls.json";
    if (!std::filesystem::exists(controlsPath)) {
        return SaveControls();
    }

    try {
        std::ifstream f(controlsPath);
        if (!f.is_open()) return false;
        json j = json::parse(f);
        UpdateControlsFromJson(j);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ConfigModel] Failed to load controls.json: " << e.what() << "\n";
        return false;
    }
}

bool ConfigModel::SaveControls() {
    std::filesystem::path controlsPath = m_configDir / "controls.json";
    try {
        json j = ControlsToJson();
        std::ofstream f(controlsPath);
        if (!f.is_open()) return false;
        f << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ConfigModel] Failed to save controls.json: " << e.what() << "\n";
        return false;
    }
}

std::string ConfigModel::NormalizeLookupKey(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return out;
}

bool ConfigModel::LoadGamesDatabase() {
    std::filesystem::path dbPath = m_configDir / "games_db.json";
    if (!std::filesystem::exists(dbPath)) {
        std::cerr << "[ConfigModel] games_db.json not found at: " << dbPath.string() << "\n";
        return false;
    }

    try {
        std::ifstream f(dbPath);
        if (!f.is_open()) return false;
        json j = json::parse(f);

        std::lock_guard<std::mutex> lock(m_mutex);
        m_gamesDb.clear();
        m_crcIndex.clear();
        m_noIntroIndex.clear();
        m_cleanNameIndex.clear();

        auto addGameObject = [&](const json& item, const std::string& fallbackKey = "") {
            GameDbEntry entry;
            entry.crc32 = item.value("crc32", fallbackKey);
            entry.id = item.value("id", !entry.crc32.empty() ? entry.crc32 : fallbackKey);
            entry.name = item.value("name", "Unknown Game");
            entry.noIntroName = item.value("noIntroName", item.value("no_intro_name", entry.name));
            entry.year = item.value("year", "Unknown");
            entry.description = item.value("description", "");
            entry.developer = item.value("developer", "");
            entry.publisher = item.value("publisher", "");
            entry.genre = item.value("genre", "Action");
            if (entry.genre.empty()) entry.genre = "SNES Game";
            entry.players = item.value("players", "1-2");
            entry.region = item.value("region", "USA");
            entry.serial = item.value("serial", "");
            entry.boxartFilename = item.value("boxart", item.value("boxart_filename", entry.noIntroName + ".png"));
            entry.hardware = item.value("hardware", "Standard");

            size_t idx = m_gamesDb.size();
            m_gamesDb.push_back(std::move(entry));

            const auto& saved = m_gamesDb[idx];
            if (!saved.crc32.empty()) {
                std::string upperCrc = saved.crc32;
                std::transform(upperCrc.begin(), upperCrc.end(), upperCrc.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                m_crcIndex[upperCrc] = idx;
            }

            std::string normNoIntro = NormalizeLookupKey(saved.noIntroName);
            if (!normNoIntro.empty()) {
                m_noIntroIndex[normNoIntro] = idx;
            }

            std::string normClean = NormalizeLookupKey(saved.name);
            if (!normClean.empty() && m_cleanNameIndex.find(normClean) == m_cleanNameIndex.end()) {
                m_cleanNameIndex[normClean] = idx;
            }
        };

        if (j.contains("gamesByCrc32") && j["gamesByCrc32"].is_object()) {
            const auto& obj = j["gamesByCrc32"];
            m_gamesDb.reserve(obj.size());
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                if (it.value().is_object()) {
                    addGameObject(it.value(), it.key());
                }
            }
        } else if (j.contains("games") && j["games"].is_array()) {
            const auto& arr = j["games"];
            m_gamesDb.reserve(arr.size());
            for (const auto& item : arr) {
                if (item.is_object()) {
                    addGameObject(item);
                }
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ConfigModel] Failed to parse games_db.json: " << e.what() << "\n";
        return false;
    }
}

const GameDbEntry* ConfigModel::FindGameByCrc32(const std::string& crc32) const {
    if (crc32.empty()) return nullptr;
    std::string upper = crc32;
    std::transform(upper.begin(), upper.end(), upper.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_crcIndex.find(upper);
    if (it != m_crcIndex.end() && it->second < m_gamesDb.size()) {
        return &m_gamesDb[it->second];
    }
    return nullptr;
}

const GameDbEntry* ConfigModel::FindGameByName(const std::string& rawFilenameOrTitle) const {
    if (rawFilenameOrTitle.empty()) return nullptr;

    std::string normFull = NormalizeLookupKey(rawFilenameOrTitle);
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it1 = m_noIntroIndex.find(normFull);
    if (it1 != m_noIntroIndex.end() && it1->second < m_gamesDb.size()) {
        return &m_gamesDb[it1->second];
    }

    auto it2 = m_cleanNameIndex.find(normFull);
    if (it2 != m_cleanNameIndex.end() && it2->second < m_gamesDb.size()) {
        return &m_gamesDb[it2->second];
    }

    // Strip parenthetical tags like (USA), [!], etc.
    std::string stripped = rawFilenameOrTitle;
    size_t parenPos = stripped.find_first_of("([");
    if (parenPos != std::string::npos) {
        stripped = stripped.substr(0, parenPos);
    }
    std::string normStripped = NormalizeLookupKey(stripped);
    if (!normStripped.empty()) {
        auto it3 = m_cleanNameIndex.find(normStripped);
        if (it3 != m_cleanNameIndex.end() && it3->second < m_gamesDb.size()) {
            return &m_gamesDb[it3->second];
        }
    }

    return nullptr;
}

GraphicsSettings ConfigModel::GetGraphicsSettings() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_graphics;
}

void ConfigModel::SetGraphicsSettings(const GraphicsSettings& gfx) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_graphics = gfx;
}

EmulationSettings ConfigModel::GetEmulationSettings() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_emulation;
}

void ConfigModel::SetEmulationSettings(const EmulationSettings& emu) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_emulation = emu;
}

std::vector<PlayerBinding> ConfigModel::GetPlayerBindings() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bindings;
}

PlayerBinding ConfigModel::GetPlayerBinding(int playerIndex) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (playerIndex >= 1 && playerIndex <= 4) {
        return m_bindings[playerIndex - 1];
    }
    return PlayerBinding{};
}

void ConfigModel::SetPlayerBinding(int playerIndex, const PlayerBinding& binding) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (playerIndex >= 1 && playerIndex <= 4) {
        m_bindings[playerIndex - 1] = binding;
        m_bindings[playerIndex - 1].playerIndex = playerIndex;
    }
}

json ConfigModel::SettingsToJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return json{
        {"graphics", {
            {"aspectRatio", m_graphics.aspectRatio},
            {"integerScaling", m_graphics.integerScaling},
            {"bilinearFiltering", m_graphics.bilinearFiltering},
            {"cpuFilter", m_graphics.cpuFilter},
            {"crtShader", m_graphics.crtShader},
            {"overscanCrop", m_graphics.overscanCrop},
            {"vsync", m_graphics.vsync},
            {"showFps", m_graphics.showFps},
            {"fullscreen", m_graphics.fullscreen}
        }},
        {"emulation", {
            {"region", m_emulation.region},
            {"speedMultiplier", m_emulation.speedMultiplier},
            {"superFxOverclock", m_emulation.superFxOverclock},
            {"reduceSpriteFlicker", m_emulation.reduceSpriteFlicker},
            {"volume", m_emulation.volume},
            {"mute", m_emulation.mute},
            {"audioInterpolation", m_emulation.audioInterpolation},
            {"autoSaveSram", m_emulation.autoSaveSram},
            {"multitapMode", m_emulation.multitapMode},
            {"activeStateSlot", m_emulation.activeStateSlot},
            {"romFolder", m_emulation.romFolder}
        }}
    };
}

void ConfigModel::UpdateSettingsFromJson(const json& j) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (j.contains("graphics") && j["graphics"].is_object()) {
        const auto& g = j["graphics"];
        m_graphics.aspectRatio = g.value("aspectRatio", m_graphics.aspectRatio);
        m_graphics.integerScaling = g.value("integerScaling", m_graphics.integerScaling);
        m_graphics.bilinearFiltering = g.value("bilinearFiltering", m_graphics.bilinearFiltering);
        m_graphics.cpuFilter = g.value("cpuFilter", m_graphics.cpuFilter);
        m_graphics.crtShader = g.value("crtShader", m_graphics.crtShader);
        m_graphics.overscanCrop = g.value("overscanCrop", m_graphics.overscanCrop);
        m_graphics.vsync = g.value("vsync", m_graphics.vsync);
        m_graphics.showFps = g.value("showFps", m_graphics.showFps);
        m_graphics.fullscreen = g.value("fullscreen", m_graphics.fullscreen);
    }
    if (j.contains("emulation") && j["emulation"].is_object()) {
        const auto& e = j["emulation"];
        m_emulation.region = e.value("region", m_emulation.region);
        m_emulation.speedMultiplier = e.value("speedMultiplier", m_emulation.speedMultiplier);
        m_emulation.superFxOverclock = e.value("superFxOverclock", m_emulation.superFxOverclock);
        m_emulation.reduceSpriteFlicker = e.value("reduceSpriteFlicker", m_emulation.reduceSpriteFlicker);
        m_emulation.volume = std::clamp(e.value("volume", m_emulation.volume), 0, 100);
        m_emulation.mute = e.value("mute", m_emulation.mute);
        m_emulation.audioInterpolation = e.value("audioInterpolation", m_emulation.audioInterpolation);
        m_emulation.autoSaveSram = e.value("autoSaveSram", m_emulation.autoSaveSram);
        m_emulation.multitapMode = e.value("multitapMode", m_emulation.multitapMode);
        m_emulation.activeStateSlot = std::clamp(e.value("activeStateSlot", m_emulation.activeStateSlot), 1, 10);
        m_emulation.romFolder = e.value("romFolder", m_emulation.romFolder);
    }
}

json ConfigModel::ControlsToJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    json playersArr = json::array();
    for (const auto& p : m_bindings) {
        playersArr.push_back({
            {"playerIndex", p.playerIndex},
            {"deviceType", p.deviceType},
            {"deviceId", p.deviceId},
            {"deviceName", p.deviceName},
            {"rumbleEnabled", p.rumbleEnabled},
            {"deadzone", p.deadzone},
            {"buttons", p.buttons}
        });
    }
    return json{
        {"multitapMode", m_emulation.multitapMode},
        {"players", playersArr}
    };
}

void ConfigModel::UpdateControlsFromJson(const json& j) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (j.contains("multitapMode") && j["multitapMode"].is_string()) {
        m_emulation.multitapMode = j["multitapMode"].get<std::string>();
    }
    if (j.contains("players") && j["players"].is_array()) {
        const auto& arr = j["players"];
        for (size_t i = 0; i < arr.size() && i < 4; ++i) {
            const auto& item = arr[i];
            int pIdx = item.value("playerIndex", static_cast<int>(i + 1));
            if (pIdx < 1 || pIdx > 4) continue;
            auto& target = m_bindings[pIdx - 1];
            target.playerIndex = pIdx;
            target.deviceType = item.value("deviceType", target.deviceType);
            target.deviceId = item.value("deviceId", target.deviceId);
            target.deviceName = item.value("deviceName", target.deviceName);
            target.rumbleEnabled = item.value("rumbleEnabled", target.rumbleEnabled);
            target.deadzone = item.value("deadzone", target.deadzone);
            if (item.contains("buttons") && item["buttons"].is_object()) {
                for (auto it = item["buttons"].begin(); it != item["buttons"].end(); ++it) {
                    if (it.value().is_string()) {
                        target.buttons[it.key()] = it.value().get<std::string>();
                    }
                }
            }
        }
    }
}

} // namespace snex9x::model

