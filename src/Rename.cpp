#include "Rename.h"
#include <curl/curl.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include "NexusMods.h" // For API_KEY

namespace fs = std::filesystem;
using json = nlohmann::json;

// This callback will be used by libcurl to write received data into a std::string.
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    std::string* str = static_cast<std::string*>(userp);
    size_t totalSize = size * nmemb;
    str->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

std::vector<std::string> getGameDomainNames(const fs::path& modsListsDir)
{
    std::vector<std::string> domains;

    if (!fs::exists(modsListsDir)) {
        std::cerr << "Directory does not exist: " << modsListsDir << std::endl;
        return domains;
    }

    for (const auto& entry : fs::directory_iterator(modsListsDir)) {
        if (entry.is_directory()) {
            domains.push_back(entry.path().filename().string());
        }
    }
    return domains;
}

std::vector<std::string> getModIDs(const fs::path& gameDomainPath)
{
    std::vector<std::string> modIDs;

    if (!fs::exists(gameDomainPath)) {
        std::cerr << "Directory does not exist: " << gameDomainPath << std::endl;
        return modIDs;
    }

    for (const auto& entry : fs::directory_iterator(gameDomainPath)) {
        if (entry.is_directory()) {
            modIDs.push_back(entry.path().filename().string());
        }
    }
    return modIDs;
}

std::string fetchModName(const std::string& gameDomain, const std::string& modID)
{
    CURL* curl = curl_easy_init();
    std::string readBuffer;

    if (curl) {
        // Construct the API URL.
        std::string url = "https://api.nexusmods.com/v1/games/" + gameDomain + "/mods/" + modID;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // Use the global API_KEY, which should be set by main().

        // Set the API key as an HTTP header.
        std::string headerStr = "apikey: " + API_KEY;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, headerStr.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Perform the API request.
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << "\n";
        }

        // Cleanup.
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    return readBuffer;
}

std::string extractModName(const std::string& jsonResponse)
{
    try {
        auto j = json::parse(jsonResponse);
        if (j.contains("name")) {
            return j["name"].get<std::string>();
        }
    } catch (const std::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
    }
    return "";
}

void combineDirectories(const fs::path& target, const fs::path& source)
{
    if (!fs::exists(target)) {
        fs::create_directories(target);
    }
    // Iterate over every item in the source directory.
    for (const auto& entry : fs::directory_iterator(source)) {
        fs::path dest = target / entry.path().filename();
        if (entry.is_directory()) {
            // Recursively merge subdirectories.
            combineDirectories(dest, entry.path());
        } else {
            // Copy (or overwrite) the file.
            fs::copy(entry.path(), dest, fs::copy_options::overwrite_existing);
        }
    }
}
