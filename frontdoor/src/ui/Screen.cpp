#include "pch.h"
#include "Screen.h"
#include "../core/Bridge.h"
#include "../core/Injector.h"
#include "../utils/ProcessHelper.h"
#include "../config/ClientInfo.h"
#include "../../../shared/common/ModuleConfig.h"
#include "../../../deps/imgui/colors.h"
#include "../../../deps/imgui/inter_bold_font.h"
#include "../../../deps/imgui/inter_regular_font.h"
#include "../../../deps/imgui/play_bold_font.h"
#include "../../../deps/imgui/play_regular_font.h"
#include "../../../deps/imgui/sword_icon.h"
#include "../../../deps/imgui/running_icon.h"
#include "../../../deps/imgui/eye_icon.h"
#include "../../../deps/imgui/settings_icon.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "../../../deps/imgui/stb_image.h"
#include "../../../shared/common/RegisterModules.h"
#include <array>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <mutex>
#include <thread>
#include <urlmon.h>
#include <winhttp.h>

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "winhttp.lib")

namespace
{
    constexpr const char* kCreatorProfileUrl = "https://github.com/Lopesnextgen";
    constexpr const char* kDiscordProfileUrl = "https://discord.com/fuckscriptkiddies";
    constexpr const char* kProjectRepositoryUrl = "https://github.com/TesseractLiberty/OpenCommunity";
    constexpr const char* kProjectReleasesUrl = "https://github.com/TesseractLiberty/OpenCommunity/releases";
    constexpr wchar_t kGitHubApiHost[] = L"api.github.com";
    constexpr wchar_t kGitHubLatestReleasePath[] = L"/repos/TesseractLiberty/OpenCommunity/releases/latest";

    enum class ReleaseCheckState {
        Idle,
        Checking,
        UpToDate,
        UpdateAvailable,
        LocalBuild,
        Error
    };

    struct LocalBuildInfo
    {
        std::string label = "local build";
        std::string releaseTag;
        std::string commitHash;
        bool isReleaseTag = false;
    };

    struct ReleaseCheckStatus
    {
        ReleaseCheckState state = ReleaseCheckState::Idle;
        std::string currentLabel = "local build";
        std::string latestTag;
        std::string latestUrl = kProjectReleasesUrl;
        std::string message = "Use Verify updates to compare this build with the latest GitHub release.";
    };

    struct SettingsTextSegment
    {
        std::string text;
        const char* url = nullptr;
        bool accent = false;
    };

    struct PlayerHeadTextureEntry
    {
        ID3D11ShaderResourceView* texture = nullptr;
        std::filesystem::path cachePath;
        bool downloadQueued = false;
        bool downloadFailed = false;
    };

    constexpr float kInjectionTypewriterCharsPerSecond = 30.0f;
    constexpr float kInjectionCursorBlinkSpeed = 6.0f;
    constexpr char kInjectingHeadline[] = "Injecting";
    constexpr char kInjectedHeadline[] = "Successful, Injected!";

    std::mutex g_PlayerHeadCacheMutex;
    std::unordered_map<std::string, PlayerHeadTextureEntry> g_PlayerHeadCache;
    std::mutex g_ReleaseCheckMutex;
    std::atomic<bool> g_ReleaseCheckInProgress = false;

