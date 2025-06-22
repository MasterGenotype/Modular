#include "Config.h"
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace { // Anonymous namespace for internal linkage

fs::path get_config_dir() {
    const char* homeEnv = std::getenv("HOME");
    if (!homeEnv) {
        throw std::runtime_error("FATAL: HOME environment variable not set. Cannot determine config directory.");
    }
    return fs::path(homeEnv) / ".config" / "Modular";
}

fs::path get_config_file_path() {
    return get_config_dir() / "config.json";
}

std::optional<AppConfig> load_config() {
    fs::path config_path = get_config_file_path();
    if (!fs::exists(config_path)) {
        return std::nullopt;
    }

    std::ifstream f(config_path);
    if (!f.is_open()) {
        std::cerr << "Error: Could not open config file for reading: " << config_path << std::endl;
        return std::nullopt;
    }

    try {
        json data = json::parse(f);
        AppConfig config;
        config.mods_directory = data.at("mods_directory").get<std::string>();
        config.nexus_api_key = data.at("nexus_api_key").get<std::string>();
        config.gb_user_id = data.value("gb_user_id", "");                 // GameBanana ID is optional
        config.nexus_cookie_path = data.value("nexus_cookie_path", ""); // Scraper cookie path is optional
        // executable_path is a runtime property, not stored in the file.
        return config;
    } catch (const json::exception& e) {
        std::cerr << "Error parsing config file: " << e.what() << std::endl;
        std::cerr << "Please fix or delete the config file at: " << config_path << std::endl;
        return std::nullopt;
    }
}

bool save_config(const AppConfig& config) {
    fs::path config_dir = get_config_dir();
    try {
        fs::create_directories(config_dir);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error creating config directory: " << e.what() << std::endl;
        return false;
    }

    fs::path config_path = get_config_file_path();
    json data = {
        {"mods_directory", config.mods_directory},
        {"nexus_api_key", config.nexus_api_key},
        {"gb_user_id", config.gb_user_id},
        {"nexus_cookie_path", config.nexus_cookie_path}};

    std::ofstream o(config_path);
    if (!o.is_open()) {
        std::cerr << "Error: Could not open config file for writing: " << config_path << std::endl;
        return false;
    }

    o << std::setw(4) << data << std::endl;
    return true;
}

std::optional<AppConfig> run_initial_setup() {
    AppConfig config;
    std::cout << "--- First Time Setup ---\n";
    std::cout << "It looks like this is your first time running Modular, or the config file is missing.\n";
    std::cout << "Let's get a few things configured.\n\n";

    // 1. Mods Directory
    std::cout << "Enter the full path to the directory where you want to store downloaded mods.\n";
    const char* homeEnv = std::getenv("HOME");
    std::string default_mods_dir = (homeEnv) ? (fs::path(homeEnv) / "Games" / "Mods-Lists").string() : "Games/Mods-Lists";
    std::cout << "Press ENTER for default (" << default_mods_dir << "): ";
    std::string mods_dir_input;
    std::getline(std::cin, mods_dir_input);

    config.mods_directory = mods_dir_input.empty() ? default_mods_dir : mods_dir_input;

    try {
        fs::create_directories(config.mods_directory);
        std::cout << "Mods directory set to: " << config.mods_directory << "\n\n";
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Warning: Could not create mods directory: " << e.what() << "\n\n";
    }

    // 2. NexusMods API Key
    while (config.nexus_api_key.empty()) {
        std::cout << "Please enter your NexusMods API Key (found on your NexusMods account page):\n> ";
        std::getline(std::cin, config.nexus_api_key);
        if (config.nexus_api_key.empty()) {
            std::cout << "API Key cannot be empty. Please try again.\n";
        }
    }

    // 3. GameBanana User ID
    std::cout << "\nPlease enter your GameBanana User ID (optional, can be left blank):\n> ";
    std::getline(std::cin, config.gb_user_id);

    // 4. NexusMods Cookie Path
    std::cout << "\nPlease enter the full path to your NexusMods cookies.json file (for the backup scraper).\n";
    std::cout << "This is optional and can be left blank. You can get this file from your browser using an extension.\n> ";
    std::getline(std::cin, config.nexus_cookie_path);

    if (save_config(config)) {
        std::cout << "\nConfiguration saved successfully to " << get_config_file_path() << "\n";
    } else {
        std::cerr << "\nFATAL: Failed to save configuration.\n";
        return std::nullopt;
    }

    std::cout << "--- Setup Complete ---\n\n";
    return {config};
}

} // end anonymous namespace

std::optional<AppConfig> initialize_app(const fs::path& exec_path) {
    auto config_opt = load_config();
    auto final_config = config_opt.has_value() ? config_opt : run_initial_setup();
    if (final_config) final_config->executable_path = exec_path;
    return final_config;
}