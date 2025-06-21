#ifndef RENAME_H
#define RENAME_H

#include <filesystem>
#include <string>
#include <vector>

// Given a base folder (e.g. "~/Games/Mods-Lists"), returns a list of game domains.
std::vector<std::string> getGameDomainNames(const std::filesystem::path& modsListsDir);

// Given a game domain folder, returns a list of mod IDs (subdirectory names).
std::vector<std::string> getModIDs(const std::filesystem::path& gameDomainPath);

// Using the game domain and mod ID, performs a GET request to the Nexus Mods API.
// (For example: https://api.nexusmods.com/v1/games/<game_domain>/mods/<mod_id>)
// Returns the JSON response as a string.
std::string fetchModName(const std::string& gameDomain, const std::string& modID);

// Given the JSON response from the API, extracts the mod name.
// (This function assumes that the JSON object has a "name" field.)
std::string extractModName(const std::string& jsonResponse);

// Given a target directory and a source directory, recursively copies (merges) the files.
void combineDirectories(const std::filesystem::path& target, const std::filesystem::path& source);

#endif // RENAME_H