    std::string ReadTextFile(const std::filesystem::path& filePath)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            return {};
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    std::string TrimCopy(std::string value)
    {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
            value.erase(value.begin());
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
            value.pop_back();
        }
        return value;
    }

    std::filesystem::path ResolveRepositoryRoot()
    {
        wchar_t modulePath[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0) {
            return {};
        }

        auto current = std::filesystem::path(modulePath).parent_path();
        while (!current.empty()) {
            if (std::filesystem::exists(current / ".git")) {
                return current;
            }

            const auto parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }

        return {};
    }

    std::filesystem::path ResolveGitDirectory(const std::filesystem::path& repositoryRoot)
    {
        const auto gitPath = repositoryRoot / ".git";
        if (std::filesystem::is_directory(gitPath)) {
            return gitPath;
        }

        if (!std::filesystem::is_regular_file(gitPath)) {
            return {};
        }

        const std::string descriptor = TrimCopy(ReadTextFile(gitPath));
        constexpr std::string_view prefix = "gitdir:";
        if (descriptor.rfind(prefix.data(), 0) != 0) {
            return {};
        }

        std::filesystem::path resolvedPath = TrimCopy(descriptor.substr(prefix.size()));
        if (resolvedPath.is_relative()) {
            resolvedPath = repositoryRoot / resolvedPath;
        }

        return resolvedPath;
    }

    std::string FindPackedRefHash(const std::filesystem::path& gitDirectory, const std::string& refName)
    {
        std::ifstream file(gitDirectory / "packed-refs");
        if (!file) {
            return {};
        }

        std::string line;
        while (std::getline(file, line)) {
            const std::string trimmed = TrimCopy(line);
            if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == '^') {
                continue;
            }

            const size_t split = trimmed.find(' ');
            if (split == std::string::npos) {
                continue;
            }

            if (trimmed.substr(split + 1) == refName) {
                return trimmed.substr(0, split);
            }
        }

        return {};
    }

    std::string ResolveGitReferenceHash(const std::filesystem::path& gitDirectory, const std::string& refName)
    {
        const auto refPath = gitDirectory / std::filesystem::path(refName);
        const std::string directHash = TrimCopy(ReadTextFile(refPath));
        if (!directHash.empty()) {
            return directHash;
        }

        return FindPackedRefHash(gitDirectory, refName);
    }

    std::string ResolveGitTagForHash(const std::filesystem::path& gitDirectory, const std::string& headHash)
    {
        if (headHash.empty()) {
            return {};
        }

        const auto tagsDirectory = gitDirectory / "refs" / "tags";
        if (std::filesystem::exists(tagsDirectory)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(tagsDirectory)) {
                if (!entry.is_regular_file()) {
                    continue;
                }

                if (TrimCopy(ReadTextFile(entry.path())) != headHash) {
                    continue;
                }

                std::error_code relativeError;
                const auto relative = std::filesystem::relative(entry.path(), tagsDirectory, relativeError);
                if (!relativeError) {
                    return relative.generic_string();
                }
            }
        }

        std::ifstream packedRefs(gitDirectory / "packed-refs");
        if (!packedRefs) {
            return {};
        }

        std::string line;
        while (std::getline(packedRefs, line)) {
            const std::string trimmed = TrimCopy(line);
            if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == '^') {
                continue;
            }

            const size_t split = trimmed.find(' ');
            if (split == std::string::npos) {
                continue;
            }

            const std::string hash = trimmed.substr(0, split);
            const std::string refName = trimmed.substr(split + 1);
            if (hash == headHash && refName.rfind("refs/tags/", 0) == 0) {
                return refName.substr(strlen("refs/tags/"));
            }
        }

        return {};
    }

    LocalBuildInfo BuildLocalBuildInfo()
    {
        LocalBuildInfo info;

        const auto repositoryRoot = ResolveRepositoryRoot();
        if (repositoryRoot.empty()) {
            return info;
        }

        const auto gitDirectory = ResolveGitDirectory(repositoryRoot);
        if (gitDirectory.empty()) {
            return info;
        }

        const std::string headContent = TrimCopy(ReadTextFile(gitDirectory / "HEAD"));
        if (headContent.empty()) {
            return info;
        }

        std::string headHash;
        constexpr std::string_view refPrefix = "ref:";
        if (headContent.rfind(refPrefix.data(), 0) == 0) {
            const std::string refName = TrimCopy(headContent.substr(refPrefix.size()));
            headHash = ResolveGitReferenceHash(gitDirectory, refName);
        } else {
            headHash = headContent;
        }

        if (headHash.empty()) {
            return info;
        }

        info.commitHash = headHash;

        const std::string releaseTag = ResolveGitTagForHash(gitDirectory, headHash);
        if (!releaseTag.empty()) {
            info.label = releaseTag;
            info.releaseTag = releaseTag;
            info.isReleaseTag = true;
            return info;
        }

        const size_t shortHashLength = (std::min)(static_cast<size_t>(7), headHash.size());
        info.label = "commit " + headHash.substr(0, shortHashLength);
        return info;
    }

    const LocalBuildInfo& GetCurrentBuildInfo()
    {
        static const LocalBuildInfo buildInfo = BuildLocalBuildInfo();
        return buildInfo;
    }

    ReleaseCheckStatus CreateDefaultReleaseCheckStatus()
    {
        ReleaseCheckStatus status;
        status.currentLabel = GetCurrentBuildInfo().label;
        return status;
    }

    ReleaseCheckStatus g_ReleaseCheckStatus = CreateDefaultReleaseCheckStatus();

    void OpenExternalUrl(const char* url)
    {
        if (!url || !url[0]) {
            return;
        }

        ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
    }

    bool FetchLatestReleaseJson(std::string& outBody, DWORD& outStatusCode, std::string& outError)
    {
        outBody.clear();
        outStatusCode = 0;
        outError.clear();

        HINTERNET session = WinHttpOpen(L"OpenCommunity/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            outError = "Failed to initialize HTTP session.";
            return false;
        }

        HINTERNET connection = WinHttpConnect(session, kGitHubApiHost, static_cast<INTERNET_PORT>(443), 0);
        if (!connection) {
            WinHttpCloseHandle(session);
            outError = "Failed to connect to GitHub.";
            return false;
        }

        HINTERNET request = WinHttpOpenRequest(
            connection,
            L"GET",
            kGitHubLatestReleasePath,
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);
        if (!request) {
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            outError = "Failed to create the update request.";
            return false;
        }

        const wchar_t* requestHeaders =
            L"Accept: application/vnd.github+json\r\n"
            L"X-GitHub-Api-Version: 2022-11-28\r\n";
        WinHttpAddRequestHeaders(request, requestHeaders, -1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        const bool sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == TRUE;
        const bool received = sent && WinHttpReceiveResponse(request, nullptr) == TRUE;
        if (!received) {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            outError = "Failed to fetch the latest release.";
            return false;
        }

        DWORD statusCodeSize = sizeof(outStatusCode);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &outStatusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

        for (;;) {
            DWORD availableBytes = 0;
            if (WinHttpQueryDataAvailable(request, &availableBytes) != TRUE) {
                outError = "Failed while reading the update response.";
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                return false;
            }

            if (availableBytes == 0) {
                break;
            }

            std::string chunk(availableBytes, '\0');
            DWORD bytesRead = 0;
            if (WinHttpReadData(request, chunk.data(), availableBytes, &bytesRead) != TRUE) {
                outError = "Failed while reading the update response.";
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                return false;
            }

            chunk.resize(bytesRead);
            outBody += chunk;
        }

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return true;
    }

    std::string ExtractJsonStringValue(const std::string& json, const char* key)
    {
        if (!key || !key[0]) {
            return {};
        }

        const std::string pattern = "\"" + std::string(key) + "\"";
        const size_t keyPos = json.find(pattern);
        if (keyPos == std::string::npos) {
            return {};
        }

        const size_t colonPos = json.find(':', keyPos + pattern.size());
        if (colonPos == std::string::npos) {
            return {};
        }

        const size_t firstQuote = json.find('"', colonPos + 1);
        if (firstQuote == std::string::npos) {
            return {};
        }

        std::string result;
        bool escaping = false;
        for (size_t index = firstQuote + 1; index < json.size(); ++index) {
            const char current = json[index];
            if (escaping) {
                switch (current) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default: result.push_back(current); break;
                }
                escaping = false;
                continue;
            }

            if (current == '\\') {
                escaping = true;
                continue;
            }

            if (current == '"') {
                return result;
            }

            result.push_back(current);
        }

        return {};
    }

    void StartReleaseCheckAsync()
    {
        if (g_ReleaseCheckInProgress.exchange(true)) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_ReleaseCheckMutex);
            g_ReleaseCheckStatus = CreateDefaultReleaseCheckStatus();
            g_ReleaseCheckStatus.state = ReleaseCheckState::Checking;
            g_ReleaseCheckStatus.message = "Checking the latest GitHub release...";
        }

        std::thread([]() {
            ReleaseCheckStatus nextStatus = CreateDefaultReleaseCheckStatus();
            nextStatus.state = ReleaseCheckState::Error;

            DWORD statusCode = 0;
            std::string responseBody;
            std::string requestError;
            if (!FetchLatestReleaseJson(responseBody, statusCode, requestError)) {
                nextStatus.message = requestError.empty() ? "Unable to verify updates right now." : requestError;
            } else if (statusCode == 404) {
                nextStatus.message = "No published GitHub release was found yet.";
            } else if (statusCode != 200) {
                nextStatus.message = "GitHub returned HTTP " + std::to_string(statusCode) + " while checking releases.";
            } else {
                nextStatus.latestTag = ExtractJsonStringValue(responseBody, "tag_name");
                nextStatus.latestUrl = ExtractJsonStringValue(responseBody, "html_url");
                if (nextStatus.latestUrl.empty()) {
                    nextStatus.latestUrl = kProjectReleasesUrl;
                }

                if (nextStatus.latestTag.empty()) {
                    nextStatus.message = "The latest release response did not include a tag.";
                } else {
                    const auto& buildInfo = GetCurrentBuildInfo();
                    if (buildInfo.isReleaseTag && _stricmp(buildInfo.releaseTag.c_str(), nextStatus.latestTag.c_str()) == 0) {
                        nextStatus.state = ReleaseCheckState::UpToDate;
                        nextStatus.message = "This build matches the latest published release.";
                    } else if (buildInfo.isReleaseTag) {
                        nextStatus.state = ReleaseCheckState::UpdateAvailable;
                        nextStatus.message = "A newer published release is available.";
                    } else {
                        nextStatus.state = ReleaseCheckState::LocalBuild;
                        nextStatus.message = "This build is local and does not map to a published release tag.";
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(g_ReleaseCheckMutex);
                g_ReleaseCheckStatus = std::move(nextStatus);
            }

            g_ReleaseCheckInProgress.store(false);
        }).detach();
    }

    float Clamp01(float value)
    {
        return (value < 0.0f) ? 0.0f : ((value > 1.0f) ? 1.0f : value);
    }

    float EaseOutCubic(float t)
    {
        const float clamped = Clamp01(t);
        const float inv = 1.0f - clamped;
        return 1.0f - inv * inv * inv;
    }

    float EaseInOutCubic(float t)
    {
        const float clamped = Clamp01(t);
        return (clamped < 0.5f)
            ? 4.0f * clamped * clamped * clamped
            : 1.0f - powf(-2.0f * clamped + 2.0f, 3.0f) * 0.5f;
    }

    float EaseOutBack(float t)
    {
        const float clamped = Clamp01(t);
        const float c1 = 1.70158f;
        const float c3 = c1 + 1.0f;
        const float x = clamped - 1.0f;
        return 1.0f + c3 * x * x * x + c1 * x * x;
    }

    float ProgressRange(float elapsed, float start, float duration)
    {
        if (duration <= 0.0f)
            return elapsed >= start ? 1.0f : 0.0f;
        return Clamp01((elapsed - start) / duration);
    }

    ImVec2 CalcTextSizeWithFont(ImFont* font, const char* text, float fontSize)
    {
        if (!font)
            return ImGui::CalcTextSize(text);
        return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    }

    std::vector<std::string> TokenizeWrappedText(const std::string& text)
    {
        std::vector<std::string> tokens;
        size_t index = 0;
        while (index < text.size()) {
            const bool isWhitespace = std::isspace(static_cast<unsigned char>(text[index])) != 0;
            size_t next = index + 1;
            while (next < text.size() && (std::isspace(static_cast<unsigned char>(text[next])) != 0) == isWhitespace) {
                ++next;
            }

            tokens.push_back(text.substr(index, next - index));
            index = next;
        }

        return tokens;
    }

    float DrawWrappedSettingsLine(
        ImDrawList* drawList,
        const ImVec2& startPos,
        float maxWidth,
        const std::vector<SettingsTextSegment>& segments,
        ImFont* regularFont,
        ImFont* accentFont,
        float fontSize,
        ImU32 textColor,
        ImU32 accentColor,
        ImU32 accentHoverColor)
    {
        if (maxWidth <= 0.0f) {
            return 0.0f;
        }

        const bool canDraw = drawList != nullptr;
        const float lineAdvance = fontSize + 8.0f;
        float cursorX = startPos.x;
        float cursorY = startPos.y;

        for (const auto& segment : segments) {
            ImFont* font = segment.accent && accentFont ? accentFont : regularFont;
            if (!font) {
                font = ImGui::GetFont();
            }

            for (const auto& token : TokenizeWrappedText(segment.text)) {
                if (token.empty()) {
                    continue;
                }

                const bool whitespaceOnly = std::all_of(token.begin(), token.end(), [](unsigned char ch) {
                    return std::isspace(ch) != 0;
                });

                const ImVec2 tokenSize = CalcTextSizeWithFont(font, token.c_str(), fontSize);
                if (!whitespaceOnly && cursorX > startPos.x && (cursorX + tokenSize.x) > (startPos.x + maxWidth)) {
                    cursorX = startPos.x;
                    cursorY += lineAdvance;
                }

                if (whitespaceOnly && cursorX <= startPos.x) {
                    continue;
                }

                const bool isLink = segment.url && segment.url[0] && !whitespaceOnly;
                ImU32 drawColor = segment.accent ? accentColor : textColor;
                if (isLink && canDraw) {
                    const ImVec2 tokenMin(cursorX, cursorY);
                    const ImVec2 tokenMax(cursorX + tokenSize.x, cursorY + fontSize + 2.0f);
                    const bool hovered = ImGui::IsMouseHoveringRect(tokenMin, tokenMax, false);
                    drawColor = hovered ? accentHoverColor : accentColor;
                    if (hovered) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            OpenExternalUrl(segment.url);
                        }
                    }
                }

                if (canDraw) {
                    drawList->AddText(font, fontSize, ImVec2(cursorX, cursorY), drawColor, token.c_str());
                }

                if (isLink && canDraw) {
                    const float underlineY = cursorY + fontSize + 1.0f;
                    drawList->AddLine(ImVec2(cursorX, underlineY), ImVec2(cursorX + tokenSize.x, underlineY), drawColor, 1.0f);
                }

                cursorX += tokenSize.x;
            }
        }

        return (cursorY - startPos.y) + lineAdvance;
    }

    ImU32 MakeColorU32(float r, float g, float b, float a = 1.0f)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
    }

    void DrawShadowedText(ImDrawList* drawList, ImFont* font, float fontSize, const ImVec2& pos, ImU32 color, ImU32 shadowColor, const std::string& text)
    {
        drawList->AddText(font, fontSize, ImVec2(pos.x + 1.0f, pos.y + 1.0f), shadowColor, text.c_str());
        drawList->AddText(font, fontSize, pos, color, text.c_str());
    }

    std::string NormalizePlayerHeadKey(const std::string& name)
    {
        std::string key;
        key.reserve(name.size());

        for (const unsigned char ch : name) {
            if (std::isalnum(ch) || ch == '_' || ch == '-') {
                key.push_back(static_cast<char>(std::tolower(ch)));
            }
        }

        return key;
    }

    std::filesystem::path GetPlayerHeadCacheDirectory()
    {
        wchar_t tempPath[MAX_PATH] = {};
        const DWORD tempLength = GetTempPathW(MAX_PATH, tempPath);
        if (tempLength == 0 || tempLength >= MAX_PATH) {
            return {};
        }

        return std::filesystem::path(tempPath) / "OpenCommunity" / "player_heads";
    }

    ID3D11ShaderResourceView* CreateTextureFromPixels(ID3D11Device* device, const unsigned char* pixels, int width, int height)
    {
        if (!device || !pixels || width <= 0 || height <= 0) {
            return nullptr;
        }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA subresource = {};
        subresource.pSysMem = pixels;
        subresource.SysMemPitch = width * 4;

        ID3D11Texture2D* texture = nullptr;
        if (FAILED(device->CreateTexture2D(&desc, &subresource, &texture)) || !texture) {
            return nullptr;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        ID3D11ShaderResourceView* shaderResourceView = nullptr;
        const HRESULT result = device->CreateShaderResourceView(texture, &srvDesc, &shaderResourceView);
        texture->Release();
        return SUCCEEDED(result) ? shaderResourceView : nullptr;
    }

    ID3D11ShaderResourceView* CreateTextureFromFile(ID3D11Device* device, const std::filesystem::path& imagePath)
    {
        if (!device || imagePath.empty() || !std::filesystem::exists(imagePath)) {
            return nullptr;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load(imagePath.string().c_str(), &width, &height, &channels, 4);
        if (!pixels) {
            return nullptr;
        }

        ID3D11ShaderResourceView* texture = CreateTextureFromPixels(device, pixels, width, height);
        stbi_image_free(pixels);
        return texture;
    }

    void QueuePlayerHeadDownload(const std::string& playerName)
    {
        if (playerName.empty()) {
            return;
        }

        const std::string cacheKey = NormalizePlayerHeadKey(playerName);
        if (cacheKey.empty()) {
            return;
        }

        const std::filesystem::path cacheDirectory = GetPlayerHeadCacheDirectory();
        const std::filesystem::path cachePath = cacheDirectory / (cacheKey + ".png");

        {
            std::lock_guard<std::mutex> lock(g_PlayerHeadCacheMutex);
            auto& entry = g_PlayerHeadCache[cacheKey];
            entry.cachePath = cachePath;
            if (entry.downloadQueued || entry.downloadFailed || entry.texture || std::filesystem::exists(cachePath)) {
                return;
            }

            entry.downloadQueued = true;
            entry.downloadFailed = false;
        }

        std::thread([playerName, cacheKey, cacheDirectory, cachePath]() {
            std::error_code errorCode;
            std::filesystem::create_directories(cacheDirectory, errorCode);

            const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            const std::string downloadUrl = "https://minotar.net/helm/" + playerName + "/32.png";
            const HRESULT downloadResult = URLDownloadToFileA(nullptr, downloadUrl.c_str(), cachePath.string().c_str(), 0, nullptr);
            if (SUCCEEDED(initResult)) {
                CoUninitialize();
            }

            std::lock_guard<std::mutex> lock(g_PlayerHeadCacheMutex);
            auto& entry = g_PlayerHeadCache[cacheKey];
            entry.downloadQueued = false;
            entry.downloadFailed = FAILED(downloadResult) || !std::filesystem::exists(cachePath);
        }).detach();
    }

    ID3D11ShaderResourceView* GetPlayerHeadTexture(ID3D11Device* device, const std::string& playerName)
    {
        if (!device || playerName.empty()) {
            return nullptr;
        }

        const std::string cacheKey = NormalizePlayerHeadKey(playerName);
        if (cacheKey.empty()) {
            return nullptr;
        }

        std::filesystem::path cachePath;
        {
            std::lock_guard<std::mutex> lock(g_PlayerHeadCacheMutex);
            auto& entry = g_PlayerHeadCache[cacheKey];
            if (entry.cachePath.empty()) {
                entry.cachePath = GetPlayerHeadCacheDirectory() / (cacheKey + ".png");
            }

            if (entry.texture) {
                return entry.texture;
            }

            if (entry.downloadFailed) {
                return nullptr;
            }

            cachePath = entry.cachePath;
        }

        if (std::filesystem::exists(cachePath)) {
            ID3D11ShaderResourceView* texture = CreateTextureFromFile(device, cachePath);
            if (texture) {
                std::lock_guard<std::mutex> lock(g_PlayerHeadCacheMutex);
                g_PlayerHeadCache[cacheKey].texture = texture;
                return texture;
            }
        }

        QueuePlayerHeadDownload(playerName);
        return nullptr;
    }

    void DrawFallbackPlayerHead(ImDrawList* drawList, ImFont* font, float fontSize, const ImVec2& min, const ImVec2& max, const std::string& playerName)
    {
        if (!drawList) {
            return;
        }

        drawList->AddRectFilled(min, max, IM_COL32(215, 217, 221, 255), 5.0f);
        drawList->AddRect(min, max, IM_COL32(184, 188, 194, 255), 5.0f, 0, 1.0f);

        if (playerName.empty()) {
            return;
        }

        const char initial[2] = { static_cast<char>(std::toupper(static_cast<unsigned char>(playerName[0]))), '\0' };
        const ImVec2 textSize = font ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, initial) : ImGui::CalcTextSize(initial);
        drawList->AddText(
            font,
            fontSize,
            ImVec2(min.x + ((max.x - min.x) - textSize.x) * 0.5f, min.y + ((max.y - min.y) - textSize.y) * 0.5f),
            IM_COL32(72, 76, 84, 255),
            initial);
    }

    void DrawPlayerHeadPreview(ImDrawList* drawList, ID3D11Device* device, ImFont* font, float fontSize, const std::string& playerName, const ImVec2& min, const ImVec2& max)
    {
        if (!drawList) {
            return;
        }

        ID3D11ShaderResourceView* texture = GetPlayerHeadTexture(device, playerName);
        if (!texture) {
            DrawFallbackPlayerHead(drawList, font, fontSize, min, max, playerName);
            return;
        }

        drawList->AddRectFilled(min, max, IM_COL32(225, 227, 231, 255), 5.0f);
        drawList->AddImageRounded(reinterpret_cast<ImTextureID>(texture), min, max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, 5.0f, ImDrawFlags_RoundCornersAll);
        drawList->AddRect(min, max, IM_COL32(184, 188, 194, 255), 5.0f, 0, 1.0f);
    }

    struct TargetNameAutocompleteState
    {
        std::string basePrefix;
        std::vector<std::string> matches;
        int matchIndex = -1;
    };

    TargetNameAutocompleteState g_TargetNameAutocompleteState;

    void ResetTargetNameAutocomplete()
    {
        g_TargetNameAutocompleteState = {};
    }

    bool CaseInsensitiveEquals(const std::string& lhs, const std::string& rhs)
    {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        for (size_t i = 0; i < lhs.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(lhs[i])) != std::tolower(static_cast<unsigned char>(rhs[i]))) {
                return false;
            }
        }

        return true;
    }

    bool CaseInsensitiveLess(const std::string& lhs, const std::string& rhs)
    {
        return std::lexicographical_compare(
            lhs.begin(),
            lhs.end(),
            rhs.begin(),
            rhs.end(),
            [](unsigned char left, unsigned char right) {
                return std::tolower(left) < std::tolower(right);
            });
    }

    bool StartsWithInsensitive(const std::string& value, const std::string& prefix)
    {
        if (prefix.empty() || prefix.size() > value.size()) {
            return false;
        }

        for (size_t index = 0; index < prefix.size(); ++index) {
            if (std::tolower(static_cast<unsigned char>(value[index])) != std::tolower(static_cast<unsigned char>(prefix[index]))) {
                return false;
            }
        }

        return true;
    }

    std::vector<std::string> CollectTargetAutocompleteMatches(const ModuleConfig* config, const std::string& prefix)
    {
        std::vector<std::string> matches;
        if (!config) {
            return matches;
        }

        const int maxPlayers = static_cast<int>(sizeof(config->Target.m_OnlinePlayerNames) / sizeof(config->Target.m_OnlinePlayerNames[0]));
        const int playerCount = (std::clamp)(config->Target.m_OnlinePlayersCount, 0, maxPlayers);
        for (int index = 0; index < playerCount; ++index) {
            const char* playerName = config->Target.m_OnlinePlayerNames[index];
            if (!playerName || !playerName[0]) {
                continue;
            }

            std::string candidate(playerName);
            if (prefix.empty() || StartsWithInsensitive(candidate, prefix)) {
                matches.push_back(std::move(candidate));
            }
        }

        std::sort(matches.begin(), matches.end(), CaseInsensitiveLess);
        matches.erase(std::unique(matches.begin(), matches.end(), [](const std::string& lhs, const std::string& rhs) {
            return CaseInsensitiveEquals(lhs, rhs);
        }), matches.end());
        return matches;
    }

    int TargetNameInputCallback(ImGuiInputTextCallbackData* data)
    {
        if (!data || data->EventFlag != ImGuiInputTextFlags_CallbackCompletion) {
            return 0;
        }

        const auto* config = static_cast<const ModuleConfig*>(data->UserData);
        if (!config || !data->Buf) {
            return 0;
        }

        const std::string currentText = data->Buf;
        if (currentText.empty()) {
            ResetTargetNameAutocomplete();
            return 0;
        }

        const bool isCyclingExistingMatch =
            g_TargetNameAutocompleteState.matchIndex >= 0 &&
            g_TargetNameAutocompleteState.matchIndex < static_cast<int>(g_TargetNameAutocompleteState.matches.size()) &&
            currentText == g_TargetNameAutocompleteState.matches[g_TargetNameAutocompleteState.matchIndex];

        const bool reverseCycle = ImGui::GetIO().KeyShift;
        if (!isCyclingExistingMatch) {
            g_TargetNameAutocompleteState.basePrefix = currentText;
            g_TargetNameAutocompleteState.matches = CollectTargetAutocompleteMatches(config, currentText);
            if (g_TargetNameAutocompleteState.matches.empty()) {
                ResetTargetNameAutocomplete();
                return 0;
            }

            g_TargetNameAutocompleteState.matchIndex = reverseCycle
                ? static_cast<int>(g_TargetNameAutocompleteState.matches.size()) - 1
                : 0;
        } else {
            auto refreshedMatches = CollectTargetAutocompleteMatches(config, g_TargetNameAutocompleteState.basePrefix);
            if (refreshedMatches.empty()) {
                ResetTargetNameAutocomplete();
                return 0;
            }

            if (refreshedMatches != g_TargetNameAutocompleteState.matches) {
                auto currentIt = std::find(refreshedMatches.begin(), refreshedMatches.end(), currentText);
                g_TargetNameAutocompleteState.matchIndex = currentIt != refreshedMatches.end()
                    ? static_cast<int>(std::distance(refreshedMatches.begin(), currentIt))
                    : 0;
                g_TargetNameAutocompleteState.matches = std::move(refreshedMatches);
            }

            const int direction = reverseCycle ? -1 : 1;
            const int matchCount = static_cast<int>(g_TargetNameAutocompleteState.matches.size());
            g_TargetNameAutocompleteState.matchIndex =
                (g_TargetNameAutocompleteState.matchIndex + direction + matchCount) % matchCount;
        }

        const std::string& replacement = g_TargetNameAutocompleteState.matches[g_TargetNameAutocompleteState.matchIndex];
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, replacement.c_str());
        data->SelectAll();
        return 0;
    }

    std::vector<size_t> GetVisibleOptionOrder(const std::shared_ptr<Module>& mod)
    {
        std::vector<size_t> order;
        if (!mod) {
            return order;
        }

        const auto& options = mod->GetOptions();
        for (size_t optionIndex = 0; optionIndex < options.size(); ++optionIndex) {
            if (mod->ShouldRenderOption(optionIndex)) {
                order.push_back(optionIndex);
            }
        }

        std::stable_sort(order.begin(), order.end(), [&](size_t lhs, size_t rhs) {
            const auto& left = options[lhs];
            const auto& right = options[rhs];
            const int leftOrder = left.displayOrder >= 0 ? left.displayOrder : static_cast<int>(lhs * 100);
            const int rightOrder = right.displayOrder >= 0 ? right.displayOrder : static_cast<int>(rhs * 100);
            if (leftOrder != rightOrder) {
                return leftOrder < rightOrder;
            }
            return lhs < rhs;
        });

        return order;
    }

    float GetModuleBodyFooterSpacing(const std::shared_ptr<Module>& mod, const std::vector<size_t>& visibleOrder)
    {
        (void)mod;
        return visibleOrder.empty() ? 0.0f : 14.0f;
    }

    std::filesystem::path ResolveModuleImagePath(const std::string& relativePath)
    {
        if (relativePath.empty()) {
            return {};
        }

        const std::filesystem::path direct(relativePath);
        if (direct.is_absolute() && std::filesystem::exists(direct)) {
            return direct;
        }

        wchar_t modulePath[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0) {
            return {};
        }

        auto current = std::filesystem::path(modulePath).parent_path();
        while (!current.empty()) {
            const auto candidate = current / relativePath;
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }

            const auto parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }

        return {};
    }

    std::string FormatModuleName(const std::string& name, bool spacedModules)
    {
        if (!spacedModules || name.empty()) {
            return name;
        }

        std::string result;
        result.reserve(name.size() + 4);

        for (size_t i = 0; i < name.size(); ++i) {
            const unsigned char current = static_cast<unsigned char>(name[i]);
            if (i > 0 && std::isupper(current)) {
                const unsigned char previous = static_cast<unsigned char>(name[i - 1]);
                const bool followsLowercase = std::islower(previous) != 0;
                const bool breaksAcronym = std::isupper(previous) != 0 &&
                    (i + 1) < name.size() &&
                    std::islower(static_cast<unsigned char>(name[i + 1])) != 0;
                if (followsLowercase || breaksAcronym) {
                    result.push_back(' ');
                }
            }

            result.push_back(static_cast<char>(current));
        }

        return result;
    }

    void GetRiseRGB(int offset, float& r, float& g, float& b)
    {
        const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const float phase = 0.5f + 0.5f * sinf(seconds * 1.35f + offset * 0.5f);
        const float hue = 0.58f - phase * 0.18f;
        ImGui::ColorConvertHSVtoRGB(hue, 0.72f, 0.95f, r, g, b);
    }

    void GetTesseractRGB(int offset, float& r, float& g, float& b)
    {
        const double tMs = (double)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const float goldenRatio = 0.618033988749895f;
        const float baseHue = (float)fmod(tMs / 2000.0, 1.0);
        const float hueOffset = fmodf(offset * goldenRatio, 1.0f);
        const float hue = fmodf(baseHue + hueOffset, 1.0f);
        ImGui::ColorConvertHSVtoRGB(hue, 0.75f, 0.9f, r, g, b);
    }

    void GetTesseractHeaderRGB(int offset, float& r, float& g, float& b)
    {
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const float state = ceilf((float)(millis + offset) / 20.0f);
        const float hue = fmodf(state, 360.0f) / 360.0f;
        ImGui::ColorConvertHSVtoRGB(hue, 0.7f, 0.8f, r, g, b);
    }

    void ApplyRoundedWindowRegion(HWND hwnd, int width, int height, int radius)
    {
        if (!hwnd || width <= 0 || height <= 0)
            return;

        HRGN region = CreateRectRgn(0, 0, width + 1, height + 1);
        if (region) {
            SetWindowRgn(hwnd, region, TRUE);
        }
    }

    void DrawWindowBase(ImDrawList* drawList, const ImVec2& origin, float width, float height)
    {
        const ImVec2 surfaceMin(origin.x + 0.5f, origin.y + 0.5f);
        const ImVec2 surfaceMax(origin.x + width - 0.5f, origin.y + height - 0.5f);
        drawList->AddRectFilled(surfaceMin, surfaceMax, color::GetBackgroundU32(), 0.0f);
    }

    // --- Topographic noise & contour system (Kali Linux style) ---
    static float topoHash(int x, int y) {
        int h = x * 374761393 + y * 668265263;
        h = (h ^ (h >> 13)) * 1274126177;
        h = h ^ (h >> 16);
        return (h & 0x7fffffff) / (float)0x7fffffff;
    }

    static float topoSmoothstep(float t) {
        return t * t * (3.0f - 2.0f * t);
    }

    static float topoNoise2D(float x, float y) {
        int ix = (int)floorf(x);
        int iy = (int)floorf(y);
        float fx = x - ix;
        float fy = y - iy;
        fx = topoSmoothstep(fx);
        fy = topoSmoothstep(fy);
        float n00 = topoHash(ix, iy);
        float n10 = topoHash(ix + 1, iy);
        float n01 = topoHash(ix, iy + 1);
        float n11 = topoHash(ix + 1, iy + 1);
        float nx0 = n00 + (n10 - n00) * fx;
        float nx1 = n01 + (n11 - n01) * fx;
        return nx0 + (nx1 - nx0) * fy;
    }

    static float topoFbm(float x, float y) {
        float value = 0.0f;
        float amp = 0.5f;
        float freq = 1.0f;
        for (int i = 0; i < 5; i++) {
            value += amp * topoNoise2D(x * freq, y * freq);
            amp *= 0.5f;
            freq *= 2.0f;
        }
        return value;
    }

    void DrawTopographicBackground(ImDrawList* drawList, const ImVec2& origin, float width, float height, float /*elapsed*/, float alphaMultiplier = 1.0f) {
        drawList->PushClipRect(origin, ImVec2(origin.x + width, origin.y + height), true);

        const int gridW = 120;
        const int gridH = 90;
        const float cellW = width / (float)gridW;
        const float cellH = height / (float)gridH;
        const float scale = 0.04f;
        const int numLevels = 14;
        const int lineAlpha = static_cast<int>(80.0f * (std::clamp)(alphaMultiplier, 0.0f, 1.0f));
        const ImU32 lineColor = IM_COL32(200, 200, 200, lineAlpha);

        // sample noise grid
        static float grid[121][91];
        static bool gridReady = false;
        static float cachedW = 0, cachedH = 0;
        if (!gridReady || cachedW != width || cachedH != height) {
            for (int gy = 0; gy <= gridH; gy++) {
                for (int gx = 0; gx <= gridW; gx++) {
                    float nx = gx * scale;
                    float ny = gy * scale;
                    grid[gx][gy] = topoFbm(nx, ny);
                }
            }
            gridReady = true;
            cachedW = width;
            cachedH = height;
        }

        // marching squares for each contour level
        for (int lv = 1; lv < numLevels; lv++) {
            float threshold = (float)lv / (float)numLevels;

            for (int gy = 0; gy < gridH; gy++) {
                for (int gx = 0; gx < gridW; gx++) {
                    float v00 = grid[gx][gy];
                    float v10 = grid[gx + 1][gy];
                    float v01 = grid[gx][gy + 1];
                    float v11 = grid[gx + 1][gy + 1];

                    int config = 0;
                    if (v00 >= threshold) config |= 1;
                    if (v10 >= threshold) config |= 2;
                    if (v11 >= threshold) config |= 4;
                    if (v01 >= threshold) config |= 8;

                    if (config == 0 || config == 15) continue;

                    float x0 = origin.x + gx * cellW;
                    float y0 = origin.y + gy * cellH;

                    // interpolation helpers for edge midpoints
                    auto lerpEdge = [&](float va, float vb, float ax, float ay, float bx, float by) -> ImVec2 {
                        float t = (threshold - va) / (vb - va + 1e-6f);
                        t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
                        return ImVec2(ax + (bx - ax) * t, ay + (by - ay) * t);
                    };

                    // edge midpoints: top, right, bottom, left
                    ImVec2 eT = lerpEdge(v00, v10, x0, y0, x0 + cellW, y0);
                    ImVec2 eR = lerpEdge(v10, v11, x0 + cellW, y0, x0 + cellW, y0 + cellH);
                    ImVec2 eB = lerpEdge(v01, v11, x0, y0 + cellH, x0 + cellW, y0 + cellH);
                    ImVec2 eL = lerpEdge(v00, v01, x0, y0, x0, y0 + cellH);

                    // draw line segments based on marching squares config
                    switch (config) {
                    case 1: case 14: drawList->AddLine(eT, eL, lineColor, 0.8f); break;
                    case 2: case 13: drawList->AddLine(eT, eR, lineColor, 0.8f); break;
                    case 3: case 12: drawList->AddLine(eL, eR, lineColor, 0.8f); break;
                    case 4: case 11: drawList->AddLine(eR, eB, lineColor, 0.8f); break;
                    case 6: case 9:  drawList->AddLine(eT, eB, lineColor, 0.8f); break;
                    case 7: case 8:  drawList->AddLine(eL, eB, lineColor, 0.8f); break;
                    case 5:
                        drawList->AddLine(eT, eR, lineColor, 0.8f);
                        drawList->AddLine(eL, eB, lineColor, 0.8f);
                        break;
                    case 10:
                        drawList->AddLine(eT, eL, lineColor, 0.8f);
                        drawList->AddLine(eR, eB, lineColor, 0.8f);
                        break;
                    }
                }
            }
        }

        drawList->PopClipRect();
    }

    void DrawGlassCard(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float elapsed, float rounding = 28.0f)
    {
        const float width = max.x - min.x;
        const float height = max.y - min.y;
        if (width <= 0.0f || height <= 0.0f)
            return;

        drawList->AddRectFilled(min, max, color::GetPanelU32(0.72f), rounding);
        drawList->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, min.y + height * 0.42f), color::GetGlassHighlightU32(0.18f), rounding, ImDrawFlags_RoundCornersTop);
        drawList->AddRect(min, max, color::GetBorderU32(0.40f), rounding, 0, 1.0f);
        drawList->AddRect(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, max.y - 1.0f), color::GetGlassHighlightU32(0.26f), rounding - 1.0f, 0, 1.0f);
    }

    void DrawWindowMoveBlurOverlay(
        ImDrawList* drawList,
        const ImVec2& origin,
        float width,
        float height,
        float time,
        float overlayAlpha,
        ImFont* headlineFont,
        float headlineFontSize)
    {
        if (!drawList || width <= 0.0f || height <= 0.0f || overlayAlpha <= 0.001f) {
            return;
        }

        const ImVec2 max(origin.x + width, origin.y + height);
        const float easedAlpha = overlayAlpha * overlayAlpha * (3.0f - 2.0f * overlayAlpha);
        const float pulse = 0.5f + 0.5f * std::sinf(time * 3.2f);

        drawList->PushClipRect(origin, max, true);
        drawList->AddRectFilled(origin, max, IM_COL32(255, 255, 255, static_cast<int>(255.0f * easedAlpha)), 0.0f);
        DrawTopographicBackground(drawList, origin, width, height, 0.0f, easedAlpha);

        ImFont* font = headlineFont ? headlineFont : ImGui::GetFont();
        const float fontSize = headlineFontSize > 0.0f ? headlineFontSize : ImGui::GetFontSize();
        const char* draggingLabel = "Dragging...";
        const ImVec2 textSize = CalcTextSizeWithFont(font, draggingLabel, fontSize);
        const ImVec2 textPos(origin.x + (width - textSize.x) * 0.5f, origin.y + (height - textSize.y) * 0.5f - 2.0f);
        drawList->AddText(
            font,
            fontSize,
            ImVec2(textPos.x, textPos.y + 2.0f),
            IM_COL32(255, 255, 255, static_cast<int>(120.0f * easedAlpha)),
            draggingLabel);
        drawList->AddText(
            font,
            fontSize,
            textPos,
            IM_COL32(20, 22, 28, static_cast<int>(255.0f * easedAlpha)),
            draggingLabel);

        if (pulse > 0.22f) {
            const float cursorX = textPos.x + textSize.x + 8.0f;
            drawList->AddRectFilled(
                ImVec2(cursorX, textPos.y + 2.0f),
                ImVec2(cursorX + 2.5f, textPos.y + 2.0f + fontSize * 0.92f),
                IM_COL32(20, 22, 28, static_cast<int>(235.0f * easedAlpha)),
                1.0f);
        }

        drawList->PopClipRect();
    }

    bool DrawRoundedActionButton(
        ImDrawList* drawList,
        const char* id,
        const char* label,
        const ImVec2& pos,
        const ImVec2& size,
        float rounding,
        ImU32 baseColor,
        ImU32 hoverColor,
        ImU32 activeColor,
        ImU32 borderColor,
        ImU32 textColor,
        ImFont* font,
        float fontSize,
        bool enabled)
    {
        if (!drawList || !id || !label || size.x <= 0.0f || size.y <= 0.0f) {
            return false;
        }

        ImGui::SetCursorScreenPos(pos);
        const bool pressed = ImGui::InvisibleButton(id, size) && enabled;
        const bool hovered = enabled && ImGui::IsItemHovered();
        const bool held = enabled && ImGui::IsItemActive();
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        ImU32 fillColor = baseColor;
        if (!enabled) {
            fillColor = IM_COL32(150, 156, 167, 190);
            borderColor = IM_COL32(130, 136, 146, 120);
            textColor = IM_COL32(245, 246, 250, 215);
        } else if (held) {
            fillColor = activeColor;
        } else if (hovered) {
            fillColor = hoverColor;
        }

        const ImVec2 min = pos;
        const ImVec2 max(pos.x + size.x, pos.y + size.y);
        drawList->AddRectFilled(min, max, fillColor, rounding);
        drawList->AddRectFilled(
            ImVec2(min.x + 1.0f, min.y + 1.0f),
            ImVec2(max.x - 1.0f, max.y - 1.0f),
            IM_COL32(255, 255, 255, enabled ? (hovered ? 18 : 10) : 6),
            rounding - 1.0f);
        drawList->AddRect(min, max, borderColor, rounding, 0, 1.0f);

        ImFont* textFont = font ? font : ImGui::GetFont();
        const float textFontSize = fontSize > 0.0f ? fontSize : ImGui::GetFontSize();
        const ImVec2 textSize = CalcTextSizeWithFont(textFont, label, textFontSize);
        const ImVec2 textPos(
            min.x + (size.x - textSize.x) * 0.5f,
            min.y + (size.y - textSize.y) * 0.5f - 1.0f);
        drawList->AddText(textFont, textFontSize, textPos, textColor, label);
        return pressed;
    }

    void DrawCloseButton(float width, bool& running)
    {
        ImGui::SetCursorPos(ImVec2(width - 60.0f, 26.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, color::GetPanelVec4(0.76f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color::GetPanelHoverVec4(0.84f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, color::GetPanelActiveVec4(0.92f));
        ImGui::PushStyleColor(ImGuiCol_Text, color::GetStrongTextVec4());
        if (ImGui::Button("X", ImVec2(34.0f, 34.0f))) {
            running = false;
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
    }

    float EaseBreathing(float t)
    {
        return 0.5f + 0.5f * sinf(t);
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static Screen* g_Instance = nullptr;

Screen::Screen(float width, float height) 
    : m_Width(width), m_Height(height) {
    g_Instance = this;
}

Screen::~Screen() {
    Shutdown();
    g_Instance = nullptr;
}

LRESULT WINAPI Screen::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_Instance) {
        return g_Instance->HandleWndProc(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Screen::HandleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    constexpr LONG kWindowDragHeight = 14;
    auto isWithinDragStrip = [&](LPARAM mouseParam) -> bool {
        const LONG x = static_cast<LONG>(static_cast<SHORT>(LOWORD(mouseParam)));
        const LONG y = static_cast<LONG>(static_cast<SHORT>(HIWORD(mouseParam)));
        return x >= 0 &&
            y >= 0 &&
            x < static_cast<LONG>(m_Width) &&
            y < kWindowDragHeight;
    };

    switch (msg) {
    case WM_NCHITTEST: {
        const LRESULT defaultHit = DefWindowProcW(hwnd, msg, wParam, lParam);
        if (defaultHit != HTCLIENT) {
            return defaultHit;
        }
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN:
        if (isWithinDragStrip(lParam)) {
            RECT windowRect = {};
            POINT cursorPoint = {};
            GetWindowRect(hwnd, &windowRect);
            GetCursorPos(&cursorPoint);

            m_IsManualWindowDrag = true;
            m_IsWindowMoveActive = true;
            m_WindowDragOffset.x = cursorPoint.x - windowRect.left;
            m_WindowDragOffset.y = cursorPoint.y - windowRect.top;
            SetCapture(hwnd);
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (m_IsManualWindowDrag) {
            POINT cursorPoint = {};
            GetCursorPos(&cursorPoint);

            const int nextX = cursorPoint.x - m_WindowDragOffset.x;
            const int nextY = cursorPoint.y - m_WindowDragOffset.y;
            SetWindowPos(
                hwnd,
                nullptr,
                nextX,
                nextY,
                0,
                0,
                SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOZORDER);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (m_IsManualWindowDrag) {
            m_IsManualWindowDrag = false;
            m_IsWindowMoveActive = false;
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            return 0;
        }
        break;
    case WM_ENTERSIZEMOVE:
        m_IsWindowMoveActive = true;
        return 0;
    case WM_EXITSIZEMOVE:
        m_IsManualWindowDrag = false;
        m_IsWindowMoveActive = false;
        return 0;
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        m_IsManualWindowDrag = false;
        m_IsWindowMoveActive = false;
        break;
    }

    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return true;
    }
    
    switch (msg) {
    case WM_SIZE:
        if (m_Device && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            m_SwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
            ApplyRoundedWindowRegion(hwnd, LOWORD(lParam), HIWORD(lParam), 42);
        }
        if (wParam == SIZE_MINIMIZED) {
            m_Minimized = true;
        } else {
            m_Minimized = false;
        }
        return 0;
        
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_CLOSE) {
            m_Running = false;
            return 0;
        }
        if ((wParam & 0xfff0) == SC_KEYMENU) {
            return 0;
        }
        break;
        
    case WM_CLOSE:
    case WM_DESTROY:
        m_IsWindowMoveActive = false;
        m_Running = false;
        PostQuitMessage(0);
        return 0;
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool Screen::CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    
    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_SwapChain, &m_Device, &featureLevel, &m_Context) != S_OK) {
        return false;
    }
    
    CreateRenderTarget();
    return true;
}

void Screen::CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (m_SwapChain) { m_SwapChain->Release(); m_SwapChain = nullptr; }
    if (m_Context) { m_Context->Release(); m_Context = nullptr; }
    if (m_Device) { m_Device->Release(); m_Device = nullptr; }
}

void Screen::CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    m_SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        m_Device->CreateRenderTargetView(backBuffer, nullptr, &m_RenderTarget);
        backBuffer->Release();
    }
}

void Screen::CleanupRenderTarget() {
    if (m_RenderTarget) {
        m_RenderTarget->Release();
        m_RenderTarget = nullptr;
    }
}

void Screen::SetupImGuiStyle() {
    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4* colors = style->Colors;

    style->WindowRounding = 0.0f;
    style->FrameRounding = 0.0f;
    style->PopupRounding = 0.0f;
    style->GrabRounding = 0.0f;
    style->TabRounding = 0.0f;
    style->ChildRounding = 0.0f;
    style->ScrollbarRounding = 0.0f;

    style->IndentSpacing = 12.0f;
    style->ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style->ItemSpacing = ImVec2(10.0f, 10.0f);
    style->FramePadding = ImVec2(14.0f, 9.0f);
    style->WindowPadding = ImVec2(18.0f, 18.0f);

    style->WindowBorderSize = 0.0f;
    style->ChildBorderSize = 0.0f;
    style->FrameBorderSize = 1.0f;
    style->PopupBorderSize = 1.0f;

    colors[ImGuiCol_Text] = color::GetStrongTextVec4();
    colors[ImGuiCol_TextDisabled] = color::GetMutedTextVec4();
    colors[ImGuiCol_WindowBg] = color::GetBackgroundVec4();
    colors[ImGuiCol_ChildBg] = color::GetPanelVec4(0.44f);
    colors[ImGuiCol_PopupBg] = color::GetPanelVec4(0.84f);
    colors[ImGuiCol_Border] = color::GetBorderVec4(0.34f);
    colors[ImGuiCol_BorderShadow] = color::GetTransparentVec4();
    colors[ImGuiCol_FrameBg] = color::GetFieldBgVec4(0.68f);
    colors[ImGuiCol_FrameBgHovered] = color::GetFieldHoverVec4(0.78f);
    colors[ImGuiCol_FrameBgActive] = color::GetFieldActiveVec4(0.88f);
    colors[ImGuiCol_TitleBg] = color::GetBackgroundVec4();
    colors[ImGuiCol_TitleBgActive] = color::GetBackgroundVec4();
    colors[ImGuiCol_TitleBgCollapsed] = color::GetBackgroundVec4(0.95f);
    colors[ImGuiCol_MenuBarBg] = color::GetPanelVec4(0.32f);
    colors[ImGuiCol_ScrollbarBg] = color::GetTransparentVec4();
    colors[ImGuiCol_ScrollbarGrab] = color::GetPanelVec4(0.72f);
    colors[ImGuiCol_ScrollbarGrabHovered] = color::GetPanelHoverVec4(0.82f);
    colors[ImGuiCol_ScrollbarGrabActive] = color::GetPanelActiveVec4(0.92f);
    colors[ImGuiCol_CheckMark] = color::GetStrongTextVec4();
    colors[ImGuiCol_SliderGrab] = color::GetStrongTextVec4();
    colors[ImGuiCol_SliderGrabActive] = color::GetModuleAltTextVec4();
    colors[ImGuiCol_Button] = color::GetPanelVec4(0.72f);
    colors[ImGuiCol_ButtonHovered] = color::GetPanelHoverVec4(0.82f);
    colors[ImGuiCol_ButtonActive] = color::GetPanelActiveVec4(0.90f);
    colors[ImGuiCol_Header] = color::GetPanelHoverVec4(0.64f);
    colors[ImGuiCol_HeaderHovered] = color::GetPanelActiveVec4(0.76f);
    colors[ImGuiCol_HeaderActive] = color::GetPanelActiveVec4(0.88f);
    colors[ImGuiCol_Separator] = color::GetBorderVec4(0.38f);
    colors[ImGuiCol_ResizeGrip] = color::GetTransparentVec4();
    colors[ImGuiCol_ResizeGripHovered] = color::GetPanelHoverVec4(0.70f);
    colors[ImGuiCol_ResizeGripActive] = color::GetPanelActiveVec4(0.80f);
    colors[ImGuiCol_Tab] = color::GetPanelVec4(0.58f);
    colors[ImGuiCol_TabHovered] = color::GetPanelHoverVec4(0.76f);
    colors[ImGuiCol_TabActive] = color::GetPanelActiveVec4(0.84f);
    colors[ImGuiCol_TabUnfocused] = color::GetPanelVec4(0.44f);
    colors[ImGuiCol_TabUnfocusedActive] = color::GetPanelHoverVec4(0.58f);
    colors[ImGuiCol_TextSelectedBg] = color::GetGlassStrongVec4(0.35f);
    colors[ImGuiCol_ModalWindowDimBg] = color::GetBackgroundVec4(0.35f);
}

bool Screen::LoadTextureFromMemory(const unsigned char* data, unsigned int dataSize, ID3D11ShaderResourceView** outSrv, int* outW, int* outH, bool invertRGB) {
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(data, dataSize, &width, &height, &channels, 4);
    if (!pixels) return false;

    if (invertRGB) {
        int totalPixels = width * height;
        for (int p = 0; p < totalPixels; p++) {
            pixels[p * 4 + 0] = 255 - pixels[p * 4 + 0]; // R
            pixels[p * 4 + 1] = 255 - pixels[p * 4 + 1]; // G
            pixels[p * 4 + 2] = 255 - pixels[p * 4 + 2]; // B
        }
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subResource = {};
    subResource.pSysMem = pixels;
    subResource.SysMemPitch = width * 4;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = m_Device->CreateTexture2D(&desc, &subResource, &texture);
    stbi_image_free(pixels);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = m_Device->CreateShaderResourceView(texture, &srvDesc, outSrv);
    texture->Release();
    if (FAILED(hr)) return false;

    if (outW) *outW = width;
    if (outH) *outH = height;
    return true;
}

void Screen::LoadIconTextures() {
    int w, h;
    LoadTextureFromMemory(icons::sword_icon_data, icons::sword_icon_data_size, &m_IconCombat, &w, &h, true);
    LoadTextureFromMemory(icons::running_icon_data, icons::running_icon_data_size, &m_IconMovement, &w, &h, true);
    LoadTextureFromMemory(icons::eye_icon_data, icons::eye_icon_data_size, &m_IconVisuals, &w, &h, true);
    LoadTextureFromMemory(icons::settings_icon_data, icons::settings_icon_data_size, &m_IconSettings, &w, &h, true);
    const auto infoLampPath = ResolveModuleImagePath("Light-Bulb-256.png");
    if (!infoLampPath.empty()) {
        m_InfoLampTexture = CreateTextureFromFile(m_Device, infoLampPath);
    }
    m_IconW = w;
    m_IconH = h;
}

void Screen::ReleaseIconTextures() {
    if (m_IconCombat) { m_IconCombat->Release(); m_IconCombat = nullptr; }
    if (m_IconMovement) { m_IconMovement->Release(); m_IconMovement = nullptr; }
    if (m_IconVisuals) { m_IconVisuals->Release(); m_IconVisuals = nullptr; }
    if (m_IconSettings) { m_IconSettings->Release(); m_IconSettings = nullptr; }
    if (m_InfoLampTexture) { m_InfoLampTexture->Release(); m_InfoLampTexture = nullptr; }
}

bool Screen::Initialize() {
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    m_Wc = { sizeof(m_Wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"OpenCommunity", nullptr };
    RegisterClassExW(&m_Wc);
    
    int screenX = GetSystemMetrics(SM_CXSCREEN);
    int screenY = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenX - static_cast<int>(m_Width)) / 2;
    int y = (screenY - static_cast<int>(m_Height)) / 2;
    
    m_Hwnd = CreateWindowExW(
        0,
        m_Wc.lpszClassName,
        L"OpenCommunity",
        WS_POPUP,
        x, y,
        static_cast<int>(m_Width), static_cast<int>(m_Height),
        nullptr, nullptr, m_Wc.hInstance, nullptr
    );
    
    if (!m_Hwnd) return false;
    ApplyRoundedWindowRegion(m_Hwnd, static_cast<int>(m_Width), static_cast<int>(m_Height), 42);
    
    if (!CreateDeviceD3D(m_Hwnd)) {
        DestroyWindow(m_Hwnd);
        UnregisterClassW(m_Wc.lpszClassName, m_Wc.hInstance);
        return false;
    }
    
    ShowWindow(m_Hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_Hwnd);
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        m_FontBody = io.Fonts->AddFontFromMemoryTTF((void*)fonts::inter_regular_data, fonts::inter_regular_size, 17.0f, &cfg);
        cfg.FontDataOwnedByAtlas = false;
        m_FontBold = io.Fonts->AddFontFromMemoryTTF((void*)fonts::inter_bold_data, fonts::inter_bold_size, 17.0f, &cfg);
        cfg.FontDataOwnedByAtlas = false;
        m_FontTitle = io.Fonts->AddFontFromMemoryTTF((void*)fonts::inter_bold_data, fonts::inter_bold_size, 32.0f, &cfg);
        cfg.FontDataOwnedByAtlas = false;
        m_FontHero = io.Fonts->AddFontFromMemoryTTF((void*)fonts::inter_bold_data, fonts::inter_bold_size, 110.0f, &cfg);
        cfg.FontDataOwnedByAtlas = false;
        m_FontBodyLarge = io.Fonts->AddFontFromMemoryTTF((void*)fonts::inter_regular_data, fonts::inter_regular_size, 32.0f, &cfg);
        cfg.FontDataOwnedByAtlas = false;
        m_FontBodyMed = io.Fonts->AddFontFromMemoryTTF((void*)fonts::inter_regular_data, fonts::inter_regular_size, 22.0f, &cfg);
        cfg.FontDataOwnedByAtlas = false;
        m_FontBoldMed = io.Fonts->AddFontFromMemoryTTF((void*)fonts::inter_bold_data, fonts::inter_bold_size, 22.0f, &cfg);
        cfg.FontDataOwnedByAtlas = false;
        m_FontOverlayRegular = io.Fonts->AddFontFromMemoryTTF((void*)fonts::play_regular_data, fonts::play_regular_size, 16.0f, &cfg);
        cfg.FontDataOwnedByAtlas = false;
        m_FontOverlayBold = io.Fonts->AddFontFromMemoryTTF((void*)fonts::play_bold_data, fonts::play_bold_size, 16.0f, &cfg);
        if (m_FontBody) io.FontDefault = m_FontBody;
    }
    if (!io.FontDefault) {
        io.FontDefault = io.Fonts->AddFontDefault();
    }
    if (!m_FontBody) {
        m_FontBody = io.FontDefault;
    }
    if (!m_FontBold) {
        m_FontBold = m_FontBody;
    }
    if (!m_FontTitle) {
        m_FontTitle = m_FontBold;
    }
    if (!m_FontHero) {
        m_FontHero = m_FontTitle;
    }
    if (!m_FontOverlayRegular) {
        m_FontOverlayRegular = m_FontBody;
    }
    if (!m_FontOverlayBold) {
        m_FontOverlayBold = m_FontBold;
    }
    
    SetupImGuiStyle();
    
    ImGui_ImplWin32_Init(m_Hwnd);
    ImGui_ImplDX11_Init(m_Device, m_Context);
    
    LoadIconTextures();
    
    RegisterAllModules();
    FeatureManager::Get()->SyncAllFromConfig(Bridge::Get()->GetConfig());
    
    m_Initialized = true;
    return true;
}

void Screen::Shutdown() {
    if (!m_Initialized) return;
    
    ReleaseIconTextures();
    
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    CleanupDeviceD3D();
    DestroyWindow(m_Hwnd);
    UnregisterClassW(m_Wc.lpszClassName, m_Wc.hInstance);
    
    m_Initialized = false;
}

void Screen::RenderIntro() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(m_Width, m_Height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("OpenCommunityIntro", nullptr, flags)) {
        if (m_IntroStartTime < 0.0f) {
            m_IntroStartTime = static_cast<float>(ImGui::GetTime());
        }

        const float elapsed = static_cast<float>(ImGui::GetTime()) - m_IntroStartTime;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();

        DrawWindowBase(dl, wp, m_Width, m_Height);
        DrawTopographicBackground(dl, wp, m_Width, m_Height, elapsed);

        const float line1FontSize = 32.0f;
        const float line2FontSize = 22.0f;
        const float charsPerSec = 30.0f;

        const char* line1Full = "Hey! I'm Lopes.";
        const int line1Len = 17;
        const float line1Start = 0.3f;

        const char* line2Full = "Welcome to our open-source client, shaped by features the community really cares about.";
        const int line2Len = 88;
        const float line2Start = line1Start + (float)line1Len / charsPerSec + 0.3f;

        const int line1Chars = (int)fminf((float)line1Len, fmaxf(0.0f, (elapsed - line1Start) * charsPerSec));
        const int line2Chars = (int)fminf((float)line2Len, fmaxf(0.0f, (elapsed - line2Start) * charsPerSec));
        auto calcLineWidth = [&](auto* segs, int segCount, float fontSize, int totalChars) -> float {
            float w = 0.0f;
            int drawn = 0;
            for (int i = 0; i < segCount; i++) {
                int segLen = (int)strlen(segs[i].text);
                int segChars = (int)fminf((float)segLen, fmaxf(0.0f, (float)(totalChars - drawn)));
                if (segChars <= 0) break;
                char buf[128] = {};
                memcpy(buf, segs[i].text, segChars);
                buf[segChars] = '\0';
                ImFont* font = (fontSize > 35.0f) ? (segs[i].bold ? m_FontTitle : m_FontBodyLarge) : (segs[i].bold ? m_FontBoldMed : m_FontBodyMed);
                w += CalcTextSizeWithFont(font, buf, fontSize).x;
                drawn += segChars;
            }
            return w;
        };

        struct TextSegment { const char* text; bool bold; };

        const float line1Y = wp.y + 60.0f;
        const float line2Y = wp.y + 60.0f + line1FontSize + 30.0f;
        if (line1Chars > 0) {
            TextSegment segs1[] = {
                { "Hey! I'm ", false },
                { "Lopes", true },
                { ".", false }
            };

            float line1W = calcLineWidth(segs1, 3, line1FontSize, line1Chars);
            float curX = wp.x + (m_Width - line1W) * 0.5f;
            int charsDrawn = 0;
            for (auto& seg : segs1) {
                int segLen = (int)strlen(seg.text);
                int segChars = (int)fminf((float)segLen, fmaxf(0.0f, (float)(line1Chars - charsDrawn)));
                if (segChars <= 0) break;

                char buf[64] = {};
                memcpy(buf, seg.text, segChars);
                buf[segChars] = '\0';

                ImFont* font = seg.bold ? m_FontTitle : m_FontBodyLarge;
                dl->AddText(font, line1FontSize, ImVec2(curX, line1Y), color::GetAccentU32(1.0f), buf);

                ImVec2 fullSz = CalcTextSizeWithFont(font, buf, line1FontSize);
                curX += fullSz.x;
                charsDrawn += segChars;
            }

            if (line1Chars < line1Len) {
                float blinkAlpha = (sinf(elapsed * 6.0f) > 0.0f) ? 0.8f : 0.0f;
                dl->AddLine(ImVec2(curX + 2.0f, line1Y + 2.0f), ImVec2(curX + 2.0f, line1Y + line1FontSize - 2.0f), color::GetAccentU32(blinkAlpha), 1.5f);
            }
        }

        if (line2Chars > 0) {
            TextSegment segs2[] = {
                { "Welcome to our ", false },
                { "open-source", true },
                { " client, shaped by ", false },
                { "features", true },
                { " the community ", false },
                { "really cares about", true },
                { ".", false }
            };

            float line2W = calcLineWidth(segs2, 7, line2FontSize, line2Chars);
            float curX = wp.x + (m_Width - line2W) * 0.5f;
            int charsDrawn = 0;
            for (auto& seg : segs2) {
                int segLen = (int)strlen(seg.text);
                int segChars = (int)fminf((float)segLen, fmaxf(0.0f, (float)(line2Chars - charsDrawn)));
                if (segChars <= 0) break;

                char buf[128] = {};
                memcpy(buf, seg.text, segChars);
                buf[segChars] = '\0';

                ImFont* font = seg.bold ? m_FontBoldMed : m_FontBodyMed;
                dl->AddText(font, line2FontSize, ImVec2(curX, line2Y), color::GetAccentU32(1.0f), buf);

                ImVec2 fullSz = CalcTextSizeWithFont(font, buf, line2FontSize);
                curX += fullSz.x;
                charsDrawn += segChars;
            }

            if (line2Chars < line2Len) {
                float blinkAlpha = (sinf(elapsed * 6.0f) > 0.0f) ? 0.8f : 0.0f;
                dl->AddLine(ImVec2(curX + 2.0f, line2Y + 2.0f), ImVec2(curX + 2.0f, line2Y + line2FontSize - 2.0f), color::GetAccentU32(blinkAlpha), 1.5f);
            }
        }

        const float totalAnimTime = line2Start + (float)line2Len / charsPerSec + 0.5f;
        const bool showButton = elapsed >= totalAnimTime;
        if (showButton) {
            const char* btnLabel = "Let's go";
            ImGui::PushFont(m_FontTitle);
            
            const float breathTime = static_cast<float>(ImGui::GetTime()) * 2.5f;
            const float breathScale = 0.92f + 0.08f * sinf(breathTime);
            
            ImVec2 baseSize = ImGui::CalcTextSize(btnLabel);
            float scaledFontSize = 32.0f * breathScale;
            ImVec2 scaledSize = CalcTextSizeWithFont(m_FontTitle, btnLabel, scaledFontSize);
            
            float btnX = (m_Width - scaledSize.x) * 0.5f;
            float btnY = m_Height - scaledSize.y - 40.0f;

            ImGui::SetCursorPos(ImVec2((m_Width - baseSize.x) * 0.5f, m_Height - baseSize.y - 40.0f));
            if (ImGui::InvisibleButton("##letsgo", baseSize)) {
                m_State = AppState::InstanceChooser;
            }
            const bool hovered = ImGui::IsItemHovered();

            ImVec4 btnColor = hovered ? color::GetSecondaryTextVec4() : color::GetStrongTextVec4();
            dl->AddText(m_FontTitle, scaledFontSize, ImVec2(wp.x + btnX, wp.y + btnY), ImGui::ColorConvertFloat4ToU32(btnColor), btnLabel);
            
            ImGui::PopFont();
        }

        ImGui::End();
    }
}

void Screen::RenderInstanceChooser() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(m_Width, m_Height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("InstanceChooser", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();

        DrawWindowBase(dl, wp, m_Width, m_Height);
        DrawTopographicBackground(dl, wp, m_Width, m_Height, 0.0f);

        {
            const char* title = "Select Instance";
            ImGui::PushFont(m_FontBody);
            ImVec2 tSz = ImGui::CalcTextSize(title);
            ImGui::SetCursorPos(ImVec2((m_Width - tSz.x) * 0.5f, 60.f));
            ImGui::TextColored(color::GetStrongTextVec4(0.96f), "%s", title);
            ImGui::PopFont();
        }
        
        {
            const char* sub = "Choose your Minecraft window";
            ImGui::PushFont(m_FontBody);
            ImVec2 sSz = ImGui::CalcTextSize(sub);
            ImGui::SetCursorPos(ImVec2((m_Width - sSz.x) * 0.5f, 95.f));
            ImGui::TextColored(color::GetSecondaryTextVec4(0.92f), "%s", sub);
            ImGui::PopFont();
        }
        
        std::vector<std::pair<HWND, std::string>> instances;
        for (HWND hwnd = GetTopWindow(NULL); hwnd != NULL; hwnd = GetNextWindow(hwnd, GW_HWNDNEXT)) {
            if (!IsWindowVisible(hwnd)) continue;
            int length = GetWindowTextLength(hwnd);
            if (length == 0) continue;
            
            CHAR cName[MAX_PATH];
            GetClassNameA(hwnd, cName, _countof(cName));
            if (strcmp(cName, "LWJGL") != 0 && strcmp(cName, "GLFW30") != 0)
                continue;
            
            std::vector<char> title(length + 1);
            GetWindowTextA(hwnd, title.data(), length + 1);
            instances.push_back({ hwnd, std::string(title.data()) });
        }
        
        if (instances.empty()) {
            const char* w = "Waiting for Minecraft...";
            ImGui::PushFont(m_FontBody);
            ImVec2 wSz = ImGui::CalcTextSize(w);
            ImGui::SetCursorPos(ImVec2((m_Width - wSz.x) * 0.5f, m_Height * 0.5f - 10.f));
            ImGui::TextColored(color::GetModuleAltTextVec4(0.95f), "%s", w);
            ImGui::PopFont();
        } else {
            float btnY = m_Height * 0.5f + 30.f - (float)instances.size() * 23.f;
            for (const auto& inst : instances) {
                ImGui::SetCursorPos(ImVec2((m_Width - 320.f) * 0.5f, btnY));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, color::GetPanelVec4());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color::GetPanelHoverVec4());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, color::GetPanelActiveVec4());
                ImGui::PushStyleColor(ImGuiCol_Text, color::GetStrongTextVec4());
                
                if (ImGui::Button(inst.second.c_str(), ImVec2(320.f, 36.f))) {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(inst.first, &pid);
                    if (pid != 0) {
                        Singleton<ClientInfo>::Get()->m_TargetPid = pid;
                        m_State = AppState::Injecting;
                        m_InjectionDone = false;
                        m_InjectionFailed = false;
                        m_InjectionCompleteTime = -1.0f;
                        m_InjectionViewStartTime = -1.0f;
                        m_InterfaceTransitionStartTime = -1.0f;
                        m_InjectionStatus = "Injecting...";
                        
                        std::thread([this, pid]() {
                            Bridge::Get()->Initialize();

                            wchar_t exePath[MAX_PATH] = {};
                            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
                            std::wstring dllPath(exePath);
                            dllPath = dllPath.substr(0, dllPath.find_last_of(L"\\/") + 1);
                            dllPath += L"backdoor.dll";

                            bool success = Injector::Get()->InjectFromFile(pid, dllPath);
                            
                            if (success) {
                                m_InjectionDone = true;
                                m_InjectionStatus = "Injected";
                                m_InjectionCompleteTime = -1.0f;
                            } else {
                                m_InjectionStatus = "Injection failed! Check if backdoor.dll exists.";
                                m_InjectionFailed = true;
                                m_InjectionCompleteTime = -1.0f;
                                m_InterfaceTransitionStartTime = -1.0f;
                                Sleep(3000);
                                m_State = AppState::InstanceChooser;
                            }
                        }).detach();
                    }
                }
                
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();
                btnY += 46.f;
            }
        }
        
        ImGui::End();
    }
}

