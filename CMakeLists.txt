cmake_minimum_required(VERSION 3.20)

# Enable vcpkg manifest mode BEFORE project()
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
set(VCPKG_MANIFEST_MODE ON CACHE BOOL "Enable vcpkg manifest mode")
set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE STRING "vcpkg manifest directory")
set(VCPKG_INSTALLED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed" CACHE STRING "vcpkg installed directory")

# Set MSVC runtime BEFORE project() to ensure it applies to all targets
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# Allow proper runtime library selection per configuration
# Debug builds use /MDd (with debug DLLs from vcpkg debug/bin)
# Release builds use /MD (with release DLLs from vcpkg bin)
# This is handled automatically by CMAKE_MSVC_RUNTIME_LIBRARY per config

project(SC4AdvancedLotPlop VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable testing
enable_testing()

if(WIN32)
    add_definitions(-DWIN32 -D_WINDOWS -DUNICODE -D_UNICODE)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

    # Enable debugging in release builds
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()

find_package(spdlog CONFIG REQUIRED)
find_package(args CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(jsoncons CONFIG REQUIRED)
find_package(raylib CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
add_subdirectory(vendor/DBPFKit)
add_subdirectory(src/shared)
add_subdirectory(src/dll)
add_subdirectory(src/app)
add_subdirectory(tools)

if(EXISTS "${CMAKE_SOURCE_DIR}/docs")
    install(DIRECTORY docs/
        DESTINATION docs
        FILES_MATCHING PATTERN "*"
    )
endif()
