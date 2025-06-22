#include "GameBanana.h"
#include "NexusMods.h"
#include "HTTPClient.h"
#include "Config.h"
#include "Rename.h"
#include <cstdlib> // for std::getenv
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream> // for std::istringstream if we parse user input
#include <string>
#include <vector>

namespace fs = std::filesystem;

//--------------------------------------------------
// Run all GameBanana steps in one sequence
//--------------------------------------------------
void runGameBananaSequence(const AppConfig& config)
{
    // Use GameBanana user ID from config
    std::string userId = config.gb_user_id;
    if (userId.empty()) {
        std::cerr << "Error: GameBanana User ID is not set in the configuration.\n";
        return;
    }
    std::cout << "Using GameBanana user ID from config: " << userId << "\n";

    // Fetch all subscribed mods
    auto mods = fetchSubscribedMods(userId);
    if (mods.empty()) {
        std::cout << "No subscribed mods found for user ID: " << userId << "\n";
        return;
    }

    std::cout << "\nFound " << mods.size() << " subscribed mods.\n";
    for (const auto& mod : mods) {
        std::cout << "  - " << mod.second << "\n";
    }

    // Use base directory from config
    const std::string& baseDir = config.mods_directory;

    // Download all detected mods
    std::cout << "\nStarting download of all subscribed mods...\n";

    for (const auto& mod : mods) {
        std::string modUrl = mod.first;
        std::string modName = sanitizeFilename(mod.second);

        // Extract Mod ID from URL
        std::string modId = extractModId(modUrl);
        if (modId.empty()) {
            std::cerr << "Warning: Failed to extract mod ID from URL: " << modUrl << "\n";
            continue;
        }

        std::cout << "Downloading Mod: " << modName << " (ID: " << modId << ")...\n";
        downloadModFiles(modId, modName, baseDir);
    }

    std::cout << "\nAll subscribed mods have been downloaded to: " << baseDir << "\n";
}

//--------------------------------------------------
// Helper: Run the NexusMods workflow for a single domain
//--------------------------------------------------
void runNexusModsForOneDomain(const AppConfig& config, const std::vector<int>& trackedMods, const std::string& gameDomain)
{
    // Get file IDs
    auto fileIdsMap = get_file_ids(config, trackedMods, gameDomain);

    // Generate download links
    auto downloadLinks = generate_download_links(config, fileIdsMap, gameDomain);
    std::cout << "\nGenerated Download Links for domain '" << gameDomain << "':\n";
    for (auto& [modFilePair, url] : downloadLinks) {
        std::cout << "  ModID: " << modFilePair.first
                  << ", FileID: " << modFilePair.second
                  << " => " << url << "\n";
    }

    // Save download links
    save_download_links(downloadLinks, gameDomain, config.mods_directory);
    std::cout << "Download links saved for domain '" << gameDomain << "'.\n";

    // Download files
    download_files(gameDomain, config.mods_directory);
    std::cout << "Files downloaded for domain '" << gameDomain << "'.\n";
}

//--------------------------------------------------
// Run the NexusMods steps for multiple domains
//--------------------------------------------------
void runNexusModsSequence(const AppConfig& config, const std::vector<std::string>& domains)
{
    // Get tracked mods once
    std::vector<int> trackedMods = get_tracked_mods(config);
    std::cout << "\nFound " << trackedMods.size() << " tracked mods.\n";
    for (int modId : trackedMods) {
        std::cout << "  " << modId << "\n";
    }

    // Run the pipeline for each domain
    for (const auto& domain : domains) {
        std::cout << "\n===== Processing Domain: " << domain << " =====\n";
        runNexusModsForOneDomain(config, trackedMods, domain);
    }
}

//--------------------------------------------------
// Run all Rename steps in one sequence
//--------------------------------------------------
void runRenameSequence(const AppConfig& config)
{
    fs::path modsDir = config.mods_directory;
    std::cout << "Using mods directory: " << modsDir.string() << "\n";

    auto gameDomains = getGameDomainNames(modsDir);
    if (gameDomains.empty()) {
        std::cerr << "No game domains found in: " << modsDir << "\n";
        return;
    }

    for (const auto& gameDomain : gameDomains) {
        fs::path gameDomainPath = modsDir / gameDomain;
        std::cout << "\nProcessing game domain: " << gameDomain << "\n";

        auto modIDs = getModIDs(gameDomainPath);
        if (modIDs.empty()) {
            std::cerr << "No mod IDs found in: " << gameDomainPath << "\n";
            continue;
        }

        for (const auto& modID : modIDs) {
            std::cout << "\nFetching mod name for modID: " << modID << "\n";
            std::string jsonResponse = fetchModName(config, gameDomain, modID);
            std::cout << "JSON response: " << jsonResponse << "\n";

            std::string rawModName = extractModName(jsonResponse);
            if (rawModName.empty()) {
                std::cerr << "No mod name found for modID: " << modID << "\n";
                continue;
            }

            std::string modName = sanitizeFilename(rawModName);
            fs::path oldPath = gameDomainPath / modID;
            fs::path newPath = gameDomainPath / modName;
            std::cout << "Renaming: " << oldPath << " -> " << newPath << "\n";

            try {
                fs::rename(oldPath, newPath);
                std::cout << "Renamed " << modID << " to " << modName << " in " << gameDomain << "\n";
            } catch (const fs::filesystem_error& e) {
                std::cerr << "Failed to rename " << oldPath << " to " << newPath << ": " << e.what() << "\n";
            }
        }
    }
}