void Screen::DrawInjectionStatusText(
    ImDrawList* drawList,
    const ImVec2& windowPos,
    float alpha,
    float offsetY,
    float scale,
    const char* headline,
    float elapsed,
    bool showCursor,
    const char* detailText) {
    if (!drawList || alpha <= 0.0f || !headline) {
        return;
    }

    const float clampedAlpha = Clamp01(alpha);
    const float clampedScale = (std::max)(0.85f, scale);
    const float headlineFontSize = 72.0f * clampedScale;
    const float detailFontSize = 19.0f * clampedScale;
    const float charsVisible = (std::max)(0.0f, elapsed) * kInjectionTypewriterCharsPerSecond;
    const int totalChars = static_cast<int>(strlen(headline));
    const int visibleChars = (std::min)(totalChars, static_cast<int>(charsVisible));

    std::string visibleHeadline;
    if (visibleChars > 0) {
        visibleHeadline.assign(headline, headline + visibleChars);
    }

    ImFont* headlineFont = m_FontHero ? m_FontHero : (m_FontTitle ? m_FontTitle : ImGui::GetFont());
    ImFont* detailFont = m_FontBodyMed ? m_FontBodyMed : ImGui::GetFont();
    if (!headlineFont || !detailFont) {
        return;
    }

    const ImVec2 headlineSize = CalcTextSizeWithFont(headlineFont, visibleHeadline.c_str(), headlineFontSize);
    const bool hasDetailText = detailText && detailText[0] != '\0';
    const float verticalGap = hasDetailText ? (24.0f * clampedScale) : 0.0f;
    const float detailBlockHeight = hasDetailText ? detailFontSize : 0.0f;
    const float totalBlockHeight = headlineFontSize + verticalGap + detailBlockHeight;
    const float baseY = windowPos.y + ((m_Height - totalBlockHeight) * 0.5f) + offsetY;
    const float headlineX = windowPos.x + ((m_Width - headlineSize.x) * 0.5f);
    const ImU32 headlineColor = IM_COL32(12, 12, 12, static_cast<int>(255.0f * clampedAlpha));
    drawList->AddText(headlineFont, headlineFontSize, ImVec2(headlineX, baseY), headlineColor, visibleHeadline.c_str());

    if (showCursor) {
        const bool typingInProgress = visibleChars < totalChars;
        const float blink = std::sinf(static_cast<float>(ImGui::GetTime()) * kInjectionCursorBlinkSpeed);
        const float cursorAlpha = typingInProgress ? clampedAlpha : (blink > 0.0f ? clampedAlpha : 0.0f);
        if (cursorAlpha > 0.0f) {
            const float cursorX = headlineX + headlineSize.x + (8.0f * clampedScale);
            const float cursorTop = baseY + (10.0f * clampedScale);
            const float cursorBottom = baseY + headlineFontSize - (10.0f * clampedScale);
            drawList->AddLine(
                ImVec2(cursorX, cursorTop),
                ImVec2(cursorX, cursorBottom),
                IM_COL32(12, 12, 12, static_cast<int>(255.0f * cursorAlpha)),
                2.0f * clampedScale);
        }
    }

    if (hasDetailText) {
        const ImVec2 detailSize = CalcTextSizeWithFont(detailFont, detailText, detailFontSize);
        const float detailX = windowPos.x + ((m_Width - detailSize.x) * 0.5f);
        const float detailY = baseY + headlineFontSize + verticalGap;
        drawList->AddText(
            detailFont,
            detailFontSize,
            ImVec2(detailX, detailY),
            IM_COL32(110, 110, 110, static_cast<int>(255.0f * clampedAlpha)),
            detailText);
    }
}

