#include "GameLibraryModel.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>
#include <iostream>

using json = nlohmann::json;

namespace snex9x::model {

static uint32_t s_crcTable[256];
static bool s_crcTableInitialized = false;

static void InitCrcTable() {
    if (s_crcTableInitialized) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        s_crcTable[i] = c;
    }
    s_crcTableInitialized = true;
}

static std::string CurrentIsoTimestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &now);
#else
    localtime_r(&now, &tmBuf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmBuf);
    return std::string(buf);
}

GameLibraryModel::GameLibraryModel(ConfigModel& configModel, BoxartService& boxartService)
    : m_config(configModel), m_boxart(boxartService) {
    InitCrcTable();
}

std::string GameLibraryModel::ComputeRomCrc32(const std::filesystem::path& romPath) {
    InitCrcTable();
    std::ifstream file(romPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return "";

    std::streamsize size = file.tellg();
    if (size <= 0) return "";
    file.seekg(0, std::ios::beg);

    // Strip 512-byte SMC copier header if present
    if ((size % 1024) == 512 && size > 512) {
        file.seekg(512, std::ios::beg);
        size -= 512;
    }

    uint32_t crc = 0xFFFFFFFFu;
    std::vector<uint8_t> buffer(65536);
    std::streamsize remaining = size;
    while (remaining > 0) {
        std::streamsize toRead = std::min<std::streamsize>(remaining, static_cast<std::streamsize>(buffer.size()));
        if (!file.read(reinterpret_cast<char*>(buffer.data()), toRead)) break;
        for (std::streamsize i = 0; i < toRead; ++i) {
            crc = s_crcTable[(crc ^ buffer[i]) & 0xFFu] ^ (crc >> 8);
        }
        remaining -= toRead;
    }
    crc ^= 0xFFFFFFFFu;

    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << crc;
    return oss.str();
}

LibraryGame GameLibraryModel::CreateEntryFromMetadata(const GameDbEntry& meta, const std::string& romPath) const {
    LibraryGame g;
    g.id = meta.id.empty() ? ("game_" + ConfigModel::NormalizeLookupKey(meta.noIntroName)) : meta.id;
    g.romPath = romPath;
    std::error_code ec;
    g.hasLocalRom = !romPath.empty() && std::filesystem::exists(std::filesystem::u8path(romPath), ec);
    g.name = meta.name;
    g.noIntroName = meta.noIntroName;
    g.year = meta.year;
    g.description = meta.description;
    g.developer = meta.developer;
    g.publisher = meta.publisher;
    g.genre = meta.genre;
    g.players = meta.players;
    g.region = meta.region;
    g.serial = meta.serial;
    g.crc32 = meta.crc32;
    g.boxartFilename = meta.boxartFilename;
    g.hardware = meta.hardware;
    return g;
}

void GameLibraryModel::SortGamesAlphabetical() {
    std::sort(m_games.begin(), m_games.end(), [](const LibraryGame& a, const LibraryGame& b) {
        return std::lexicographical_compare(
            a.name.begin(), a.name.end(),
            b.name.begin(), b.name.end(),
            [](char c1, char c2) {
                return std::tolower(static_cast<unsigned char>(c1)) < std::tolower(static_cast<unsigned char>(c2));
            });
    });
}

bool GameLibraryModel::Initialize() {
    if (LoadLibrary()) {
        // Retain only valid local games that the user actually scanned or added
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_games.erase(std::remove_if(m_games.begin(), m_games.end(), [](const LibraryGame& g) {
                std::error_code ec;
                return g.romPath.empty() || !std::filesystem::exists(std::filesystem::u8path(g.romPath), ec);
            }), m_games.end());
            SortGamesAlphabetical();
        }
        SaveLibrary();
    } else {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_games.clear();
        }
        SaveLibrary();
    }

    // Trigger background boxart check for local ROMs
    auto gamesCopy = GetGames();
    for (const auto& g : gamesCopy) {
        if (g.hasLocalRom && !g.boxartFilename.empty()) {
            m_boxart.RequestBoxartAsync(g.id, g.boxartFilename, g.name);
        }
    }

    return true;
}

bool GameLibraryModel::LoadLibrary() {
    std::filesystem::path libPath = m_config.GetConfigDir() / "library.json";
    if (!std::filesystem::exists(libPath)) {
        return false;
    }

    try {
        std::ifstream f(libPath);
        if (!f.is_open()) return false;
        json j = json::parse(f);

        std::lock_guard<std::mutex> lock(m_mutex);
        m_games.clear();

        if (j.contains("games") && j["games"].is_array()) {
            for (const auto& item : j["games"]) {
                LibraryGame g;
                g.id = item.value("id", "");
                g.romPath = item.value("romPath", "");
                std::error_code ec;
                g.hasLocalRom = !g.romPath.empty() && std::filesystem::exists(std::filesystem::u8path(g.romPath), ec);
                g.name = item.value("name", "Unknown Game");
                g.noIntroName = item.value("noIntroName", g.name);
                g.year = item.value("year", "Unknown");
                g.description = item.value("description", "");
                g.developer = item.value("developer", "");
                g.publisher = item.value("publisher", "");
                g.genre = item.value("genre", "Action");
                g.players = item.value("players", "1");
                g.region = item.value("region", "USA");
                g.serial = item.value("serial", "");
                g.crc32 = item.value("crc32", "");
                g.boxartFilename = item.value("boxartFilename", g.noIntroName + ".png");
                g.hardware = item.value("hardware", "Standard");
                g.lastPlayed = item.value("lastPlayed", "");
                g.playCount = item.value("playCount", 0);
                g.favorite = item.value("favorite", false);
                m_games.push_back(std::move(g));
            }
        }
        return !m_games.empty();
    } catch (const std::exception& e) {
        std::cerr << "[GameLibraryModel] Failed to load library.json: " << e.what() << "\n";
        return false;
    }
}