//--------------------------------------------------
// Command-line Interface Logic
//--------------------------------------------------
void show_menu() {
    std::cout << "\n---------------------------------------\n";
    std::cout << "\n============== Main Menu ==============\n";
    std::cout << "1. Run GameBanana Sequence\n";
    std::cout << "2. Run NexusMods Sequence\n";
    std::cout << "3. Run Rename Sequence (for NexusMods downloads)\n";
    std::cout << "4. Run NexusMods Backup Scraper\n";
    std::cout << "0. Exit\n";
    std::cout << "=======================================\n";
    std::cout << "Enter your choice: ";
}

int run_interactive_mode(const AppConfig& config) {
    bool running = true;
    while (running) {
        show_menu();

        int choice;
        std::cin >> choice;

        // Handle invalid input
        if (!std::cin) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Invalid input, please try again.\n";
            continue;
        }

        // Consume the rest of the line to prevent issues with subsequent `getline` calls.
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        switch (choice) {
        case 0:
            running = false;
            break;
        case 1:
            runGameBananaSequence(config);
            break;
        case 2: {
            std::vector<std::string> gameDomains;
            std::cout << "Enter one or more game domains (space-separated), then press ENTER:\n> ";
            std::string domainsLine;
            std::getline(std::cin, domainsLine);

            std::istringstream iss(domainsLine);
            std::string domain;
            while (iss >> domain) {
                gameDomains.push_back(domain);
            }

            if (gameDomains.empty()) {
                std::cout << "No domains specified. Returning to main menu.\n";
                break;
            }
            runNexusModsSequence(config, gameDomains);
            break;
        }
        case 3:
            runRenameSequence(config);
            break;
        case 4:
            runNexusBackupScraper(config);
            break;
        default:
            std::cout << "Invalid choice. Please try again.\n";
            break;
        }
    }
    return 0;
}

int run_direct_command(int argc, char* argv[], const AppConfig& config) {
    std::string command = argv[1];
    if (command == "gamebanana" || command == "1") {
        runGameBananaSequence(config);
    } else if (command == "nexus" || command == "2") {
        if (argc < 3) {
            std::cerr << "Error: The 'nexus' command requires at least one game domain.\n";
            std::cerr << "Usage: " << argv[0] << " nexus <game_domain_1> [game_domain_2] ...\n";
            return 1;
        }
        std::vector<std::string> gameDomains;
        for (int i = 2; i < argc; ++i) {
            gameDomains.push_back(argv[i]);
        }
        runNexusModsSequence(config, gameDomains);
    } else if (command == "rename" || command == "3") {
        runRenameSequence(config);
    } else if (command == "scraper" || command == "4") {
        runNexusBackupScraper(config);
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        std::cerr << "Available commands: gamebanana, nexus, rename, scraper\n";
        return 1;
    }
    return 0;
}

//--------------------------------------------------
// Main
//--------------------------------------------------
int main(int argc, char* argv[])
{
    HTTPClient::initialize(); // Initialize global resources once.
    int exit_code = 0;

    try {
        // Determine executable path for script finding
        fs::path executable_path;
        std::error_code ec;
        executable_path = fs::canonical(argv[0], ec);
        if (ec) {
            std::cerr << "Warning: Could not determine canonical path for executable. Using provided path. Error: " << ec.message() << std::endl;
            executable_path = argv[0];
        }

        // Initialize configuration
        auto config_opt = initialize_app(executable_path);
        if (!config_opt) {
            throw std::runtime_error("Failed to initialize configuration. Exiting.");
        }
        AppConfig config = *config_opt;

        if (argc > 1) {
            // Direct command mode
            exit_code = run_direct_command(argc, argv, config);
        } else {
            // Interactive menu mode
            exit_code = run_interactive_mode(config);
        }

    } catch (const std::exception& e) {
        std::cerr << "An unhandled exception occurred: " << e.what() << std::endl;
        exit_code = 1;
    }

    HTTPClient::cleanup(); // Cleanup global resources before exiting.
    return exit_code;
}