void Screen::RenderInjectingLayer(
    const char* windowName,
    float alpha,
    float offsetY,
    float scale,
    const char* headline,
    float elapsed,
    bool showCursor,
    bool drawTopographicBackground,
    const char* detailText) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(m_Width, m_Height));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin(windowName, nullptr, flags)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 windowPos = ImGui::GetWindowPos();
        DrawWindowBase(drawList, windowPos, m_Width, m_Height);
        if (drawTopographicBackground) {
            DrawTopographicBackground(drawList, windowPos, m_Width, m_Height, 0.0f);
        }
        DrawInjectionStatusText(drawList, windowPos, alpha, offsetY, scale, headline, elapsed, showCursor, detailText);
        ImGui::End();
    }
}

void Screen::RenderInjecting() {
    const float now = static_cast<float>(ImGui::GetTime());
    if (m_InjectionViewStartTime < 0.0f) {
        m_InjectionViewStartTime = now;
    }

    if (m_InjectionFailed) {
        const float failureElapsed = now - m_InjectionViewStartTime;
        RenderInjectingLayer("Injecting", 1.0f, 0.0f, 1.0f, "Injection failed", failureElapsed, true, false, m_InjectionStatus.c_str());
        return;
    }

    if (m_InjectionDone && !m_InjectionFailed) {
        if (m_InjectionCompleteTime < 0.0f) {
            m_InjectionCompleteTime = now;
        }

        const float successElapsed = now - m_InjectionCompleteTime;
        const float successTextDuration = (static_cast<float>(strlen(kInjectedHeadline)) / kInjectionTypewriterCharsPerSecond) + 0.7f;
        RenderInjectingLayer("Injecting", 1.0f, 0.0f, 1.0f, kInjectedHeadline, successElapsed, true, true);

        if (successElapsed >= successTextDuration) {
            if (m_InterfaceTransitionStartTime < 0.0f) {
                m_InterfaceTransitionStartTime = now;
            }
            m_State = AppState::TransitionToInterface;
            RenderTransitionToInterface();
            return;
        }

        return;
    }

    const float injectingElapsed = now - m_InjectionViewStartTime;
    RenderInjectingLayer("Injecting", 1.0f, 0.0f, 1.0f, kInjectingHeadline, injectingElapsed, true, false);
}

