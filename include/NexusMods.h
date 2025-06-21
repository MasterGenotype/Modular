#ifndef NEXUSMODS_H
#define NEXUSMODS_H

#include <chrono>
#include <cstdlib>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

extern std::string API_KEY;

// A small utility struct to store HTTP response data
struct HttpResponse {
    long status_code;
    std::string body;
};

// Function declarations (exactly as in the original code)
HttpResponse http_get(const std::string& url, const std::vector<std::string>& headers);
std::string escape_spaces(const std::string& url);
std::vector<int> get_tracked_mods();
std::map<int, std::vector<int>> get_file_ids(const std::vector<int>& mod_ids, const std::string& game_domain);
std::map<std::pair<int, int>, std::string> generate_download_links(const std::map<int, std::vector<int>>& mod_file_ids, const std::string& game_domain);
void save_download_links(const std::map<std::pair<int, int>, std::string>& download_links, const std::string& game_domain, const fs::path& base_dir);
void download_files(const std::string& game_domain, const fs::path& base_dir);

#endif // NEXUSMODS_H
