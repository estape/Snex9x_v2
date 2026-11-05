#include "BoxartService.hpp"
#include <windows.h>
#include <winhttp.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>

#pragma comment(lib, "winhttp.lib")

namespace snex9x::model {

static const wchar_t* kGithubHost = L"raw.githubusercontent.com";
static const std::string kBoxartsBasePath = "/libretro-thumbnails/Nintendo_-_Super_Nintendo_Entertainment_System/cdd8f1d44612976cc9d422b0ace53a8542667688/Named_Boxarts/";
static const std::string kRestrictedChars = "&*/:`<>?\\|\"";
static const uint8_t kPngSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
static const std::vector<std::string> kFallbackBoxartSuffixes = {
    " (USA).png",
    " (USA, Europe).png",
    " (Europe).png",
    " (Japan).png",
    " (World).png"
};

BoxartService::BoxartService() = default;

BoxartService::~BoxartService() {
    Shutdown();
}

void BoxartService::Initialize(const std::filesystem::path& boxartsDir, BoxartReadyCallback callback) {
    m_boxartsDir = boxartsDir;
    m_callback = std::move(callback);
    m_shuttingDown = false;
    std::error_code ec;
    std::filesystem::create_directories(m_boxartsDir, ec);

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_workerRunning) {
        m_workerRunning = true;
        m_workerThread = std::thread(&BoxartService::WorkerLoop, this);
    }
}

void BoxartService::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shuttingDown = true;
        m_workerRunning = false;
    }
    m_cv.notify_all();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

std::string BoxartService::SanitizeBoxartFilename(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (kRestrictedChars.find(c) != std::string::npos) {
            out.push_back('_');
        } else {
            out.push_back(c);
        }
    }
    if (out.size() < 4 || out.substr(out.size() - 4) != ".png") {
        out += ".png";
    }
    return out;
}

std::string BoxartService::UrlEncodeFilename(const std::string& filename) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex << std::uppercase;

    for (unsigned char c : filename) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(c);
        }
    }
    return escaped.str();
}

bool BoxartService::HasCachedBoxart(const std::string& boxartFilename) const {
    if (boxartFilename.empty()) return false;
    std::filesystem::path p = GetCachedBoxartPath(boxartFilename);
    std::error_code ec;
    return std::filesystem::exists(p, ec) && std::filesystem::file_size(p, ec) > 64;
}

std::filesystem::path BoxartService::GetCachedBoxartPath(const std::string& boxartFilename) const {
    std::string safe = SanitizeBoxartFilename(boxartFilename);
    return m_boxartsDir / std::filesystem::u8path(safe);
}

bool BoxartService::IsValidPng(const std::vector<uint8_t>& data) {
    if (data.size() < 8) return false;
    return std::memcmp(data.data(), kPngSignature, 8) == 0;
}

