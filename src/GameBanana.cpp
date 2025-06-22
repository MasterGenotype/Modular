#include "GameBanana.h"
#include "HTTPClient.h"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <iostream>

using json = nlohmann::json;
namespace fs = std::filesystem;

std::string sanitizeFilename(const std::string& name)
{
    std::string sanitized = name;
    for (char& c : sanitized) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    return sanitized;
}

std::string extractModId(const std::string& profileUrl)
{
    const std::string marker = "/mods/";
    size_t pos = profileUrl.find(marker);
    return (pos != std::string::npos) ? profileUrl.substr(pos + marker.length()) : "";
}

std::string extractFileName(const std::string& downloadUrl)
{
    size_t pos = downloadUrl.find_last_of("/");
    return (pos != std::string::npos && pos + 1 < downloadUrl.size()) ? downloadUrl.substr(pos + 1) : "downloaded_file";
}

std::vector<std::pair<std::string, std::string>> fetchSubscribedMods(const std::string& userId)
{
    std::string url = "https://gamebanana.com/apiv11/Member/" + userId + "/Subscriptions";
    HTTPClient::HttpResponse response = HTTPClient::http_get(url);
    std::vector<std::pair<std::string, std::string>> mods;
    if (response.status_code != 200 || response.body.empty())
        return mods;
    json subsJson = json::parse(response.body);
    if (!subsJson.contains("_aRecords"))
        return mods;
    for (const auto& record : subsJson["_aRecords"]) {
        if (record.contains("_aSubscription")) {
            json subscription = record["_aSubscription"];
            if (subscription.contains("_sSingularTitle") && subscription["_sSingularTitle"] == "Mod" && subscription.contains("_sProfileUrl") && subscription.contains("_sName")) {
                mods.emplace_back(subscription["_sProfileUrl"].get<std::string>(),
                    subscription["_sName"].get<std::string>());
            }
        }
    }
    return mods;
}

std::vector<std::string> fetchModFileUrls(const std::string& modId)
{
    std::string url = "https://gamebanana.com/apiv11/Mod/" + modId + "?_csvProperties=_aFiles";
    HTTPClient::HttpResponse response = HTTPClient::http_get(url);
    std::vector<std::string> urls;
    if (response.status_code != 200 || response.body.empty())
        return urls;
    json fileListJson = json::parse(response.body);
    if (!fileListJson.contains("_aFiles"))
        return urls;
    for (const auto& fileEntry : fileListJson["_aFiles"]) {
        if (fileEntry.contains("_sDownloadUrl")) {
            urls.push_back(fileEntry["_sDownloadUrl"].get<std::string>());
        }
    }
    return urls;
}

void downloadModFiles(const std::string& modId, const std::string& modName, const std::string& baseDir)
{
    std::string modFolder = baseDir + "/" + sanitizeFilename(modName);
    fs::create_directories(modFolder);
    std::vector<std::string> downloadUrls = fetchModFileUrls(modId);
    int fileCount = 0;
    for (const auto& url : downloadUrls) {
        std::string outputPath = modFolder + "/" + std::to_string(++fileCount) + "_" + extractFileName(url);
        HTTPClient::download_file(url, outputPath);
    }
}