void Screen::RenderHUDPreview() {
    auto* featureManager = FeatureManager::Get();
    bool arrayListEnabled = false;
    for (const auto& mod : featureManager->GetModules(ModuleCategory::Visuals)) {
        if (mod->GetName() == "ArrayList" && mod->IsEnabled()) {
            arrayListEnabled = true;
            break;
        }
    }
    if (!arrayListEnabled) {
        return;
    }

    static std::unordered_map<std::string, float> slideProgress;
    static auto lastFrameTime = std::chrono::steady_clock::now();

    const auto now = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
    lastFrameTime = now;
    if (deltaTime > 0.1f) {
        deltaTime = 0.1f;
    }

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList) {
        return;
    }

    ImFont* regularFont = m_FontOverlayRegular ? m_FontOverlayRegular : ImGui::GetFont();
    ImFont* boldFont = m_FontOverlayBold ? m_FontOverlayBold : regularFont;
    if (!regularFont || !boldFont) {
        return;
    }

    auto* config = Bridge::Get()->GetConfig();
    const bool riseMode = config && config->HUD.m_Mode == static_cast<int>(ArrayListMode::Rise);
    const bool tesseractMode = config && config->HUD.m_Mode == static_cast<int>(ArrayListMode::Tesseract);
    ImFont* nameFont = (riseMode || tesseractMode) ? regularFont : boldFont;
    const float detailFontSize = regularFont->FontSize;
    const float nameFontSize = nameFont->FontSize;
    const float topY = 4.0f;
    const float risePadX = 4.0f;
    const float risePadY = 2.0f;
    const float riseRectWidth = 2.0f;
    const float itemHeight = riseMode ? ((std::max)(detailFontSize, nameFontSize) + 6.0f) : (tesseractMode ? (nameFontSize + 4.0f) : ((std::max)(detailFontSize, nameFontSize) + 2.0f));
    const float itemSpacing = riseMode ? 0.0f : 2.0f;
    const float spaceWidth = CalcTextSizeWithFont(regularFont, " ", detailFontSize).x;
    const float screenW = ImGui::GetIO().DisplaySize.x;
    const ImU32 shadowColor = IM_COL32(0, 0, 0, 120);
    const ImU32 secondaryColor = riseMode ? IM_COL32(154, 154, 154, 255) : IM_COL32(200, 200, 200, 255);
    const ImU32 riseBackgroundColor = IM_COL32(0, 0, 0, 88);
    const ImU32 riseWatermarkTextColor = IM_COL32(232, 232, 232, 255);
    const ImU32 tesseractBackgroundColor = IM_COL32(25, 25, 30, 240);
    const float tesseractPadding = 6.0f;
    const float tesseractGapAfterText = 4.0f;
    const float tesseractRectWidth = 3.0f;

    if (config && config->HUD.m_Watermark) {
        std::string nick = config->m_Username[0] != '\0' ? config->m_Username : "player";
        char fpsText[16];
        snprintf(fpsText, sizeof(fpsText), "%d FPS", (int)(ImGui::GetIO().Framerate + 0.5f));

        float hueR;
        float hueG;
        float hueB;
        if (riseMode) {
            GetRiseRGB(0, hueR, hueG, hueB);
        } else if (tesseractMode) {
            GetTesseractHeaderRGB(0, hueR, hueG, hueB);
        } else {
            const double tMs = (double)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            ImGui::ColorConvertHSVtoRGB((float)fmod(tMs / 2000.0, 1.0), 0.75f, 0.9f, hueR, hueG, hueB);
        }
        const ImU32 accentColor = MakeColorU32(hueR, hueG, hueB);
        const std::string middle = " | " + nick + " | ";
        const float textY = riseMode ? (topY + risePadY) : (tesseractMode ? 6.0f : topY);
        float cursorX = riseMode ? (10.0f + risePadX) : 10.0f;

        const float titleWidth = CalcTextSizeWithFont(nameFont, "OpenCommunity", nameFontSize).x;
        const float middleWidth = CalcTextSizeWithFont(regularFont, middle.c_str(), detailFontSize).x;
        const float fpsWidth = CalcTextSizeWithFont(regularFont, fpsText, detailFontSize).x;

        if (riseMode) {
            const float boxWidth = titleWidth + middleWidth + fpsWidth + risePadX * 2.0f + riseRectWidth + 1.0f;
            const ImVec2 boxMin(10.0f, topY);
            const ImVec2 boxMax(boxMin.x + boxWidth, topY + itemHeight);
            drawList->AddRectFilled(boxMin, boxMax, riseBackgroundColor, 0.0f);
            drawList->AddRectFilled(ImVec2(boxMax.x - riseRectWidth, boxMin.y), boxMax, accentColor, 0.0f);
        }

        DrawShadowedText(drawList, nameFont, nameFontSize, ImVec2(cursorX, textY), riseMode ? riseWatermarkTextColor : accentColor, shadowColor, "OpenCommunity");
        cursorX += titleWidth;

        DrawShadowedText(drawList, regularFont, detailFontSize, ImVec2(cursorX, textY), secondaryColor, shadowColor, middle);
        cursorX += middleWidth;

        DrawShadowedText(drawList, regularFont, detailFontSize, ImVec2(cursorX, textY), secondaryColor, shadowColor, fpsText);
    }

    struct ModEntry {
        std::string name;
        std::string tag;
        float totalWidth;
    };

    std::vector<ModEntry> activeModules;
    std::vector<std::string> currentActiveKeys;

    ModuleCategory cats[] = { ModuleCategory::Combat, ModuleCategory::Movement, ModuleCategory::Visuals, ModuleCategory::Settings };
    for (auto cat : cats) {
        for (const auto& mod : featureManager->GetModules(cat)) {
            if (!mod->IsEnabled() || mod->GetName() == "ArrayList") {
                continue;
            }

            const std::string name = FormatModuleName(mod->GetName(), config && config->HUD.m_SpacedModules);
            const std::string tag = mod->GetTag();
            float totalWidth = CalcTextSizeWithFont(nameFont, name.c_str(), nameFontSize).x;
            if (!tag.empty()) {
                totalWidth += spaceWidth + CalcTextSizeWithFont(regularFont, tag.c_str(), detailFontSize).x;
            }

            activeModules.push_back({ name, tag, totalWidth });
            currentActiveKeys.push_back(name);
        }
    }

    for (auto it = slideProgress.begin(); it != slideProgress.end(); ) {
        bool found = false;
        for (const auto& k : currentActiveKeys) {
            if (k == it->first) { found = true; break; }
        }
        if (!found) it = slideProgress.erase(it);
        else ++it;
    }

    std::sort(activeModules.begin(), activeModules.end(), [](const ModEntry& a, const ModEntry& b) {
        return a.totalWidth > b.totalWidth;
    });

    int idx = 0;
    const float goldenRatio = 0.618033988749895f;

    for (const auto& mod : activeModules) {
        const ImVec2 nameSizePx = CalcTextSizeWithFont(nameFont, mod.name.c_str(), nameFontSize);
        float contentWidth = nameSizePx.x;
        if (!mod.tag.empty()) {
            contentWidth += spaceWidth + CalcTextSizeWithFont(regularFont, mod.tag.c_str(), detailFontSize).x;
        }

        float cr, cg, cb;
        if (riseMode) {
            GetRiseRGB(idx, cr, cg, cb);
        } else if (tesseractMode) {
            GetTesseractRGB(idx, cr, cg, cb);
        } else {
            double tMsD = (double)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            float bHue = (float)fmod(tMsD / 2000.0, 1.0);
            float hueOffset = fmodf(idx * goldenRatio, 1.0f);
            float hue = fmodf(bHue + hueOffset, 1.0f);
            ImGui::ColorConvertHSVtoRGB(hue, 0.75f, 0.9f, cr, cg, cb);
        }
        const ImU32 modColor = MakeColorU32(cr, cg, cb);

        if (slideProgress.find(mod.name) == slideProgress.end())
            slideProgress[mod.name] = 0.0f;
        float& progress = slideProgress[mod.name];
        if (progress < 1.0f) {
            progress += deltaTime * 6.0f;
            if (progress > 1.0f) {
                progress = 1.0f;
            }
        }
        float eased = 1.0f - (1.0f - progress) * (1.0f - progress) * (1.0f - progress);

        float layoutWidth = contentWidth;
        if (tesseractMode) {
            layoutWidth = nameSizePx.x;
            if (!mod.tag.empty()) {
                layoutWidth += 4.0f + CalcTextSizeWithFont(regularFont, mod.tag.c_str(), detailFontSize).x;
            }
        }

        const float boxWidth = riseMode
            ? (layoutWidth + risePadX * 2.0f + riseRectWidth + 1.0f)
            : (tesseractMode ? (layoutWidth + tesseractPadding + tesseractGapAfterText + tesseractRectWidth) : layoutWidth);
        float targetX = screenW - boxWidth - (riseMode ? 4.0f : 6.0f);
        float currentX = screenW + (targetX - screenW) * eased;
        float yPos = (tesseractMode ? 2.0f : topY) + idx * (itemHeight + itemSpacing);
        float textX = riseMode ? (currentX + risePadX) : (tesseractMode ? (currentX + tesseractPadding) : currentX);
        float textY = riseMode ? (yPos + risePadY) : (tesseractMode ? (yPos + 2.0f) : yPos);

        if (riseMode) {
            const ImVec2 boxMin(currentX, yPos);
            const ImVec2 boxMax(currentX + boxWidth, yPos + itemHeight);
            drawList->AddRectFilled(boxMin, boxMax, riseBackgroundColor, 0.0f);
            drawList->AddRectFilled(ImVec2(boxMax.x - riseRectWidth, boxMin.y), boxMax, modColor, 0.0f);
        } else if (tesseractMode) {
            const ImVec2 boxMin(currentX, yPos);
            const ImVec2 boxMax(currentX + boxWidth, yPos + itemHeight);
            drawList->AddRectFilled(boxMin, boxMax, tesseractBackgroundColor, 5.0f, ImDrawFlags_RoundCornersLeft);
            drawList->AddRectFilled(ImVec2(boxMax.x - tesseractRectWidth, boxMin.y), boxMax, modColor);
        }

        DrawShadowedText(drawList, nameFont, nameFontSize, ImVec2(textX, textY), modColor, shadowColor, mod.name);

        if (!mod.tag.empty()) {
            float tagX = textX + nameSizePx.x + (tesseractMode ? 4.0f : spaceWidth);
            DrawShadowedText(drawList, regularFont, detailFontSize, ImVec2(tagX, textY), secondaryColor, shadowColor, mod.tag);
        }

        idx++;
    }
}