bool BoxartService::HttpGetBytes(const std::wstring& path, std::vector<uint8_t>& outData, uint32_t& outStatusCode) {
    outData.clear();
    outStatusCode = 0;

    HINTERNET hSession = WinHttpOpen(
        L"Snex9x_v2/2.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession) return false;

    WinHttpSetTimeouts(hSession, 5000, 5000, 8000, 8000);

    HINTERNET hConnect = WinHttpConnect(hSession, kGithubHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    bool ok = false;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD dwStatusCode = 0;
        DWORD statusCodeSize = sizeof(dwStatusCode);
        WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &dwStatusCode,
            &statusCodeSize,
            WINHTTP_NO_HEADER_INDEX);
        outStatusCode = static_cast<uint32_t>(dwStatusCode);

        if (outStatusCode == 200) {
            DWORD dwSize = 0;
            do {
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                if (dwSize == 0) {
                    ok = true;
                    break;
                }
                size_t oldSize = outData.size();
                outData.resize(oldSize + dwSize);
                DWORD dwDownloaded = 0;
                if (!WinHttpReadData(hRequest, outData.data() + oldSize, dwSize, &dwDownloaded)) {
                    break;
                }
                outData.resize(oldSize + dwDownloaded);
            } while (dwSize > 0);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok && outStatusCode == 200 && !outData.empty();
}

bool BoxartService::DownloadBoxartSync(const std::string& remoteFilename, const std::filesystem::path& destPath, int depth) {
    if (depth > 3 || m_shuttingDown) return false;

    std::string sanitized = SanitizeBoxartFilename(remoteFilename);
    std::string encoded = UrlEncodeFilename(sanitized);
    std::string fullUrlPathUtf8 = kBoxartsBasePath + encoded;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, fullUrlPathUtf8.c_str(), static_cast<int>(fullUrlPathUtf8.size()), nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wPath(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, fullUrlPathUtf8.c_str(), static_cast<int>(fullUrlPathUtf8.size()), &wPath[0], wlen);

    std::vector<uint8_t> data;
    uint32_t statusCode = 0;
    if (!HttpGetBytes(wPath, data, statusCode)) {
        return false;
    }

    if (IsValidPng(data)) {
        std::ofstream out(destPath, std::ios::binary);
        if (!out.is_open()) return false;
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        out.close();
        return true;
    }

    // Check if Git symlink (mode 120000 in libretro-thumbnails returns a short UTF-8 target filename)
    if (data.size() > 4 && data.size() < 260) {
        std::string symlinkTarget(reinterpret_cast<const char*>(data.data()), data.size());
        while (!symlinkTarget.empty() && (symlinkTarget.back() == '\n' || symlinkTarget.back() == '\r' || symlinkTarget.back() == ' ')) {
            symlinkTarget.pop_back();
        }
        if (symlinkTarget.size() > 4 && symlinkTarget.substr(symlinkTarget.size() - 4) == ".png") {
            return DownloadBoxartSync(symlinkTarget, destPath, depth + 1);
        }
    }

    return false;
}

void BoxartService::WorkerLoop() {
    while (true) {
        BoxartRequest req;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() {
                return !m_workerRunning || !m_queue.empty();
            });
            if (!m_workerRunning && m_queue.empty()) {
                break;
            }
            if (m_queue.empty()) {
                continue;
            }
            req = std::move(m_queue.front());
            m_queue.pop();
        }

        if (m_shuttingDown) break;

        ProcessDownload(req);
    }
}

void BoxartService::ProcessDownload(const BoxartRequest& req) {
    std::filesystem::path destPath = GetCachedBoxartPath(req.safeName);
    bool success = DownloadBoxartSync(req.safeName, destPath);

    if (!success && !req.cleanGameTitle.empty()) {
        std::string cleanSanitized = SanitizeBoxartFilename(req.cleanGameTitle);
        if (cleanSanitized.size() > 4 && cleanSanitized.substr(cleanSanitized.size() - 4) == ".png") {
            cleanSanitized = cleanSanitized.substr(0, cleanSanitized.size() - 4);
        }
        for (const auto& suffix : kFallbackBoxartSuffixes) {
            if (m_shuttingDown) break;
            std::string candidate = cleanSanitized + suffix;
            if (candidate == req.safeName) continue;
            if (DownloadBoxartSync(candidate, destPath)) {
                success = true;
                break;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inFlight.erase(req.safeName);
        if (!success) {
            m_failedCache.insert(req.safeName);
        }
    }

    if (success && !m_shuttingDown && m_callback) {
        m_callback(req.gameId, req.safeName);
    }
}

void BoxartService::RequestBoxartAsync(const std::string& gameId, const std::string& boxartFilename, const std::string& cleanGameTitle) {
    if (boxartFilename.empty() || m_shuttingDown) return;

    std::string safeName = SanitizeBoxartFilename(boxartFilename);
    if (HasCachedBoxart(safeName)) {
        if (m_callback) m_callback(gameId, safeName);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_inFlight.count(safeName) || m_failedCache.count(safeName)) {
            return;
        }
        m_inFlight.insert(safeName);
        m_queue.push(BoxartRequest{gameId, safeName, cleanGameTitle});
    }
    m_cv.notify_one();
}

} // namespace snex9x::model

