#include "NexusMods.h"
#include <chrono>
#include <cstdlib>
#include <curl/curl.h>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>

static std::mutex console_mutex;

//----------------------------------------------------------------------------------
// Curl utility functions
//----------------------------------------------------------------------------------

/**
 * Write callback for libcurl to accumulate the response body in a std::string.
 */
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t totalSize = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

/**
 * Perform a GET request to the specified URL with the specified headers.
 */
HttpResponse http_get(const std::string& url, const std::vector<std::string>& headers)
{
    HttpResponse response { 0, "" };
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL." << std::endl;
        return response;
    }

    // Build the custom headers
    struct curl_slist* curl_headers = nullptr;
    for (const auto& header : headers) {
        curl_headers = curl_slist_append(curl_headers, header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    // Perform the request
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "CURL GET failed: " << curl_easy_strerror(res) << std::endl;
    } else {
        // Get HTTP status code
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
    }

    // Cleanup
    curl_slist_free_all(curl_headers);
    curl_easy_cleanup(curl);

    return response;
}

//----------------------------------------------------------------------------------
// Utility to escape only raw spaces (replace ' ' with "%20") in a URL
//----------------------------------------------------------------------------------
std::string escape_spaces(const std::string& url)
{
    std::string result;
    result.reserve(url.size());
    for (char c : url) {
        if (c == ' ') {
            result += "%20";
        } else {
            result += c;
        }
    }
    return result;
}

//----------------------------------------------------------------------------------
// Functions that mirror the Python script logic
//----------------------------------------------------------------------------------

/**
 * Retrieve the list of tracked mods and extract mod_ids.
 */
std::vector<int> get_tracked_mods()
{
    std::vector<int> mod_ids;

    std::string url = "https://api.nexusmods.com/v1/user/tracked_mods.json";
    std::vector<std::string> local_headers = {
        "accept: application/json",
        "apikey: " + API_KEY
    };

    HttpResponse resp = http_get(url, local_headers);
    if (resp.status_code == 200) {
        try {
            json data = json::parse(resp.body);
            // Check if data is a list or an object with "mods"
            if (data.is_array()) {
                for (auto& mod : data) {
                    if (mod.contains("mod_id")) {
                        mod_ids.push_back(mod["mod_id"].get<int>());
                    }
                }
            } else if (data.contains("mods")) {
                auto mods_list = data["mods"];
                for (auto& mod : mods_list) {
                    if (mod.contains("mod_id")) {
                        mod_ids.push_back(mod["mod_id"].get<int>());
                    }
                }
            } else {
                std::cout << "No mods found in the tracked mods response." << std::endl;
                return {};
            }
            std::cout << "Retrieved " << mod_ids.size() << " mod IDs." << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "JSON parse error in get_tracked_mods: " << e.what() << std::endl;
        }
    } else {
        std::cerr << "Error fetching tracked mods: " << resp.status_code << std::endl;
    }

    return mod_ids;
}

/**
 * Retrieve file_ids for each mod_id.
 */
