#include "HTTPClient.h"
#include <curl/curl.h>
#include <fstream>
#include <iostream>

namespace HTTPClient {

// --- Private Callbacks ---

static size_t WriteStringCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    auto* str = static_cast<std::string*>(userp);
    try {
        str->append(static_cast<char*>(contents), totalSize);
    } catch (const std::bad_alloc& e) {
        std::cerr << "bad_alloc caught: " << e.what() << std::endl;
        return 0;
    }
    return totalSize;
}

static size_t WriteStreamCallback(void* ptr, size_t size, size_t nmemb, void* stream) {
    auto* out = static_cast<std::ostream*>(stream);
    size_t totalSize = size * nmemb;
    out->write(static_cast<char*>(ptr), totalSize);
    return totalSize;
}

// --- Public API ---

void initialize() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void cleanup() {
    curl_global_cleanup();
}

HttpResponse http_get(const std::string& url, const std::vector<std::string>& headers) {
    HttpResponse response{0, ""};
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL for GET request." << std::endl;
        return response;
    }

    struct curl_slist* curl_headers = nullptr;
    for (const auto& header : headers) {
        curl_headers = curl_slist_append(curl_headers, header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "CURL GET failed for URL " << url << ": " << curl_easy_strerror(res) << std::endl;
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
    }

    curl_slist_free_all(curl_headers);
    curl_easy_cleanup(curl);

    return response;
}

bool download_file(const std::string& url, const fs::path& output_path) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL for download." << std::endl;
        return false;
    }

    std::ofstream outFile(output_path, std::ios::binary);
    if (!outFile) {
        std::cerr << "Error: Could not open file " << output_path << " for writing." << std::endl;
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStreamCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outFile);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "CURL download failed for URL " << url << ": " << curl_easy_strerror(res) << std::endl;
    }

    curl_easy_cleanup(curl);
    return res == CURLE_OK;
}

} // namespace HTTPClient