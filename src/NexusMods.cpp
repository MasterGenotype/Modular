#include "NexusMods.h"
#include "HTTPClient.h"
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <queue>
#include <atomic>
#include <thread>
#include <functional>

static std::mutex console_mutex;

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
std::vector<int> get_tracked_mods(const AppConfig& config)
{
    std::vector<int> mod_ids;

    std::string url = "https://api.nexusmods.com/v1/user/tracked_mods.json";
    std::vector<std::string> local_headers = {
        "accept: application/json",
        "apikey: " + config.nexus_api_key
    };

    HTTPClient::HttpResponse resp = HTTPClient::http_get(url, local_headers);
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
 * A generic thread-safe queue for tasks.
 */
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

/**
 * A generic worker for processing tasks from a queue.
 */
template <typename Task, typename Result>
void api_worker(ThreadSafeQueue<Task>& task_queue, ThreadSafeQueue<Result>& result_queue, std::function<Result(Task)> work_function) {
    Task task;
    while (task_queue.try_pop(task)) {
        result_queue.push(work_function(task));
        // Respect rate limit *after* performing the task
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

/**
 * Retrieve file_ids for each mod_id in parallel.
 */
std::map<int, std::vector<int>> get_file_ids(const AppConfig& config, const std::vector<int>& mod_ids, const std::string& game_domain)
{
    using Task = int; // mod_id
    using Result = std::pair<int, std::vector<int>>; // mod_id, file_ids

    ThreadSafeQueue<Task> task_queue;
    for (int mod_id : mod_ids) {
        task_queue.push(mod_id);
    }

    ThreadSafeQueue<Result> result_queue;
    auto work_function = [&](Task mod_id) -> Result {
        std::ostringstream oss;
        oss << "https://api.nexusmods.com/v1/games/" << game_domain << "/mods/" << mod_id << "/files.json?category=main";
        HTTPClient::HttpResponse resp = HTTPClient::http_get(oss.str(), {"accept: application/json", "apikey: " + config.nexus_api_key});

        if (resp.status_code == 200) {
            try {
                json data = json::parse(resp.body);
                if (data.contains("files")) {
                    std::vector<int> file_ids;
                    for (auto& file_json : data["files"]) {
                        if (file_json.contains("file_id")) file_ids.push_back(file_json["file_id"].get<int>());
                    }
                    return {mod_id, file_ids};
                }
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(console_mutex);
                std::cerr << "JSON parse error for mod " << mod_id << ": " << e.what() << std::endl;
            }
        }
        return {mod_id, {}}; // Return empty vector on error
    };

    const unsigned int num_threads = std::min((unsigned int)mod_ids.size(), std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; ++i) {
        threads.emplace_back(api_worker<Task, Result>, std::ref(task_queue), std::ref(result_queue), work_function);
    }

    for (auto& t : threads) t.join();

    std::map<int, std::vector<int>> mod_file_ids;
    Result result;
    while (result_queue.try_pop(result)) {
        mod_file_ids[result.first] = result.second;
    }
    return mod_file_ids;
}

/**
 * Generate download links for each (mod_id, file_id) pair in parallel.
 */
std::map<std::pair<int, int>, std::string> generate_download_links(const AppConfig& config,
    const std::map<int, std::vector<int>>& mod_file_ids,
    const std::string& game_domain)
{
    using Task = std::pair<int, int>; // mod_id, file_id
    using Result = std::pair<Task, std::string>; // {mod_id, file_id}, url

    ThreadSafeQueue<Task> task_queue;
    size_t total_tasks = 0;
    for (const auto& [mod_id, file_ids] : mod_file_ids) {
        for (auto file_id : file_ids) {
            task_queue.push({mod_id, file_id});
            total_tasks++;
        }
    }

    ThreadSafeQueue<Result> result_queue;
    auto work_function = [&](Task task) -> Result {
        auto [mod_id, file_id] = task;
        std::ostringstream oss;
        oss << "https://api.nexusmods.com/v1/games/" << game_domain << "/mods/" << mod_id << "/files/" << file_id << "/download_link.json?expires=999999";
        HTTPClient::HttpResponse resp = HTTPClient::http_get(oss.str(), {"accept: application/json", "apikey: " + config.nexus_api_key});

        if (resp.status_code == 200) {
            try {
                json data = json::parse(resp.body);
                if (data.is_array() && !data.empty() && data[0].contains("URI")) {
                    return {task, data[0]["URI"].get<std::string>()};
                }
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(console_mutex);
                std::cerr << "JSON parse error for link " << mod_id << "/" << file_id << ": " << e.what() << std::endl;
            }
        }
        return {task, ""}; // Return empty string on error
    };

    const unsigned int num_threads = std::min((unsigned int)total_tasks, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; ++i) {
        threads.emplace_back(api_worker<Task, Result>, std::ref(task_queue), std::ref(result_queue), work_function);
    }

    for (auto& t : threads) t.join();

    std::map<std::pair<int, int>, std::string> download_links;
    Result result;
    while (result_queue.try_pop(result)) {
        if (!result.second.empty()) {
            download_links[result.first] = result.second;
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

        // Use the consolidated HTTPClient download function
        if (HTTPClient::download_file(safe_url, file_path)) {
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cout << "Downloaded " << file_path.filename().string()
                      << " to " << file_path.parent_path().string() << std::endl;
            return; // success
        } else {
            {
                std::lock_guard<std::mutex> lock(console_mutex);
                std::cerr << "Error downloading Mod ID " << mod_id << ", File ID " << file_id << std::endl;
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

    // Use a dynamic number of threads based on hardware
    const unsigned int num_threads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
    std::atomic<int> progress_counter(0);
    int total_files = lines.size();

    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "Starting download of " << total_files << " files using " << num_threads << " concurrent workers..." << std::endl;
    }

    for (unsigned int i = 0; i < num_threads; ++i) {
        threads.emplace_back(download_worker, std::ref(download_queue), std::ref(progress_counter), total_files);
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "All downloads have been processed." << std::endl;
}

//----------------------------------------------------------------------------------
// Backup Scraper Functions
//----------------------------------------------------------------------------------

/**
 * Executes the Python scraper script to build a local database of downloaded mods.
 */
static void runPythonScraper(const AppConfig& config, const std::string& cookiePath, const std::string& outputJsonPath) {
    if (config.executable_path.empty()) {
        std::cerr << "Error: Application path not initialized." << std::endl;
        return;
    }

    // Find the script relative to the executable's location
    fs::path executable_dir = config.executable_path.parent_path();
    fs::path script_path = executable_dir / "scripts" / "nexus_scraper.py";

    // As a fallback for development, check the source tree structure
    if (!fs::exists(script_path)) {
        script_path = executable_dir / ".." / ".." / "scripts" / "nexus_scraper.py";
        if (!fs::exists(script_path)) {
             std::cerr << "Error: Scraper script 'nexus_scraper.py' not found in install or development locations." << std::endl;
             return;
        }
    }

    // Use the canonical path to resolve any ".." components
    script_path = fs::canonical(script_path);

    std::string command = "python3 \"" + script_path.string() + "\" \"" + cookiePath + "\" \"" + outputJsonPath + "\"";

    std::cout << "Running Python scraper. This may take several minutes depending on your download history...\n";
    std::cout << "Executing: " << command << std::endl;

    int result = std::system(command.c_str());

    if (result == 0) {
        std::cout << "\nScraper finished successfully." << std::endl;
        std::cout << "Downloaded mods database has been saved to: " << outputJsonPath << std::endl;
    } else {
        std::cerr << "\nError: Python scraper script failed with exit code " << result << std::endl;
        std::cerr << "Please ensure Python 3, Selenium, and a compatible web driver (like geckodriver for Firefox) are installed and in your system's PATH." << std::endl;
    }
}

void runNexusBackupScraper(const AppConfig& config) {
    std::cout << "\n===== Running NexusMods Backup Scraper =====\n";

    if (config.nexus_cookie_path.empty() || !fs::exists(config.nexus_cookie_path)) {
        std::cerr << "Error: Path to NexusMods cookies file is not set or the file does not exist.\n";
        std::cerr << "Please check the 'nexus_cookie_path' in your config file: " << config.nexus_cookie_path << std::endl;
        return;
    }

    // Define an output path for the scraped data in the same directory as the cookies file.
    fs::path output_path = fs::path(config.nexus_cookie_path).parent_path() / "nexusmods_downloaded.json";

    runPythonScraper(config, config.nexus_cookie_path, output_path.string());
}