bool GameLibraryModel::SaveLibrary() {
    std::filesystem::path libPath = m_config.GetConfigDir() / "library.json";
    try {
        json j = ToJson();
        std::ofstream f(libPath);
        if (!f.is_open()) return false;
        f << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[GameLibraryModel] Failed to save library.json: " << e.what() << "\n";
        return false;
    }
}

LibraryGame GameLibraryModel::AddOrUpdateRomFile(const std::filesystem::path& romPath) {
    std::string crc = ComputeRomCrc32(romPath);
    std::string stem = romPath.stem().u8string();
    std::string fullPathStr = romPath.u8string();

    const GameDbEntry* meta = nullptr;
    if (!crc.empty()) {
        meta = m_config.FindGameByCrc32(crc);
    }
    if (!meta) {
        meta = m_config.FindGameByName(stem);
    }

    LibraryGame entry;
    if (meta) {
        entry = CreateEntryFromMetadata(*meta, fullPathStr);
        if (!crc.empty()) entry.crc32 = crc;
    } else {
        entry.id = !crc.empty() ? crc : ("local_" + ConfigModel::NormalizeLookupKey(stem));
        entry.romPath = fullPathStr;
        entry.hasLocalRom = true;
        entry.name = stem;
        entry.noIntroName = stem;
        entry.year = "1993";
        entry.description = "Super Nintendo Entertainment System ROM image loaded from local storage (" + romPath.filename().u8string() + ").";
        entry.developer = "Nintendo / Third-Party";
        entry.publisher = "Nintendo";
        entry.genre = "SNES Game";
        entry.players = "1-2";
        entry.region = "NTSC";
        entry.crc32 = crc;
        entry.boxartFilename = BoxartService::SanitizeBoxartFilename(stem);
        entry.hardware = "Standard";
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        bool updated = false;
        for (auto& existing : m_games) {
            if (existing.id == entry.id ||
                (!existing.noIntroName.empty() && existing.noIntroName == entry.noIntroName) ||
                (!existing.romPath.empty() && existing.romPath == entry.romPath)) {
                existing.romPath = entry.romPath;
                existing.hasLocalRom = true;
                if (!entry.crc32.empty()) existing.crc32 = entry.crc32;
                entry = existing;
                updated = true;
                break;
            }
        }
        if (!updated) {
            m_games.push_back(entry);
        }
        SortGamesAlphabetical();
    }

    SaveLibrary();
    m_boxart.RequestBoxartAsync(entry.id, entry.boxartFilename, entry.name);
    return entry;
}

int GameLibraryModel::ScanRomDirectory(const std::filesystem::path& folderPath) {
    std::error_code ec;
    if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec)) {
        return 0;
    }

    int count = 0;
    for (const auto& dirEntry : std::filesystem::recursive_directory_iterator(folderPath, ec)) {
        if (!dirEntry.is_regular_file(ec)) continue;
        std::string ext = dirEntry.path().extension().u8string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".sfc" || ext == ".smc" || ext == ".swc" || ext == ".fig" || ext == ".bs") {
            AddOrUpdateRomFile(dirEntry.path());
            ++count;
        }
    }

    if (count > 0) {
        auto emu = m_config.GetEmulationSettings();
        emu.romFolder = folderPath.u8string();
        m_config.SetEmulationSettings(emu);
        m_config.SaveSettings();
    }
    return count;
}

bool GameLibraryModel::RemoveGame(const std::string& id) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::remove_if(m_games.begin(), m_games.end(),
            [&id](const LibraryGame& g) { return g.id == id; });
        if (it != m_games.end()) {
            m_games.erase(it, m_games.end());
            removed = true;
        }
    }
    if (removed) SaveLibrary();
    return removed;
}

void GameLibraryModel::RecordGamePlayed(const std::string& id) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& g : m_games) {
            if (g.id == id) {
                g.lastPlayed = CurrentIsoTimestamp();
                g.playCount += 1;
                break;
            }
        }
    }
    SaveLibrary();
}

std::vector<LibraryGame> GameLibraryModel::GetGames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_games;
}

const LibraryGame* GameLibraryModel::FindGameById(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& g : m_games) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

json GameLibraryModel::ToJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    json arr = json::array();
    for (const auto& g : m_games) {
        std::string safeBoxart = BoxartService::SanitizeBoxartFilename(g.boxartFilename);
        bool cached = m_boxart.HasCachedBoxart(safeBoxart);
        std::string boxartUrl = cached ? ("snes://boxarts/" + BoxartService::UrlEncodeFilename(safeBoxart)) : "";

        arr.push_back({
            {"id", g.id},
            {"romPath", g.romPath},
            {"hasLocalRom", g.hasLocalRom},
            {"name", g.name},
            {"noIntroName", g.noIntroName},
            {"year", g.year},
            {"description", g.description},
            {"developer", g.developer},
            {"publisher", g.publisher},
            {"genre", g.genre},
            {"players", g.players},
            {"region", g.region},
            {"serial", g.serial},
            {"crc32", g.crc32},
            {"boxartFilename", safeBoxart},
            {"boxartUrl", boxartUrl},
            {"hardware", g.hardware},
            {"lastPlayed", g.lastPlayed},
            {"playCount", g.playCount},
            {"favorite", g.favorite}
        });
    }
    return json{{"games", arr}};
}

} // namespace snex9x::model

