#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace fs = std::filesystem;

struct AppConfig {
    std::string mods_directory;
    std::string nexus_api_key;
    std::string gb_user_id;
    std::string nexus_cookie_path;
    fs::path executable_path;
};

// Initializes the application configuration, running first-time setup if needed.
// Returns an empty optional if setup fails or is aborted.
std::optional<AppConfig> initialize_app(const fs::path& exec_path);