static void RenderModulesForCategory(ModuleCategory category, float areaWidth, float areaHeight, ImFont* fontBold, ImFont* fontBody, ID3D11Device* device) {
    // Cache for module prefix icon textures (keyed by image data pointer)
    static std::unordered_map<const unsigned char*, ID3D11ShaderResourceView*> s_ModuleIconCache;
    static std::unordered_map<std::string, ID3D11ShaderResourceView*> s_ModulePathIconCache;
    static float s_CategoryScroll[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    auto& modules = FeatureManager::Get()->GetModules(category);
    if (modules.empty()) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    auto flushPendingBindKeys = []() {
        for (int vk = 1; vk < 256; ++vk) {
            GetAsyncKeyState(vk);
        }
    };
    auto hasHeldKeyboardKey = []() {
        for (int vk = 1; vk < 256; ++vk) {
            if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
                vk == VK_XBUTTON1 || vk == VK_XBUTTON2) {
                continue;
            }

            if (GetAsyncKeyState(vk) & 0x8000) {
                return true;
            }
        }
        return false;
    };

    const float colGap = 12.0f;
    const float cardPadX = 14.0f;
    const float cardPadY = 10.0f;
    const float cardGapY = 10.0f;
    const float colW = (areaWidth - colGap) * 0.5f;
    const float toggleSize = 16.0f;
    const float bindW = 42.0f;
    const float optLineH = 28.0f;
    const float headerH = 28.0f;
    const float cardRounding = 8.0f;
    const float contentBottomPad = 16.0f;
    const float visibleHeight = (areaHeight > contentBottomPad) ? (areaHeight - contentBottomPad) : areaHeight;

    auto getCardHeight = [&](const std::shared_ptr<Module>& mod) -> float {
        const auto visibleOrder = GetVisibleOptionOrder(mod);
        float cardHeight = headerH + cardPadY;
        if (!visibleOrder.empty()) {
            cardHeight += static_cast<float>(visibleOrder.size()) * optLineH + GetModuleBodyFooterSpacing(mod, visibleOrder);
        }
        return cardHeight;
    };

    static int waitingBindModuleIdx = -1;
    static ModuleCategory waitingBindCat = ModuleCategory::Combat;
    static bool waitingBindNeedsRelease = false;

    float layoutColY[2] = { 0.0f, 0.0f };
    for (const auto& mod : modules) {
        const float cardHeight = getCardHeight(mod);
        const int col = (layoutColY[0] <= layoutColY[1]) ? 0 : 1;
        layoutColY[col] += cardHeight + cardGapY;
    }

    const float totalContentHeight = (std::max)(0.0f, (std::max)(layoutColY[0], layoutColY[1]) - cardGapY);
    const float maxScroll = (std::max)(0.0f, totalContentHeight - visibleHeight);
    const int categoryIndex = static_cast<int>(category);
    float& scrollOffset = s_CategoryScroll[categoryIndex];
    scrollOffset = (std::clamp)(scrollOffset, 0.0f, maxScroll);

    const ImVec2 viewMin(origin.x, origin.y);
    const ImVec2 viewMax(origin.x + areaWidth, origin.y + visibleHeight);
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseHoveringRect(viewMin, viewMax, true) && io.MouseWheel != 0.0f && !ImGui::IsAnyItemActive()) {
        scrollOffset = (std::clamp)(scrollOffset - io.MouseWheel * 38.0f, 0.0f, maxScroll);
    }

    float colY[2] = { -scrollOffset, -scrollOffset };

    for (int mi = 0; mi < (int)modules.size(); mi++) {
        auto& mod = modules[mi];
        const auto visibleOptionOrder = GetVisibleOptionOrder(mod);
        const int optCount = static_cast<int>(visibleOptionOrder.size());
        const float cardH = getCardHeight(mod);

        int col = (colY[0] <= colY[1]) ? 0 : 1;
        float cx = origin.x + col * (colW + colGap);
        float cy = origin.y + colY[col];

        ImVec2 cardMin(cx, cy);
        ImVec2 cardMax(cx + colW, cy + cardH);

        dl->AddRectFilled(cardMin, cardMax, IM_COL32(230, 230, 230, 180), cardRounding);
        dl->AddRect(cardMin, cardMax, IM_COL32(200, 200, 200, 120), cardRounding, 0, 1.0f);

        // Render module prefix icon if available
        const float prefixIconSize = 20.0f;
        float nameOffsetX = 0.0f;
        if (device) {
            ID3D11ShaderResourceView* moduleIcon = nullptr;

            if (mod->GetImageData() && mod->GetImageSize() > 0) {
                auto it = s_ModuleIconCache.find(mod->GetImageData());
                if (it == s_ModuleIconCache.end()) {
                    int tw = 0, th = 0, tc = 0;
                    unsigned char* pixels = stbi_load_from_memory(mod->GetImageData(), mod->GetImageSize(), &tw, &th, &tc, 4);
                    ID3D11ShaderResourceView* srv = nullptr;
                    if (pixels && tw > 0 && th > 0) {
                        D3D11_TEXTURE2D_DESC desc = {};
                        desc.Width = tw; desc.Height = th;
                        desc.MipLevels = 1; desc.ArraySize = 1;
                        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                        desc.SampleDesc.Count = 1;
                        desc.Usage = D3D11_USAGE_DEFAULT;
                        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                        D3D11_SUBRESOURCE_DATA sub = {};
                        sub.pSysMem = pixels; sub.SysMemPitch = tw * 4;
                        ID3D11Texture2D* tex = nullptr;
                        if (SUCCEEDED(device->CreateTexture2D(&desc, &sub, &tex))) {
                            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                            srvDesc.Texture2D.MipLevels = 1;
                            device->CreateShaderResourceView(tex, &srvDesc, &srv);
                            tex->Release();
                        }
                        stbi_image_free(pixels);
                    }
                    s_ModuleIconCache[mod->GetImageData()] = srv;
                    it = s_ModuleIconCache.find(mod->GetImageData());
                }

                if (it != s_ModuleIconCache.end()) {
                    moduleIcon = it->second;
                }
            } else if (!mod->GetImagePath().empty()) {
                auto it = s_ModulePathIconCache.find(mod->GetImagePath());
                if (it == s_ModulePathIconCache.end()) {
                    ID3D11ShaderResourceView* srv = nullptr;
                    const auto resolvedPath = ResolveModuleImagePath(mod->GetImagePath());
                    if (!resolvedPath.empty()) {
                        int tw = 0, th = 0, tc = 0;
                        unsigned char* pixels = stbi_load(resolvedPath.string().c_str(), &tw, &th, &tc, 4);
                        if (pixels && tw > 0 && th > 0) {
                            D3D11_TEXTURE2D_DESC desc = {};
                            desc.Width = tw; desc.Height = th;
                            desc.MipLevels = 1; desc.ArraySize = 1;
                            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                            desc.SampleDesc.Count = 1;
                            desc.Usage = D3D11_USAGE_DEFAULT;
                            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                            D3D11_SUBRESOURCE_DATA sub = {};
                            sub.pSysMem = pixels; sub.SysMemPitch = tw * 4;
                            ID3D11Texture2D* tex = nullptr;
                            if (SUCCEEDED(device->CreateTexture2D(&desc, &sub, &tex))) {
                                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                                srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                                srvDesc.Texture2D.MipLevels = 1;
                                device->CreateShaderResourceView(tex, &srvDesc, &srv);
                                tex->Release();
                            }
                            stbi_image_free(pixels);
                        }
                    }
                    s_ModulePathIconCache[mod->GetImagePath()] = srv;
                    it = s_ModulePathIconCache.find(mod->GetImagePath());
                }

                if (it != s_ModulePathIconCache.end()) {
                    moduleIcon = it->second;
                }
            }

            if (moduleIcon) {
                float iconY = cy + (headerH - prefixIconSize) * 0.5f;
                float iconX = cx + cardPadX;
                dl->AddImage((ImTextureID)moduleIcon, ImVec2(iconX, iconY), ImVec2(iconX + prefixIconSize, iconY + prefixIconSize));
                nameOffsetX = prefixIconSize + 6.0f;
            }
        }

        ImFont* nf = fontBold ? fontBold : ImGui::GetFont();
        float nameFS = nf->FontSize;
        dl->AddText(nf, nameFS, ImVec2(cx + cardPadX + nameOffsetX, cy + (headerH - nameFS) * 0.5f), IM_COL32(0, 0, 0, 255), mod->GetName().c_str());

        float rightX = cx + colW - cardPadX;
        ImFont* bf = fontBody ? fontBody : ImGui::GetFont();
        float bfs = bf->FontSize;
        const bool supportsKeybind = mod->SupportsKeybind();
        float bindX = rightX;

        if (supportsKeybind) {
            std::string bindLabel = mod->GetKeybindName();
            ImVec2 bindSize = bf->CalcTextSizeA(bfs, FLT_MAX, 0.0f, bindLabel.c_str());
            float bindBtnW = bindSize.x + 12.0f;
            if (bindBtnW < bindW) bindBtnW = bindW;
            bindX = rightX - bindBtnW;
            float bindY = cy + (headerH - 18.0f) * 0.5f;

            ImVec2 bindMin(bindX, bindY);
            ImVec2 bindMax(bindX + bindBtnW, bindY + 18.0f);

            bool isWaiting = (waitingBindModuleIdx == mi && waitingBindCat == category);

            dl->AddRectFilled(bindMin, bindMax, isWaiting ? IM_COL32(180, 180, 180, 200) : IM_COL32(210, 210, 210, 180), 4.0f);
            dl->AddRect(bindMin, bindMax, IM_COL32(170, 170, 170, 150), 4.0f, 0, 1.0f);

            const char* displayText = isWaiting ? "..." : bindLabel.c_str();
            ImVec2 dts = bf->CalcTextSizeA(bfs, FLT_MAX, 0.0f, displayText);
            dl->AddText(bf, bfs, ImVec2(bindMin.x + (bindBtnW - dts.x) * 0.5f, bindMin.y + (18.0f - dts.y) * 0.5f),
                        IM_COL32(60, 60, 60, 255), displayText);

            ImGui::SetCursorScreenPos(bindMin);
            char bindBtnId[64];
            snprintf(bindBtnId, sizeof(bindBtnId), "##bind_%d_%d", (int)category, mi);
            if (ImGui::InvisibleButton(bindBtnId, ImVec2(bindBtnW, 18.0f))) {
                if (isWaiting) {
                    waitingBindModuleIdx = -1;
                    waitingBindNeedsRelease = false;
                } else {
                    waitingBindModuleIdx = mi;
                    waitingBindCat = category;
                    waitingBindNeedsRelease = true;
                    flushPendingBindKeys();
                }
            }

            if (isWaiting) {
                if (waitingBindNeedsRelease) {
                    const bool mouseHeld = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
                    if (!mouseHeld && !hasHeldKeyboardKey()) {
                        waitingBindNeedsRelease = false;
                        flushPendingBindKeys();
                    }
                } else {
                    for (int vk = 1; vk < 256; vk++) {
                        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
                            vk == VK_XBUTTON1 || vk == VK_XBUTTON2) {
                            continue;
                        }

                        if (GetAsyncKeyState(vk) & 1) {
                            if (vk == VK_ESCAPE) {
                                mod->SetKeybind(0);
                            } else {
                                mod->SetKeybind(vk);
                            }
                            waitingBindModuleIdx = -1;
                            waitingBindNeedsRelease = false;
                            break;
                        }
                    }
                }
            }
        }

        float toggleX = supportsKeybind ? (bindX - toggleSize - 10.0f) : (rightX - toggleSize);
        float toggleY = cy + (headerH - toggleSize) * 0.5f;
        ImVec2 toggleMin(toggleX, toggleY);
        ImVec2 toggleMax(toggleX + toggleSize, toggleY + toggleSize);

        bool enabled = mod->IsEnabled();
        ImU32 toggleBg = enabled ? IM_COL32(60, 60, 60, 255) : IM_COL32(190, 190, 190, 200);
        dl->AddRectFilled(toggleMin, toggleMax, toggleBg, 4.0f);
        if (enabled) {
            float cx2 = toggleX + toggleSize * 0.5f;
            float cy2 = toggleY + toggleSize * 0.5f;
            dl->AddLine(ImVec2(cx2 - 4, cy2), ImVec2(cx2 - 1, cy2 + 3), IM_COL32(255, 255, 255, 255), 2.0f);
            dl->AddLine(ImVec2(cx2 - 1, cy2 + 3), ImVec2(cx2 + 4, cy2 - 3), IM_COL32(255, 255, 255, 255), 2.0f);
        }

        ImGui::SetCursorScreenPos(toggleMin);
        char toggleId[64];
        snprintf(toggleId, sizeof(toggleId), "##toggle_%d_%d", (int)category, mi);
        if (ImGui::InvisibleButton(toggleId, ImVec2(toggleSize, toggleSize))) {
            mod->SetEnabled(!enabled);
            enabled = !enabled;
        }

        dl->PushClipRect(cardMin, cardMax, true);

        if (optCount > 0) {
            float optY = cy + headerH + 4.0f;
            float optX = cx + cardPadX;
            float optW = colW - cardPadX * 2;

            // Push body font so widgets render at correct 17pt size
            if (fontBody) ImGui::PushFont(fontBody);

            // Push light style for ImGui widgets on white/light cards
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.93f, 0.93f, 0.93f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.88f, 0.88f, 0.88f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.83f, 0.83f, 0.83f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.50f, 0.50f, 0.50f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.20f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.84f, 0.84f, 0.84f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.78f, 0.78f, 0.78f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.88f, 0.88f, 0.88f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.82f, 0.82f, 0.82f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.76f, 0.76f, 0.76f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.82f, 0.82f, 0.82f, 0.6f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 10.0f);

            float sliderW = optW * 0.40f;

            for (const size_t optionIndex : visibleOptionOrder) {
                auto& opt = mod->GetOptions()[optionIndex];
                ImGui::SetCursorScreenPos(ImVec2(optX, optY));

                char optId[128];
                snprintf(optId, sizeof(optId), "##opt_%d_%d_%zu_%s", (int)category, mi, optionIndex, opt.name.c_str());

                ImFont* labelFont = fontBody ? fontBody : ImGui::GetFont();
                float labelFS = labelFont->FontSize;

                switch (opt.type) {
                case OptionType::Toggle: {
                    const float cbSize = 14.0f;
                    float cbX = optX;
                    if (opt.showPlayerHead && !opt.playerHeadName.empty()) {
                        const float headSize = 18.0f;
                        const ImVec2 headMin(optX, optY + (optLineH - headSize) * 0.5f);
                        const ImVec2 headMax(headMin.x + headSize, headMin.y + headSize);
                        DrawPlayerHeadPreview(dl, device, labelFont, labelFS, opt.playerHeadName, headMin, headMax);
                        cbX += headSize + 8.0f;
                    }

                    if (opt.statusOnly) {
                        const ImU32 statusColor = opt.boolValue ? IM_COL32(85, 170, 85, 255) : IM_COL32(200, 70, 70, 255);
                        dl->AddText(labelFont, labelFS, ImVec2(cbX, optY + (optLineH - labelFS) * 0.5f), statusColor, opt.name.c_str());
                        break;
                    }

                    // Custom drawn checkbox to match card style
                    float cbY = optY + (optLineH - cbSize) * 0.5f;
                    ImVec2 cbMin(cbX, cbY);
                    ImVec2 cbMax(cbX + cbSize, cbY + cbSize);

                    ImU32 cbBg = opt.boolValue ? IM_COL32(60, 60, 60, 255) : IM_COL32(200, 200, 200, 220);
                    dl->AddRectFilled(cbMin, cbMax, cbBg, 3.0f);
                    dl->AddRect(cbMin, cbMax, IM_COL32(160, 160, 160, 180), 3.0f, 0, 1.0f);
                    if (opt.boolValue) {
                        float cx2 = cbX + cbSize * 0.5f;
                        float cy2 = cbY + cbSize * 0.5f;
                        dl->AddLine(ImVec2(cx2 - 3, cy2), ImVec2(cx2 - 1, cy2 + 3), IM_COL32(255, 255, 255, 255), 1.8f);
                        dl->AddLine(ImVec2(cx2 - 1, cy2 + 3), ImVec2(cx2 + 4, cy2 - 3), IM_COL32(255, 255, 255, 255), 1.8f);
                    }

                    if (opt.interactive) {
                        ImGui::SetCursorScreenPos(cbMin);
                        ImGui::InvisibleButton(optId, ImVec2(cbSize, cbSize));
                        if (ImGui::IsItemClicked()) {
                            opt.boolValue = !opt.boolValue;
                        }
                    }

                    dl->AddText(labelFont, labelFS, ImVec2(cbX + cbSize + 6.0f, optY + (optLineH - labelFS) * 0.5f),
                        opt.interactive ? IM_COL32(40, 40, 40, 255) : IM_COL32(90, 90, 90, 255), opt.name.c_str());
                    break;
                }
                case OptionType::SliderInt: {
                    dl->AddText(labelFont, labelFS, ImVec2(optX, optY + (optLineH - labelFS) * 0.5f), IM_COL32(40, 40, 40, 255), opt.name.c_str());

                    // Custom thin rounded slider — positioned right after label text
                    const float sliderH = 6.0f;
                    const float grabRadius = 7.0f;
                    ImVec2 labelSize = labelFont->CalcTextSizeA(labelFS, FLT_MAX, 0.0f, opt.name.c_str());
                    float sliderX = optX + labelSize.x + 12.0f;
                    float actualSliderW = optX + optW - sliderX - 30.0f;
                    if (actualSliderW < 40.0f) actualSliderW = 40.0f;
                    float sliderY = optY + (optLineH - sliderH) * 0.5f;
                    ImVec2 trackMin(sliderX, sliderY);
                    ImVec2 trackMax(sliderX + actualSliderW, sliderY + sliderH);
                    float trackRounding = sliderH * 0.5f;

                    // Track background
                    dl->AddRectFilled(trackMin, trackMax, IM_COL32(210, 210, 210, 255), trackRounding);
                    dl->AddRect(trackMin, trackMax, IM_COL32(185, 185, 185, 200), trackRounding, 0, 1.0f);

                    // Filled portion
                    float tNorm = (opt.intMax > opt.intMin) ? (float)(opt.intValue - opt.intMin) / (float)(opt.intMax - opt.intMin) : 0.0f;
                    if (tNorm < 0.0f) tNorm = 0.0f; if (tNorm > 1.0f) tNorm = 1.0f;
                    float fillX = sliderX + tNorm * actualSliderW;
                    if (fillX > sliderX + 2.0f) {
                        dl->AddRectFilled(trackMin, ImVec2(fillX, trackMax.y), IM_COL32(80, 80, 80, 255), trackRounding);
                    }

                    // Grab circle
                    float grabCX = sliderX + tNorm * actualSliderW;
                    float grabCY = sliderY + sliderH * 0.5f;
                    dl->AddCircleFilled(ImVec2(grabCX, grabCY), grabRadius, IM_COL32(60, 60, 60, 255));
                    dl->AddCircle(ImVec2(grabCX, grabCY), grabRadius, IM_COL32(40, 40, 40, 255), 0, 1.2f);

                    // Invisible button for interaction
                    ImVec2 interactMin(sliderX - grabRadius, sliderY - grabRadius);
                    ImVec2 interactMax(sliderX + actualSliderW + grabRadius, sliderY + sliderH + grabRadius);
                    ImGui::SetCursorScreenPos(interactMin);
                    ImGui::InvisibleButton(optId, ImVec2(interactMax.x - interactMin.x, interactMax.y - interactMin.y));
                    if (ImGui::IsItemActive()) {
                        float mouseX = ImGui::GetIO().MousePos.x;
                        float newT = (mouseX - sliderX) / actualSliderW;
                        if (newT < 0.0f) newT = 0.0f; if (newT > 1.0f) newT = 1.0f;
                        opt.intValue = opt.intMin + (int)(newT * (opt.intMax - opt.intMin) + 0.5f);
                    }

                    // Value text
                    char valBuf[32];
                    snprintf(valBuf, sizeof(valBuf), "%d", opt.intValue);
                    dl->AddText(labelFont, labelFS, ImVec2(sliderX + actualSliderW + 6.0f, optY + (optLineH - labelFS) * 0.5f), IM_COL32(60, 60, 60, 255), valBuf);

                    break;
                }
                case OptionType::SliderFloat: {
                    dl->AddText(labelFont, labelFS, ImVec2(optX, optY + (optLineH - labelFS) * 0.5f), IM_COL32(40, 40, 40, 255), opt.name.c_str());

                    // Custom thin rounded slider — positioned right after label text
                    const float sliderH = 6.0f;
                    const float grabRadius = 7.0f;
                    ImVec2 labelSizeF = labelFont->CalcTextSizeA(labelFS, FLT_MAX, 0.0f, opt.name.c_str());
                    float sliderX = optX + labelSizeF.x + 12.0f;
                    float actualSliderWF = optX + optW - sliderX - 30.0f;
                    if (actualSliderWF < 40.0f) actualSliderWF = 40.0f;
                    float sliderY = optY + (optLineH - sliderH) * 0.5f;
                    ImVec2 trackMin(sliderX, sliderY);
                    ImVec2 trackMax(sliderX + actualSliderWF, sliderY + sliderH);
                    float trackRounding = sliderH * 0.5f;

                    // Track background
                    dl->AddRectFilled(trackMin, trackMax, IM_COL32(210, 210, 210, 255), trackRounding);
                    dl->AddRect(trackMin, trackMax, IM_COL32(185, 185, 185, 200), trackRounding, 0, 1.0f);

                    // Filled portion
                    float tNorm = (opt.floatMax > opt.floatMin) ? (opt.floatValue - opt.floatMin) / (opt.floatMax - opt.floatMin) : 0.0f;
                    if (tNorm < 0.0f) tNorm = 0.0f; if (tNorm > 1.0f) tNorm = 1.0f;
                    float fillX = sliderX + tNorm * actualSliderWF;
                    if (fillX > sliderX + 2.0f) {
                        dl->AddRectFilled(trackMin, ImVec2(fillX, trackMax.y), IM_COL32(80, 80, 80, 255), trackRounding);
                    }

                    // Grab circle
                    float grabCX = sliderX + tNorm * actualSliderWF;
                    float grabCY = sliderY + sliderH * 0.5f;
                    dl->AddCircleFilled(ImVec2(grabCX, grabCY), grabRadius, IM_COL32(60, 60, 60, 255));
                    dl->AddCircle(ImVec2(grabCX, grabCY), grabRadius, IM_COL32(40, 40, 40, 255), 0, 1.2f);

                    // Invisible button for interaction
                    ImVec2 interactMin(sliderX - grabRadius, sliderY - grabRadius);
                    ImVec2 interactMax(sliderX + actualSliderWF + grabRadius, sliderY + sliderH + grabRadius);
                    ImGui::SetCursorScreenPos(interactMin);
                    ImGui::InvisibleButton(optId, ImVec2(interactMax.x - interactMin.x, interactMax.y - interactMin.y));
                    if (ImGui::IsItemActive()) {
                        float mouseX = ImGui::GetIO().MousePos.x;
                        float newT = (mouseX - sliderX) / actualSliderWF;
                        if (newT < 0.0f) newT = 0.0f; if (newT > 1.0f) newT = 1.0f;
                        opt.floatValue = opt.floatMin + newT * (opt.floatMax - opt.floatMin);
                    }

                    // Value text
                    char valBuf[32];
                    snprintf(valBuf, sizeof(valBuf), "%.2f", opt.floatValue);
                    dl->AddText(labelFont, labelFS, ImVec2(sliderX + actualSliderWF + 6.0f, optY + (optLineH - labelFS) * 0.5f), IM_COL32(60, 60, 60, 255), valBuf);

                    break;
                }
                case OptionType::Combo: {
                    dl->AddText(labelFont, labelFS, ImVec2(optX, optY + 2.0f), IM_COL32(40, 40, 40, 255), opt.name.c_str());

                    const float comboPadX = 8.0f;
                    const float comboPadY = 4.0f;
                    const float comboRounding = 6.0f;
                    const float comboHeight = ImGui::GetTextLineHeight() + comboPadY * 2.0f;
                    const ImVec2 comboPos(optX + optW - sliderW, optY + (optLineH - comboHeight) * 0.5f);
                    const ImVec2 comboMin = comboPos;
                    const ImVec2 comboMax(comboPos.x + sliderW, comboPos.y + comboHeight);

                    dl->AddRectFilled(comboMin, comboMax, IM_COL32(214, 219, 228, 255), comboRounding);
                    dl->AddRectFilled(
                        ImVec2(comboMin.x + 1.0f, comboMin.y + 1.0f),
                        ImVec2(comboMax.x - 1.0f, comboMax.y - 1.0f),
                        IM_COL32(236, 240, 246, 255),
                        comboRounding);
                    dl->AddRect(comboMin, comboMax, IM_COL32(145, 152, 164, 255), comboRounding, 0, 1.0f);

                    ImGui::SetCursorScreenPos(comboPos);
                    ImGui::PushItemWidth(sliderW);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.09f, 0.10f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.94f, 0.95f, 0.97f, 1.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(comboPadX, comboPadY));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, comboRounding);
                    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, comboRounding);
                    std::vector<const char*> items;
                    for (auto& s : opt.comboItems) items.push_back(s.c_str());
                    ImGui::Combo(optId, &opt.comboIndex, items.data(), (int)items.size());
                    ImGui::PopStyleVar(4);
                    ImGui::PopStyleColor(9);
                    ImGui::PopItemWidth();
                    break;
                }
                case OptionType::Color: {
                    dl->AddText(labelFont, labelFS, ImVec2(optX, optY + 2.0f), IM_COL32(40, 40, 40, 255), opt.name.c_str());
                    ImGui::SetCursorScreenPos(ImVec2(optX + optW - sliderW, optY));
                    ImGui::PushItemWidth(sliderW);
                    ImGui::ColorEdit3(optId, opt.colorValue, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                    opt.colorValue[3] = 1.0f;
                    ImGui::PopItemWidth();
                    break;
                }
                case OptionType::Text: {
                    dl->AddText(labelFont, labelFS, ImVec2(optX, optY + 2.0f), IM_COL32(40, 40, 40, 255), opt.name.c_str());

                    std::vector<char> textBuffer((std::max)(2, opt.textMaxLength + 1), '\0');
                    strncpy_s(textBuffer.data(), textBuffer.size(), opt.textValue.c_str(), _TRUNCATE);
                    const bool useTargetAutocomplete = mod->GetName() == "Target" && opt.name == "Player Name";

                    const float inputPadX = 6.0f;
                    const float inputPadY = 4.0f;
                    const float inputHeight = ImGui::GetTextLineHeight() + inputPadY * 2.0f;
                    const ImVec2 inputPos(optX + optW - sliderW, optY + (optLineH - inputHeight) * 0.5f);
                    const ImVec2 inputMin = inputPos;
                    const ImVec2 inputMax(inputPos.x + sliderW, inputPos.y + inputHeight);
                    dl->AddRectFilled(inputMin, inputMax, IM_COL32(214, 219, 228, 255), 4.0f);
                    dl->AddRectFilled(ImVec2(inputMin.x + 1.0f, inputMin.y + 1.0f), ImVec2(inputMax.x - 1.0f, inputMax.y - 1.0f), IM_COL32(236, 240, 246, 255), 4.0f);
                    dl->AddRect(inputMin, inputMax, IM_COL32(145, 152, 164, 255), 4.0f, 0, 1.0f);

                    ImGui::SetCursorScreenPos(inputPos);
                    ImGui::PushItemWidth(sliderW);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.09f, 0.10f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(0.55f, 0.67f, 0.90f, 0.35f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(inputPadX, inputPadY));
                    const ImGuiInputTextFlags inputFlags = useTargetAutocomplete ? ImGuiInputTextFlags_CallbackCompletion : ImGuiInputTextFlags_None;
                    ImGuiInputTextCallback callback = useTargetAutocomplete ? TargetNameInputCallback : nullptr;
                    void* callbackUserData = useTargetAutocomplete ? static_cast<void*>(Bridge::Get()->GetConfig()) : nullptr;
                    if (ImGui::InputText(optId, textBuffer.data(), textBuffer.size(), inputFlags, callback, callbackUserData)) {
                        opt.textValue = textBuffer.data();
                    }
                    if (useTargetAutocomplete && !ImGui::IsItemActive()) {
                        ResetTargetNameAutocomplete();
                    }
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor(6);
                    ImGui::PopItemWidth();
                    break;
                }
                case OptionType::Button: {
                    dl->AddText(labelFont, labelFS, ImVec2(optX, optY + 2.0f), IM_COL32(40, 40, 40, 255), opt.name.c_str());
                    ImGui::SetCursorScreenPos(ImVec2(optX + optW - sliderW, optY));
                    if (ImGui::Button(opt.buttonLabel.c_str(), ImVec2(sliderW, optLineH - 6.0f))) {
                        opt.buttonPressed = true;
                    }
                    break;
                }
                }

                optY += optLineH;
            }

            ImGui::PopStyleVar(4);
            ImGui::PopStyleColor(14);

            if (fontBody) ImGui::PopFont();
        }

        dl->PopClipRect();

        colY[col] += cardH + cardGapY;
    }
}

