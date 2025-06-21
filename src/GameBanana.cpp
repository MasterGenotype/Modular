#include "GameBanana.h"
#include "nlohmann/json.hpp"
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t totalSize = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

void initialize()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void cleanup()
{
    curl_global_cleanup();
}

std::string httpGet(const std::string& url)
{
    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "Error: curl_easy_perform() failed for URL " << url << "\n"
                      << "       " << curl_easy_strerror(res) << std::endl;
        }
        curl_easy_cleanup(curl);
    }
    return response;
}

size_t WriteFileCallback(void* ptr, size_t size, size_t nmemb, void* stream)
{
    return fwrite(ptr, size, nmemb, (FILE*)stream);
}

bool downloadFile(const std::string& url, const std::string& outputPath)
{
    CURL* curl = curl_easy_init();
    if (curl) {
        FILE* fp = fopen(outputPath.c_str(), "wb");
        if (!fp) {
            std::cerr << "Error: Could not open file " << outputPath << " for writing." << std::endl;
            curl_easy_cleanup(curl);
            return false;
        }
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        CURLcode res = curl_easy_perform(curl);
        fclose(fp);
        curl_easy_cleanup(curl);
        return res == CURLE_OK;
    }
    return false;
}

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
    std::string response = httpGet(url);
    std::vector<std::pair<std::string, std::string>> mods;
    if (response.empty())
        return mods;
    json subsJson = json::parse(response);
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
    std::string response = httpGet(url);
    std::vector<std::string> urls;
    if (response.empty())
        return urls;
    json fileListJson = json::parse(response);
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
        downloadFile(url, outputPath);
    }
}