std::map<int, std::vector<int>> get_file_ids(const std::vector<int>& mod_ids, const std::string& game_domain)
{
    std::map<int, std::vector<int>> mod_file_ids;

    for (auto mod_id : mod_ids) {
        std::ostringstream oss;
        oss << "https://api.nexusmods.com/v1/games/"
            << game_domain << "/mods/" << mod_id << "/files.json?category=main";
        std::string url = oss.str();

        std::vector<std::string> local_headers = {
            "accept: application/json",
            "apikey: " + API_KEY
        };

        HttpResponse resp = http_get(url, local_headers);

        if (resp.status_code == 200) {
            try {
                json data = json::parse(resp.body);
                if (data.contains("files")) {
                    auto file_list = data["files"];
                    std::vector<int> file_ids;
                    for (auto& file_json : file_list) {
                        if (file_json.contains("file_id")) {
                            file_ids.push_back(file_json["file_id"].get<int>());
                        }
                    }
                    mod_file_ids[mod_id] = file_ids;
                    std::cout << "Mod ID " << mod_id << " has " << file_ids.size() << " files." << std::endl;
                } else {
                    std::cout << "No files found for mod " << mod_id << "." << std::endl;
                    mod_file_ids[mod_id] = {};
                }
            } catch (const std::exception& e) {
                std::cerr << "JSON parse error in get_file_ids: " << e.what() << std::endl;
                mod_file_ids[mod_id] = {};
            }
        } else {
            std::cout << "Error fetching files for mod " << mod_id << ": " << resp.status_code << std::endl;
            mod_file_ids[mod_id] = {};
        }

        // Respect rate limit
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return mod_file_ids;
}

/**
 * Generate download links for each (mod_id, file_id) pair.
 */
std::map<std::pair<int, int>, std::string> generate_download_links(
    const std::map<int, std::vector<int>>& mod_file_ids,
    const std::string& game_domain)
{
    std::map<std::pair<int, int>, std::string> download_links;

    for (auto& [mod_id, file_ids] : mod_file_ids) {
        for (auto file_id : file_ids) {
            std::ostringstream oss;
            oss << "https://api.nexusmods.com/v1/games/"
                << game_domain << "/mods/" << mod_id
                << "/files/" << file_id << "/download_link.json?expires=999999";

            std::string url = oss.str();
            std::vector<std::string> local_headers = {
                "accept: application/json",
                "apikey: " + API_KEY
            };

            HttpResponse resp = http_get(url, local_headers);

            if (resp.status_code == 200) {
                try {
                    json data = json::parse(resp.body);
                    // Expecting a list of links
                    if (data.is_array() && !data.empty()) {
                        if (data[0].contains("URI")) {
                            std::string download_url = data[0]["URI"].get<std::string>();
                            download_links[{ mod_id, file_id }] = download_url;
                            std::cout << "Generated download link for Mod ID "
                                      << mod_id << ", File ID " << file_id << "." << std::endl;
                        } else {
                            std::cout << "No 'URI' field found for Mod ID "
                                      << mod_id << ", File ID " << file_id << "." << std::endl;
                        }
                    } else {
                        std::cout << "No download links found for Mod ID "
                                  << mod_id << ", File ID " << file_id << "." << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "JSON parse error in generate_download_links: " << e.what() << std::endl;
                }
            } else {
                std::cout << "Error generating download link for Mod ID "
                          << mod_id << ", File ID " << file_id << ": "
                          << resp.status_code << std::endl;
            }

            // Respect rate limit
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    return download_links;
}

/**
 * Save the download links to a text file in the base directory.
 */
void save_download_links(const std::map<std::pair<int, int>, std::string>& download_links,
    const std::string& game_domain, const fs::path& base_dir)
{
    fs::path base_directory = base_dir / game_domain;
    // Make sure directory exists
    fs::create_directories(base_directory);

    fs::path download_links_path = base_directory / "download_links.txt";

    std::ofstream ofs(download_links_path.string());
    if (!ofs.is_open()) {
        std::cerr << "Failed to open file for writing: "
                  << download_links_path.string() << std::endl;
        return;
    }

    for (auto& [mod_file_pair, url] : download_links) {
        int mod_id = mod_file_pair.first;
        int file_id = mod_file_pair.second;
        ofs << mod_id << "," << file_id << "," << url << "\n";
    }

    ofs.close();
    std::cout << "Download links saved to " << download_links_path.string() << "." << std::endl;
}

/**
 * The actual download logic for a single file, with retries.
 * This function is thread-safe.
 */
static void download_file_with_retries(const std::string& url_in, const fs::path& file_path,
                                     int mod_id, int file_id) {
    const int retries = 5;
    // Escape only spaces in the URL
    std::string safe_url = escape_spaces(url_in);

    for (int attempt = 0; attempt < retries; attempt++) {
        {
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cout << "Downloading Mod ID " << mod_id << ", File ID "
                      << file_id << " (Attempt " << (attempt + 1) << "/" << retries << ")..." << std::endl;
        }

        CURL* curl = curl_easy_init();
        if (!curl) {
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cerr << "Failed to initialize CURL for download." << std::endl;
            return;
        }

        FILE* fp = std::fopen(file_path.string().c_str(), "wb");
        if (!fp) {
            {
                std::lock_guard<std::mutex> lock(console_mutex);
                std::cerr << "Failed to open file for writing: "
                          << file_path.string() << std::endl;
            }
            curl_easy_cleanup(curl);
            return;
        }

        // Set the (space-escaped) URL
        curl_easy_setopt(curl, CURLOPT_URL, safe_url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        // Perform the request
        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        std::fclose(fp);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && http_code == 200) {
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cout << "Downloaded " << file_path.filename().string()
                      << " to " << file_path.parent_path().string() << std::endl;
            return; // success
        } else {
            {
                std::lock_guard<std::mutex> lock(console_mutex);
                std::cerr << "Error downloading Mod ID " << mod_id << ", File ID "
                          << file_id << ": CURL code " << res
                          << ", HTTP code " << http_code << std::endl;
            }
            if (attempt < retries - 1) {
                {
                    std::lock_guard<std::mutex> lock(console_mutex);
                    std::cout << "Retrying in 5 seconds..." << std::endl;
                }
                std::this_thread::sleep_for(std::chrono::seconds(5));
            } else {
                std::lock_guard<std::mutex> lock(console_mutex);
                std::cerr << "Failed to download Mod ID " << mod_id
                          << ", File ID " << file_id << " after " << retries
                          << " attempts." << std::endl;
            }
        }
    }
}

namespace {

struct DownloadTask {
    std::string url;
    fs::path file_path;
    int mod_id;
    int file_id;
};

template <typename T>
class ThreadSafeQueue {
public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(std::move(value));
    }

    bool try_pop(T& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return false;
        }
        value = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }

private:
    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
};

void download_worker(ThreadSafeQueue<DownloadTask>& queue, std::atomic<int>& progress_counter, int total_files) {
    DownloadTask task;
    while (queue.try_pop(task)) {
        download_file_with_retries(task.url, task.file_path, task.mod_id, task.file_id);
        int completed = ++progress_counter;
        {
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cout << "Progress: [" << completed << "/" << total_files << "] files downloaded." << std::endl;
        }
    }
}

} // anonymous namespace

/**
 * Download files from the list of URLs in download_links.txt with retry logic.
 */
void download_files(const std::string& game_domain, const fs::path& base_dir)
{
    fs::path base_directory = base_dir / game_domain;
    fs::path download_links_path = base_directory / "download_links.txt";

    if (!fs::exists(download_links_path)) {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "download_links.txt file not found in " << download_links_path.parent_path() << std::endl;
        return;
    }

    // Read lines
    std::ifstream ifs(download_links_path.string());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }

    if (lines.empty()) {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "No download links found in " << download_links_path << std::endl;
        return;
    }

    ThreadSafeQueue<DownloadTask> download_queue;

    // Process each line and populate the download queue
    for (auto& line : lines) {
        std::stringstream ss(line);
        std::string mod_id_str, file_id_str, url;
        if (std::getline(ss, mod_id_str, ',') && std::getline(ss, file_id_str, ',') && std::getline(ss, url)) {
            int mod_id = std::stoi(mod_id_str);
            int file_id = std::stoi(file_id_str);

            std::string filename;
            {
                // Extract substring after last '/'
                auto pos = url.rfind('/');
                if (pos != std::string::npos && pos < url.size() - 1) {
                    filename = url.substr(pos + 1);
                }
                // Remove query string if present
                pos = filename.find('?');
                if (pos != std::string::npos) {
                    filename = filename.substr(0, pos);
                }
                // Fallback if empty
                if (filename.empty()) {
                    std::ostringstream fallback;
                    fallback << "mod_" << mod_id << "_file_" << file_id << ".zip";
                    filename = fallback.str();
                }
            }

            // Create a directory for the mod_id
            fs::path mod_directory = base_directory / std::to_string(mod_id);
            fs::create_directories(mod_directory);

            // Define the full path
            fs::path file_path = mod_directory / filename;

            download_queue.push({url, file_path, mod_id, file_id});
        }
    }

    const int num_threads = 4;
    std::vector<std::thread> threads;
    std::atomic<int> progress_counter(0);
    int total_files = lines.size();

    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "Starting download of " << total_files << " files using " << num_threads << " concurrent workers..." << std::endl;
    }

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(download_worker, std::ref(download_queue), std::ref(progress_counter), total_files);
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "All downloads have been processed." << std::endl;
}
