#pragma once

#include <string>
#include <filesystem>
#include <functional>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>

namespace snex9x::model {

class BoxartService {
public:
    using BoxartReadyCallback = std::function<void(const std::string& gameId, const std::string& boxartFilename)>;

private:
    std::filesystem::path m_boxartsDir;
    BoxartReadyCallback m_callback;
    mutable std::mutex m_mutex;
    std::unordered_set<std::string> m_inFlight;
    std::unordered_set<std::string> m_failedCache;
    std::atomic<bool> m_shuttingDown{false};

public:
    BoxartService();
    ~BoxartService();

    void Initialize(const std::filesystem::path& boxartsDir, BoxartReadyCallback callback);
    void Shutdown();

    static std::string SanitizeBoxartFilename(const std::string& name);
    static std::string UrlEncodeFilename(const std::string& filename);

    bool HasCachedBoxart(const std::string& boxartFilename) const;
    std::filesystem::path GetCachedBoxartPath(const std::string& boxartFilename) const;

    // Requests an async download if not already cached
    void RequestBoxartAsync(const std::string& gameId, const std::string& boxartFilename, const std::string& cleanGameTitle = "");

private:
    bool DownloadBoxartSync(const std::string& remoteFilename, const std::filesystem::path& destPath, int depth = 0);
    bool HttpGetBytes(const std::wstring& path, std::vector<uint8_t>& outData, uint32_t& outStatusCode);
    static bool IsValidPng(const std::vector<uint8_t>& data);
};

} // namespace snex9x::model

