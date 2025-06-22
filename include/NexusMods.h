#pragma once

#include "Config.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

std::vector<int> get_tracked_mods(const AppConfig& config);

std::map<int, std::vector<int>> get_file_ids(const AppConfig& config,
    const std::vector<int>& mod_ids,
    const std::string& game_domain);

std::map<std::pair<int, int>, std::string> generate_download_links(const AppConfig& config,
    const std::map<int, std::vector<int>>& mod_file_ids,
    const std::string& game_domain);

void save_download_links(const std::map<std::pair<int, int>, std::string>& download_links,
    const std::string& game_domain,
    const fs::path& base_dir);

void download_files(const std::string& game_domain,
    const fs::path& base_dir);

void runNexusBackupScraper(const AppConfig& config);