void Screen::RenderCombatTab() {
    const float contentW = m_Width - 52.0f - 16.0f - 16.0f;
    RenderModulesForCategory(ModuleCategory::Combat, contentW, m_Height, m_FontBold, m_FontBody, m_Device);
}

void Screen::RenderMovementTab() {
    const float contentW = m_Width - 52.0f - 16.0f - 16.0f;
    RenderModulesForCategory(ModuleCategory::Movement, contentW, m_Height, m_FontBold, m_FontBody, m_Device);
}

void Screen::RenderVisualsTab() {
    const float contentW = m_Width - 52.0f - 16.0f - 16.0f;
    RenderModulesForCategory(ModuleCategory::Visuals, contentW, m_Height, m_FontBold, m_FontBody, m_Device);
}

void Screen::RenderSettingsTab() {
    const float contentW = m_Width - 52.0f - 16.0f - 16.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (!drawList) {
        return;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float pageX = origin.x + 4.0f;
    const float basePageY = origin.y + 4.0f;
    const float pageWidth = contentW - 12.0f;
    const float updatesHeight = 104.0f;
    const float buttonHeight = 52.0f;
    const float cardGap = 14.0f;
    const float cardRounding = 18.0f;
    const float cardPadding = 20.0f;

    const ImU32 panelColor = IM_COL32(244, 247, 252, 232);
    const ImU32 panelInnerColor = IM_COL32(250, 252, 255, 214);
    const ImU32 panelBorderColor = IM_COL32(194, 202, 214, 255);
    const ImU32 panelShadowColor = IM_COL32(160, 172, 189, 40);
    const ImU32 titleColor = IM_COL32(26, 31, 41, 255);
    const ImU32 bodyColor = IM_COL32(78, 86, 97, 255);
    const ImU32 linkColor = IM_COL32(58, 123, 255, 255);
    const ImU32 linkHoverColor = IM_COL32(35, 98, 228, 255);
    const ImU32 dividerColor = IM_COL32(202, 210, 221, 255);

    ImFont* titleFont = m_FontBoldMed ? m_FontBoldMed : (m_FontBold ? m_FontBold : ImGui::GetFont());
    const float titleFontSize = titleFont ? titleFont->FontSize : ImGui::GetFontSize();
    ImFont* bodyFont = m_FontBody ? m_FontBody : ImGui::GetFont();
    ImFont* accentFont = m_FontBold ? m_FontBold : bodyFont;
    const float bodyFontSize = bodyFont ? bodyFont->FontSize : ImGui::GetFontSize();
    const float textWidth = pageWidth - cardPadding * 2.0f;
    const float infoHeaderHeight = 66.0f;
    const float infoBottomPadding = 24.0f;

    const std::vector<std::vector<SettingsTextSegment>> infoLines = {
        {
            { "Hey there! It's " },
            { "Lopes", kCreatorProfileUrl, true },
            { "!" }
        },
        {
            { "I whipped up this client to give the community something free, something that actually works, and best of all, it's totally free and safe. If you're out here eating dirt and still paying to cheat in some janky game, then congrats, you're a total loser. And since I absolutely hate it when these clowns sell Minecraft hacks, I decided to bring you EVERYTHING they offer, completely free and Open Source." }
        },
        {
            { "Feel free to use, tweak, or share my client - do whatever the heck you want with it. It's super obvious I suck at design, so don't even think about ragging on me because it looks ugly, or not perfectly pretty, aesthetic, whatever, the modules actually work!" }
        },
        {
            { "If you run into any bugs, errors, something's not working, crashing your game, or causing a memory leak (which, uh, might be happening, my bad), please hit me up on " },
            { "Discord", kDiscordProfileUrl, true },
            { " or open a New Issue on " },
            { "GitHub", kProjectRepositoryUrl, true },
            { "." }
        },
        {
            { "Good luck! 67" }
        }
    };

    float measuredInfoTextHeight = 0.0f;
    for (size_t lineIndex = 0; lineIndex < infoLines.size(); ++lineIndex) {
        measuredInfoTextHeight += DrawWrappedSettingsLine(
            nullptr,
            ImVec2(0.0f, measuredInfoTextHeight),
            textWidth,
            infoLines[lineIndex],
            bodyFont,
            accentFont,
            bodyFontSize,
            bodyColor,
            linkColor,
            linkHoverColor);

        if (lineIndex + 1 < infoLines.size()) {
            measuredInfoTextHeight += 4.0f;
        }
    }

    const float totalContentHeight = infoHeaderHeight + measuredInfoTextHeight + infoBottomPadding + cardGap + updatesHeight + cardGap + buttonHeight + 8.0f;
    const float visibleHeight = (std::max)(120.0f, (m_Height - 40.0f));
    const float maxScroll = (std::max)(0.0f, totalContentHeight - visibleHeight);
    static float s_SettingsScrollOffset = 0.0f;
    s_SettingsScrollOffset = (std::clamp)(s_SettingsScrollOffset, 0.0f, maxScroll);

    const ImVec2 scrollViewMin(pageX, basePageY);
    const ImVec2 scrollViewMax(pageX + pageWidth, basePageY + visibleHeight);
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseHoveringRect(scrollViewMin, scrollViewMax, true) && io.MouseWheel != 0.0f && !ImGui::IsAnyItemActive()) {
        s_SettingsScrollOffset = (std::clamp)(s_SettingsScrollOffset - io.MouseWheel * 42.0f, 0.0f, maxScroll);
    }

    drawList->PushClipRect(scrollViewMin, scrollViewMax, true);
    const float pageY = basePageY - s_SettingsScrollOffset;

    const float infoHeight = infoHeaderHeight + measuredInfoTextHeight + infoBottomPadding;
    const ImVec2 infoMin(pageX, pageY);
    const ImVec2 infoMax(pageX + pageWidth, pageY + infoHeight);
    const ImVec2 updatesMin(pageX, infoMax.y + cardGap);
    const ImVec2 updatesMax(pageX + pageWidth, updatesMin.y + updatesHeight);
    const ImVec2 closeMin(pageX, updatesMax.y + cardGap);
    const ImVec2 closeMax(pageX + pageWidth, closeMin.y + buttonHeight);

    drawList->AddRectFilled(ImVec2(infoMin.x, infoMin.y + 6.0f), ImVec2(infoMax.x, infoMax.y + 6.0f), panelShadowColor, cardRounding);
    drawList->AddRectFilled(infoMin, infoMax, panelColor, cardRounding);
    drawList->AddRectFilled(ImVec2(infoMin.x + 1.0f, infoMin.y + 1.0f), ImVec2(infoMax.x - 1.0f, infoMax.y - 1.0f), panelInnerColor, cardRounding - 1.0f);
    drawList->AddRect(infoMin, infoMax, panelBorderColor, cardRounding, 0, 1.0f);

    drawList->AddRectFilled(ImVec2(updatesMin.x, updatesMin.y + 6.0f), ImVec2(updatesMax.x, updatesMax.y + 6.0f), panelShadowColor, cardRounding);
    drawList->AddRectFilled(updatesMin, updatesMax, panelColor, cardRounding);
    drawList->AddRectFilled(ImVec2(updatesMin.x + 1.0f, updatesMin.y + 1.0f), ImVec2(updatesMax.x - 1.0f, updatesMax.y - 1.0f), panelInnerColor, cardRounding - 1.0f);
    drawList->AddRect(updatesMin, updatesMax, panelBorderColor, cardRounding, 0, 1.0f);

    const float titleY = infoMin.y + 18.0f;
    float titleX = infoMin.x + cardPadding;
    const float lampSize = 24.0f;
    if (m_InfoLampTexture) {
        const ImVec2 lampMin(titleX, titleY - 1.0f);
        const ImVec2 lampMax(lampMin.x + lampSize, lampMin.y + lampSize);
        drawList->AddImageRounded(reinterpret_cast<ImTextureID>(m_InfoLampTexture), lampMin, lampMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, 7.0f, ImDrawFlags_RoundCornersAll);
        titleX += lampSize + 10.0f;
    }

    drawList->AddText(titleFont, titleFontSize, ImVec2(titleX, titleY), titleColor, "Informations");
    drawList->AddLine(ImVec2(infoMin.x + cardPadding, infoMin.y + 52.0f), ImVec2(infoMax.x - cardPadding, infoMin.y + 52.0f), dividerColor, 1.0f);

    float textY = infoMin.y + infoHeaderHeight;

    for (size_t lineIndex = 0; lineIndex < infoLines.size(); ++lineIndex) {
        textY += DrawWrappedSettingsLine(
            drawList,
            ImVec2(infoMin.x + cardPadding, textY),
            textWidth,
            infoLines[lineIndex],
            bodyFont,
            accentFont,
            bodyFontSize,
            bodyColor,
            linkColor,
            linkHoverColor);

        if (lineIndex + 1 < infoLines.size()) {
            textY += 4.0f;
        }
    }

    ReleaseCheckStatus releaseStatus;
    {
        std::lock_guard<std::mutex> lock(g_ReleaseCheckMutex);
        releaseStatus = g_ReleaseCheckStatus;
    }

    const bool isCheckingUpdates = g_ReleaseCheckInProgress.load();
    ImU32 releaseAccentColor = linkColor;
    switch (releaseStatus.state) {
    case ReleaseCheckState::UpToDate:
        releaseAccentColor = IM_COL32(66, 157, 93, 255);
        break;
    case ReleaseCheckState::UpdateAvailable:
        releaseAccentColor = IM_COL32(212, 135, 46, 255);
        break;
    case ReleaseCheckState::Checking:
        releaseAccentColor = IM_COL32(64, 118, 220, 255);
        break;
    case ReleaseCheckState::Error:
        releaseAccentColor = IM_COL32(196, 79, 79, 255);
        break;
    case ReleaseCheckState::LocalBuild:
    case ReleaseCheckState::Idle:
    default:
        releaseAccentColor = linkColor;
        break;
    }

    drawList->AddText(titleFont, titleFontSize, ImVec2(updatesMin.x + cardPadding, updatesMin.y + 16.0f), titleColor, "Updates");

    std::vector<SettingsTextSegment> currentBuildLine = {
        { "Current build: " },
        { releaseStatus.currentLabel, nullptr, true }
    };

    std::vector<SettingsTextSegment> releaseLine;
    switch (releaseStatus.state) {
    case ReleaseCheckState::UpToDate:
        releaseLine = {
            { "You're up to date with " },
            { releaseStatus.latestTag, releaseStatus.latestUrl.c_str(), true },
            { "." }
        };
        break;
    case ReleaseCheckState::UpdateAvailable:
        releaseLine = {
            { "A new release is available: " },
            { releaseStatus.latestTag, releaseStatus.latestUrl.c_str(), true },
            { "." }
        };
        break;
    case ReleaseCheckState::LocalBuild:
        releaseLine = {
            { "Latest published release: " },
            { releaseStatus.latestTag.empty() ? std::string("unknown") : releaseStatus.latestTag, releaseStatus.latestTag.empty() ? nullptr : releaseStatus.latestUrl.c_str(), true },
            { "." }
        };
        break;
    case ReleaseCheckState::Error:
        releaseLine = {
            { releaseStatus.message }
        };
        break;
    case ReleaseCheckState::Checking:
        releaseLine = {
            { "Checking the latest GitHub release..." }
        };
        break;
    case ReleaseCheckState::Idle:
    default:
        releaseLine = {
            { "Click Verify updates to compare this build with the latest GitHub release." }
        };
        break;
    }

    const float updateTextWidth = pageWidth - cardPadding * 2.0f - 184.0f;
    float updateTextY = updatesMin.y + 46.0f;
    updateTextY += DrawWrappedSettingsLine(
        drawList,
        ImVec2(updatesMin.x + cardPadding, updateTextY),
        updateTextWidth,
        currentBuildLine,
        bodyFont,
        accentFont,
        bodyFontSize,
        bodyColor,
        releaseAccentColor,
        linkHoverColor);

    DrawWrappedSettingsLine(
        drawList,
        ImVec2(updatesMin.x + cardPadding, updateTextY - 2.0f),
        updateTextWidth,
        releaseLine,
        bodyFont,
        accentFont,
        bodyFontSize,
        bodyColor,
        releaseAccentColor,
        linkHoverColor);

    const float updateButtonWidth = 168.0f;
    const float updateButtonHeight = 42.0f;
    const ImVec2 updateButtonPos(updatesMax.x - cardPadding - updateButtonWidth, updatesMin.y + (updatesHeight - updateButtonHeight) * 0.5f);
    if (DrawRoundedActionButton(
            drawList,
            "##settings_verify_updates",
            isCheckingUpdates ? "Checking..." : "Verify updates",
            updateButtonPos,
            ImVec2(updateButtonWidth, updateButtonHeight),
            14.0f,
            IM_COL32(43, 115, 232, 255),
            IM_COL32(34, 100, 213, 255),
            IM_COL32(26, 84, 190, 255),
            IM_COL32(27, 86, 178, 255),
            IM_COL32(255, 255, 255, 255),
            accentFont,
            bodyFontSize,
            !isCheckingUpdates)) {
        StartReleaseCheckAsync();
    }

    if (DrawRoundedActionButton(
            drawList,
            "##settings_close_application",
            "Close OpenCommunity Application",
            closeMin,
            ImVec2(pageWidth, buttonHeight),
            16.0f,
            IM_COL32(24, 29, 39, 255),
            IM_COL32(32, 39, 51, 255),
            IM_COL32(42, 49, 61, 255),
            IM_COL32(18, 22, 30, 255),
            IM_COL32(247, 249, 252, 255),
            accentFont,
            bodyFontSize,
            true)) {
        m_State = AppState::Closing;
        m_ClosingStartTime = static_cast<float>(ImGui::GetTime());
    }

    drawList->PopClipRect();

    ImGui::SetCursorScreenPos(ImVec2(pageX, closeMax.y + 2.0f));
    ImGui::Dummy(ImVec2(pageWidth, 1.0f));
}

