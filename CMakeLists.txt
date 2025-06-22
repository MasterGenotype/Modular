# Set the minimum required version of CMake.
cmake_minimum_required(VERSION 3.16)

# Set the project name, version, and language.
project(Modular VERSION 1.0 LANGUAGES CXX)

# Set the C++ standard to C++17, which is required for std::filesystem.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#=============================================================================
# 1. Find and Configure Dependencies
#=============================================================================

# --- Fetch nlohmann/json using FetchContent ---
# This automates downloading the library, removing the need for manual installation.
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2 # Using a specific, stable version is best practice
)
FetchContent_MakeAvailable(nlohmann_json)

# --- Find CURL library ---
find_package(CURL REQUIRED)

# --- Find Python Interpreter for the backup scraper ---
find_package(Python3 COMPONENTS Interpreter REQUIRED)

#=============================================================================
# 2. Define Executable and Link Libraries
#=============================================================================

# Define the executable target from your source files.
add_executable(Modular
    src/main.cpp
    src/NexusMods.cpp
    src/GameBanana.cpp
    src/Rename.cpp
)

# Add the 'include' directory to the target's include paths.
target_include_directories(Modular PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link the required libraries to our executable.
target_link_libraries(Modular PRIVATE
    nlohmann_json::nlohmann_json
    CURL::libcurl
)

#=============================================================================
# 3. Install Rules for Deployment
#=============================================================================
# This ensures the executable and its script are placed together correctly.

include(GNUInstallDirs)

# Install the executable to the standard binary directory (e.g., /usr/local/bin)
install(TARGETS Modular
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install the Python script into a 'scripts' subdirectory relative to the executable.
install(FILES scripts/nexus_scraper.py
    DESTINATION ${CMAKE_INSTALL_BINDIR}/scripts
)