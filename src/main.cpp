#include "GameBanana.h"
#include "NexusMods.h"
#include "Config.h"
#include "Rename.h"
#include <cstdlib> // for std::getenv
#include <filesystem>
#include <iostream>
#include <sstream> // for std::istringstream if we parse user input
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Define the global API_KEY declared in NexusMods.h
std::string API_KEY = "";

//--------------------------------------------------
// Run all GameBanana steps in one sequence
//--------------------------------------------------
void runGameBananaSequence(const AppConfig& config)
{
    // 1) Initialize GameBanana
    initialize();
    std::cout << "GameBanana initialized.\n";

    // 2) Use GameBanana user ID from config
    std::string userId = config.gb_user_id;
    if (userId.empty()) {
        std::cerr << "Error: GameBanana User ID is not set in the configuration.\n";
        return;
    }
    std::cout << "Using GameBanana user ID from config: " << userId << "\n";

    // 3) Fetch all subscribed mods
    auto mods = fetchSubscribedMods(userId);
    if (mods.empty()) {
        std::cout << "No subscribed mods found for user ID: " << userId << "\n";
        return;
    }

    std::cout << "Subscribed Mods:\n";
    for (const auto& mod : mods) {
        std::cout << "Profile URL: " << mod.first << " | Mod Name: " << mod.second << "\n";
    }

    // 4) Use base directory from config
    const std::string& baseDir = config.mods_directory;

    // 5) Download all detected mods
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

    // 6) Cleanup
    cleanup();
    std::cout << "GameBanana cleanup complete.\n";
}

//--------------------------------------------------
// Helper: Run the NexusMods workflow for a single domain
//--------------------------------------------------
void runNexusModsForOneDomain(const std::vector<int>& trackedMods, const std::string& gameDomain, const fs::path& base_dir)
{
    // Get file IDs
    auto fileIdsMap = get_file_ids(trackedMods, gameDomain);

    // Generate download links
    auto downloadLinks = generate_download_links(fileIdsMap, gameDomain);
    std::cout << "\nGenerated Download Links for domain '" << gameDomain << "':\n";
    for (auto& [modFilePair, url] : downloadLinks) {
        std::cout << "  ModID: " << modFilePair.first
                  << ", FileID: " << modFilePair.second
                  << " => " << url << "\n";
    }

    // Save download links
    save_download_links(downloadLinks, gameDomain, base_dir);
    std::cout << "Download links saved for domain '" << gameDomain << "'.\n";

    // Download files
    download_files(gameDomain, base_dir);
    std::cout << "Files downloaded for domain '" << gameDomain << "'.\n";
}

//--------------------------------------------------
// Run the NexusMods steps for multiple domains
//--------------------------------------------------
void runNexusModsSequence(const AppConfig& config, const std::vector<std::string>& domains)
{
    // 1) Get tracked mods once
    API_KEY = config.nexus_api_key;
    std::vector<int> trackedMods = get_tracked_mods();
    std::cout << "Tracked Mods (IDs):\n";
    for (int modId : trackedMods) {
        std::cout << "  " << modId << "\n";
    }

    // 3) Run the pipeline for each domain
    for (const auto& domain : domains) {
        std::cout << "\n===== Processing Domain: " << domain << " =====\n";
        runNexusModsForOneDomain(trackedMods, domain, config.mods_directory);
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
            std::string jsonResponse = fetchModName(gameDomain, modID);
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
// Main
//--------------------------------------------------
int main(int argc, char* argv[])
{
    AppConfig config = initialize_app();
    API_KEY = config.nexus_api_key; // Set global API key for modules that use it

    bool running = true;
    while (running) {
        std::cout << "\n---------------------------------------\n";
        std::cout << "\n============== Main Menu ==============\n";
        std::cout << "1. Run GameBanana Sequence\n";
        std::cout << "2. Run NexusMods Sequence\n";
        std::cout << "3. Run Rename Sequence - Typically only required after running NexusMods Sequence\n";
        std::cout << "0. Exit\n";
        std::cout << "=======================================\n";
        std::cout << "Enter your choice (0/1/2/3): ";

        int choice;
        std::cin >> choice;

        // Handle invalid input
        if (!std::cin) {
            std::cin.clear();
            std::cin.ignore(10000, '\n');
            std::cout << "Invalid input, please try again.\n";
            continue;
        }

        switch (choice) {
        case 0: {
            running = false;
            break;
        }
        case 1: {
            // Run everything for GameBanana
            runGameBananaSequence(config);
            break;
        }
        case 2: {
            // Gather any additional domains from argv (if provided)
            // e.g., if user ran: ./MyProgram horizonzerodawn finalfantasyxx2hdremaster
            // then parse them now.
            std::vector<std::string> gameDomains;

            // Check if there are extra arguments after the "2" in argv
            // The first argument in argv is the executable name
            // The second argument might have been "2"
            // So we start scanning after that
            // e.g.  ./MyProgram 2 horizonzerodawn finalfantasyxx2hdremaster
            //       argv[0] = "./MyProgram"
            //       argv[1] = "2"
            //       argv[2] = "horizonzerodawn"
            //       argv[3] = "finalfantasyxx2hdremaster"
            // => we start from i=2
            for (int i = 2; i < argc; i++) {
                gameDomains.push_back(argv[i]);
            }

            // If none were provided in argv, prompt user for one or more domains
            if (gameDomains.empty()) {
                std::cout << "Enter one or more game domains (space-separated), then press ENTER:\n";
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::string domainsLine;
                std::getline(std::cin, domainsLine);

                std::istringstream iss(domainsLine);
                std::string domain;
                while (iss >> domain) {
                    gameDomains.push_back(domain);
                }
            }

            // If the user still provided nothing, we can bail out or ask again
            if (gameDomains.empty()) {
                std::cout << "No domains specified. Returning to main menu.\n";
                break;
            }

            // Now we have a list of domains. Pass them all to runNexusModsSequence
            runNexusModsSequence(config, gameDomains);

            // Optionally, stop the loop
            // running = false;
            break;
        }
        case 3: {
            runRenameSequence(config);
            break;
        }
        default: {
            std::cout << "Invalid choice. Please try again.\n";
            break;
        }
        }
    }

    return 0;
}