void Screen::RenderClosing() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(m_Width, m_Height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("ClosingAnim", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        float t = (float)ImGui::GetTime() - m_ClosingStartTime;

        const float squeezeDuration = 0.6f;
        const float textStart = squeezeDuration + 0.2f;
        const char* message = "Bye, see you again soon.";
        const int msgLen = static_cast<int>(strlen(message));
        const float charsPerSec = 18.0f;
        const float totalDuration = textStart + (msgLen / charsPerSec) + 1.0f;

        dl->AddRectFilled(wp, ImVec2(wp.x + m_Width, wp.y + m_Height), IM_COL32(255, 255, 255, 255));

        if (t < squeezeDuration) {
            float progress = t / squeezeDuration;
            float ease = 1.0f - (1.0f - progress) * (1.0f - progress);
            float barW = (m_Width * 0.5f) * ease;

            dl->AddRectFilled(wp, ImVec2(wp.x + barW, wp.y + m_Height), IM_COL32(241, 244, 248, 255));
            dl->AddRectFilled(ImVec2(wp.x + m_Width - barW, wp.y), ImVec2(wp.x + m_Width, wp.y + m_Height), IM_COL32(241, 244, 248, 255));
        } else {
            dl->AddRectFilled(wp, ImVec2(wp.x + m_Width, wp.y + m_Height), IM_COL32(255, 255, 255, 255));

            if (t >= textStart) {
                float textT = t - textStart;
                int charsToShow = (int)(textT * charsPerSec);
                if (charsToShow > msgLen) charsToShow = msgLen;

                char buf[64] = {};
                memcpy(buf, message, charsToShow);
                buf[charsToShow] = '\0';

                ImFont* font = m_FontTitle ? m_FontTitle : ImGui::GetFont();
                float fontSize = font->FontSize;
                ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, buf);
                float tx = wp.x + (m_Width - textSize.x) * 0.5f;
                float ty = wp.y + (m_Height - textSize.y) * 0.5f;

                dl->AddText(font, fontSize, ImVec2(tx, ty), IM_COL32(20, 24, 32, 255), buf);
            }
        }

        if (t >= totalDuration) {
            m_Running = false;
        }

        ImGui::End();
    }
}

void Screen::RenderMainInterfaceLayer(const char* windowName, const ImVec2& windowPos, bool interactive, float overlayAlpha) {
    FeatureManager::Get()->SyncAllFromConfig(Bridge::Get()->GetConfig());

    ImGui::SetNextWindowPos(windowPos);
    ImGui::SetNextWindowSize(ImVec2(m_Width, m_Height));
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground;
    if (!interactive) {
        flags |= ImGuiWindowFlags_NoInputs;
    }
    
    if (ImGui::Begin(windowName, nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();

        dl->AddRectFilled(wp, ImVec2(wp.x + m_Width, wp.y + m_Height), IM_COL32(255, 255, 255, 255));
        DrawTopographicBackground(dl, wp, m_Width, m_Height, 0.0f);

        const float sidebarW = 52.0f;
        const float iconSize = 30.0f;
        const float iconPadY = 20.0f;
        const float totalIconsH = 4 * iconSize + 3 * iconPadY;
        const float startY = wp.y + (m_Height - totalIconsH) * 0.5f;

        const float lineX = wp.x + sidebarW;
        const float lineH = (m_Height - 20.0f) / 1.5f;
        const float lineMid = wp.y + m_Height * 0.5f;
        const float lineTop = lineMid - lineH * 0.5f;
        const float lineBottom = lineMid + lineH * 0.5f;
        dl->AddLine(ImVec2(lineX, lineTop), ImVec2(lineX, lineBottom), IM_COL32(200, 200, 200, 255), 1.0f);

        ID3D11ShaderResourceView* icons[4] = { m_IconCombat, m_IconMovement, m_IconVisuals, m_IconSettings };

        for (int i = 0; i < 4; i++) {
            float iconY = startY + i * (iconSize + iconPadY);
            float iconX = wp.x + (sidebarW - iconSize) * 0.5f;

            ImVec2 iconMin(iconX, iconY);
            ImVec2 iconMax(iconX + iconSize, iconY + iconSize);

            ImGui::SetCursorScreenPos(iconMin);
            char btnId[32];
            snprintf(btnId, sizeof(btnId), "##icon_%d", i);
            if (ImGui::InvisibleButton(btnId, ImVec2(iconSize, iconSize))) {
                m_CurrentTab = i;
            }

            if (icons[i]) {
                ImU32 tintCol;
                if (m_CurrentTab == i) {
                    float hue = fmodf((float)ImGui::GetTime() * 0.5f, 1.0f);
                    ImVec4 rgb;
                    ImGui::ColorConvertHSVtoRGB(hue, 0.85f, 1.0f, rgb.x, rgb.y, rgb.z);
                    tintCol = IM_COL32((int)(rgb.x * 255), (int)(rgb.y * 255), (int)(rgb.z * 255), 255);
                } else {
                    tintCol = IM_COL32(0, 0, 0, 255);
                }
                dl->AddImage((ImTextureID)icons[i], iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), tintCol);
            }
        }

        const float contentX = sidebarW + 16.0f;
        ImGui::SetCursorScreenPos(ImVec2(wp.x + contentX, wp.y + 16.0f));
        ImGui::BeginGroup();
        ImGui::PushClipRect(ImVec2(wp.x + contentX, wp.y), ImVec2(wp.x + m_Width, wp.y + m_Height), true);

        switch (m_CurrentTab) {
        case 0: RenderCombatTab(); break;
        case 1: RenderMovementTab(); break;
        case 2: RenderVisualsTab(); break;
        case 3: RenderSettingsTab(); break;
        }

        ImGui::PopClipRect();
        ImGui::EndGroup();

        if (overlayAlpha > 0.0f) {
            dl->AddRectFilled(
                wp,
                ImVec2(wp.x + m_Width, wp.y + m_Height),
                IM_COL32(255, 255, 255, static_cast<int>(255.0f * Clamp01(overlayAlpha))));
        }

        ImGui::End();
    }


    // Automatically process keybinds and sync all modules to shared memory
    if (interactive) {
        FeatureManager::Get()->UpdateFrontdoor(Bridge::Get()->GetConfig());
    }
}

void Screen::RenderMainInterface() {
    RenderMainInterfaceLayer("OpenCommunity", ImVec2(0.0f, 0.0f), true, 0.0f);
}

void Screen::RenderTransitionToInterface() {
    if (m_InterfaceTransitionStartTime < 0.0f) {
        m_InterfaceTransitionStartTime = static_cast<float>(ImGui::GetTime());
    }

    const float now = static_cast<float>(ImGui::GetTime());
    const float progress = Clamp01((now - m_InterfaceTransitionStartTime) / 0.80f);
    const float eased = EaseInOutCubic(progress);

    const float injectAlpha = 1.0f - eased;
    const float injectOffsetY = -10.0f * eased;
    const float injectScale = 1.0f - 0.02f * eased;
    RenderInjectingLayer("InjectingTransition", injectAlpha, injectOffsetY, injectScale, kInjectedHeadline, 99.0f, true, true);

    const float interfaceOffsetY = 22.0f * (1.0f - eased);
    const float overlayAlpha = 0.34f * (1.0f - eased);
    RenderMainInterfaceLayer("OpenCommunityTransition", ImVec2(0.0f, interfaceOffsetY), false, overlayAlpha);

    ImDrawList* foreground = ImGui::GetForegroundDrawList();
    if (foreground) {
        const float veilAlpha = 0.18f * (1.0f - eased);
        if (veilAlpha > 0.0f) {
            foreground->AddRectFilled(
                ImVec2(0.0f, 0.0f),
                ImVec2(m_Width, m_Height),
                IM_COL32(255, 255, 255, static_cast<int>(255.0f * veilAlpha)));
        }
    }

    if (progress >= 1.0f) {
        m_State = AppState::MainInterface;
        m_InterfaceTransitionStartTime = -1.0f;
    }
}


void Screen::Render() {
    const ImVec4 clear = color::GetBackgroundVec4();
    const float clearColor[4] = { clear.x, clear.y, clear.z, clear.w };
    
    m_Context->OMSetRenderTargets(1, &m_RenderTarget, nullptr);
    m_Context->ClearRenderTargetView(m_RenderTarget, clearColor);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const float deltaTime = (std::max)(ImGui::GetIO().DeltaTime, 1.0f / 240.0f);
    const float overlayTarget = m_IsWindowMoveActive ? 1.0f : 0.0f;
    const float overlayBlend = 1.0f - std::exp(-deltaTime * 18.0f);
    m_WindowMoveOverlayAlpha += (overlayTarget - m_WindowMoveOverlayAlpha) * overlayBlend;
    m_WindowMoveOverlayAlpha = (std::clamp)(m_WindowMoveOverlayAlpha, 0.0f, 1.0f);
    
    switch (m_State) {
    case AppState::Intro:
        RenderIntro();
        break;
    case AppState::InstanceChooser:
        RenderInstanceChooser();
        break;
    case AppState::Injecting:
        RenderInjecting();
        break;
    case AppState::TransitionToInterface:
        RenderTransitionToInterface();
        break;
    case AppState::MainInterface:
        RenderMainInterface();
        break;
    case AppState::Closing:
        RenderClosing();
        break;
    }

    if (m_WindowMoveOverlayAlpha > 0.001f) {
        DrawWindowMoveBlurOverlay(
            ImGui::GetForegroundDrawList(),
            ImVec2(0.0f, 0.0f),
            m_Width,
            m_Height,
            static_cast<float>(ImGui::GetTime()),
            m_WindowMoveOverlayAlpha,
            m_FontHero ? m_FontHero : m_FontBold,
            m_FontHero ? 34.0f : 28.0f);
    }

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    
    m_SwapChain->Present(1, 0);
}

void Screen::Run() {
    if (!Initialize()) {
        return;
    }

    MSG msg = {};
    while (m_Running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                m_Running = false;
            }
        }
        
        if (m_Minimized) {
            Sleep(50);
            continue;
        }

        if (auto* info = Singleton<ClientInfo>::Get()) {
            if (info->m_TargetPid != 0 && !proc::IsProcessRunning(info->m_TargetPid)) {
                m_Running = false;
            }
        }
        
        if (!m_Running) break;

        Render();
    }

    if (auto* config = Bridge::Get()->GetConfig()) {
        config->m_Destruct = true;
    }

    Sleep(500);
}

void Screen::Close() {
    m_Running = false;
}
