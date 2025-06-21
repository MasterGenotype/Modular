#ifndef CONFIG_H
#define CONFIG_H

#include <filesystem>
#include <optional>
#include <string>

namespace fs = std::filesystem;

struct AppConfig {
    std::string mods_directory;
    std::string nexus_api_key;
    std::string gb_user_id;
};

// Initializes the application: loads config or runs setup.
AppConfig initialize_app();

#endif // CONFIG_H