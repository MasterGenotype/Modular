#include "Rename.h"
#include "HTTPClient.h"
#include <iostream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace { // Anonymous namespace for internal helper function
std::vector<std::string> getSubdirectoryNames(const fs::path& directory) {
    std::vector<std::string> names;
    if (!fs::exists(directory)) {
        std::cerr << "Directory does not exist: " << directory << std::endl;
        return names;
    }
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_directory()) {
            names.push_back(entry.path().filename().string());
        }
    }
    return names;
}
} // end anonymous namespace

std::vector<std::string> getGameDomainNames(const fs::path& modsListsDir)
{
    return getSubdirectoryNames(modsListsDir);
}

std::vector<std::string> getModIDs(const fs::path& gameDomainPath)
{
    return getSubdirectoryNames(gameDomainPath);
}

std::string fetchModName(const AppConfig& config, const std::string& gameDomain, const std::string& modID)
{
    // Construct the API URL.
    std::string url = "https://api.nexusmods.com/v1/games/" + gameDomain + "/mods/" + modID;

    // Use the API_KEY from the config object.
    std::vector<std::string> headers = {"apikey: " + config.nexus_api_key};

    HTTPClient::HttpResponse resp = HTTPClient::http_get(url, headers);
    if (resp.status_code != 200) {
        std::cerr << "Failed to fetch mod name for " << modID << ". Status: " << resp.status_code << std::endl;
        return "";
    }
    return resp.body;
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
