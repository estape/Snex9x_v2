#pragma once

#include "ConfigModel.hpp"
#include "BoxartService.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <mutex>
#include <json.hpp>

namespace snex9x::model {

struct LibraryGame {
    std::string id;
    std::string romPath;
    bool hasLocalRom = false;
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
    std::string lastPlayed;
    int playCount = 0;
    bool favorite = false;
};

class GameLibraryModel {
public:
    GameLibraryModel(ConfigModel& configModel, BoxartService& boxartService);
    ~GameLibraryModel() = default;

    bool Initialize();
    bool LoadLibrary();
    bool SaveLibrary();

    // Adds or updates a single ROM file in the library and returns its entry
    LibraryGame AddOrUpdateRomFile(const std::filesystem::path& romPath);

    // Scans a directory for .sfc, .smc, .swc, .fig, .bs files
    int ScanRomDirectory(const std::filesystem::path& folderPath);

    // Removes a game from the library by ID
    bool RemoveGame(const std::string& id);

    // Updates last played timestamp and play count
    void RecordGamePlayed(const std::string& id);

    // Gets all library entries
    std::vector<LibraryGame> GetGames() const;

    // Finds a specific game by ID or ROM path
    const LibraryGame* FindGameById(const std::string& id) const;

    // Serializes library to JSON for the frontend View
    nlohmann::json ToJson() const;

    // Computes CRC32 of a ROM file, stripping 512-byte SMC copier header if present
    static std::string ComputeRomCrc32(const std::filesystem::path& romPath);

private:
    void EnsureBuiltinDemoRom();
    void SortGamesAlphabetical();
    LibraryGame CreateEntryFromMetadata(const GameDbEntry& meta, const std::string& romPath = "") const;

    ConfigModel& m_config;
    BoxartService& m_boxart;
    mutable std::mutex m_mutex;
    std::vector<LibraryGame> m_games;
};

} // namespace snex9x::model

