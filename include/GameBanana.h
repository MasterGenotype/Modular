#ifndef GAMEBANANA_H
#define GAMEBANANA_H

#include <string>
#include <utility>
#include <vector>

// Initializes necessary resources (e.g., cURL).
void initialize();

// Cleans up resources (e.g., cURL).
void cleanup();

// Performs an HTTP GET request to the specified URL and returns the response as a string.
std::string httpGet(const std::string& url);

// Downloads a file from the specified URL and saves it to the given output path.
// Returns true if the download succeeds, false otherwise.
bool downloadFile(const std::string& url, const std::string& outputPath);

// Sanitizes a filename by replacing illegal characters with underscores.
std::string sanitizeFilename(const std::string& name);

// Extracts a mod ID from the provided profile URL.
std::string extractModId(const std::string& profileUrl);

// Extracts the file name from the given download URL.
std::string extractFileName(const std::string& downloadUrl);

// Fetches a list of subscribed mods for the given user ID.
// Each mod is represented as a pair where:
//   - first: the mod's profile URL,
//   - second: the mod's name.
std::vector<std::pair<std::string, std::string>> fetchSubscribedMods(const std::string& userId);

// Fetches a list of file download URLs for the specified mod ID.
std::vector<std::string> fetchModFileUrls(const std::string& modId);

// Downloads all mod files for the specified mod.
// Files will be stored in a subdirectory (based on a sanitized version of modName) under baseDir.
void downloadModFiles(const std::string& modId, const std::string& modName, const std::string& baseDir);

#endif // GAMEBANANA_H
