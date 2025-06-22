#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace HTTPClient {

struct HttpResponse {
    long status_code;
    std::string body;
};

// Initializes and cleans up global resources (e.g., cURL).
void initialize();
void cleanup();

HttpResponse http_get(const std::string& url, const std::vector<std::string>& headers = {});
bool download_file(const std::string& url, const fs::path& output_path);

} // namespace HTTPClient