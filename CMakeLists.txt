cmake_minimum_required(VERSION 3.10)
project(ModularProject)

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

# Include directories (assuming Rename.h is in the 'include/' directory)
include_directories(${CMAKE_SOURCE_DIR}/include)

# Find cURL
find_package(CURL REQUIRED)

# Find nlohmann/json (header-only library)
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)
if(NLOHMANN_JSON_INCLUDE_DIR)
    include_directories(${NLOHMANN_JSON_INCLUDE_DIR})
else()
    message(FATAL_ERROR "nlohmann/json.hpp not found. Install nlohmann-json or provide the correct include path.")
endif()

# Add source files (adjust paths if needed)
set(SOURCES
    src/NexusMods.cpp
    src/GameBanana.cpp
    src/Rename.cpp
    src/main.cpp
)

# Create a library for all the sources
add_library(ModularLib ${SOURCES})
target_link_libraries(ModularLib CURL::libcurl)

# Linux executable
add_executable(Modular_Linux src/main.cpp)
target_link_libraries(Modular_Linux ModularLib)

# Enable additional compiler warnings with GCC/Clang
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Modular_Linux PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Cross-compile for Windows (optional)
if(CMAKE_CROSSCOMPILING OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
    set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
    add_executable(Modular_Windows src/main.cpp)
    target_link_libraries(Modular_Windows ModularLib)
endif()
