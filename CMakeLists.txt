cmake_minimum_required(VERSION 3.20)

# Force release runtime BEFORE project() call - this is critical for MSVC builds
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    set(CMAKE_CXX_FLAGS_DEBUG "/MD /Zi /Ob0 /Od")
    set(CMAKE_C_FLAGS_DEBUG "/MD /Zi /Ob0 /Od")
endif()

project(SC4AdvancedLotPlop VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_definitions(-DWIN32 -D_WINDOWS -DUNICODE -D_UNICODE)
    set(CMAKE_SIZEOF_VOID_P 4)
    set(CMAKE_C_SIZEOF_DATA_PTR 4)
    set(CMAKE_CXX_SIZEOF_DATA_PTR 4)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    
    # Force release runtime for all configurations
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    
    # Override debug flags to use release runtime
    set(CMAKE_CXX_FLAGS_DEBUG "/MD /Zi /Ob0 /Od /RTC1")
    set(CMAKE_C_FLAGS_DEBUG "/MD /Zi /Ob0 /Od /RTC1")
    
    # Enable debugging in release builds
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()

set(REFLECTCPP_TOML ON)
set(REFLECTCPP_PARQUET ON)
set(REFLECTCPP_JSON ON)

set(SPDLOG_DIRS
    "${CMAKE_SOURCE_DIR}/vendor/spdlog"
    "${CMAKE_SOURCE_DIR}/vendor/sc4-imgui-service/vendor/spdlog"
)

set(SPDLOG_ADDED OFF)
set(SPDLOG_SOURCE_DIR "")
foreach(SPDLOG_CANDIDATE IN LISTS SPDLOG_DIRS)
    if(EXISTS "${SPDLOG_CANDIDATE}/CMakeLists.txt")
        add_subdirectory("${SPDLOG_CANDIDATE}")
        set(SPDLOG_ADDED ON)
        set(SPDLOG_SOURCE_DIR "${SPDLOG_CANDIDATE}")
        break()
    endif()
endforeach()

if(SPDLOG_ADDED)
    message(STATUS "Configuring bundled spdlog from ${SPDLOG_SOURCE_DIR}")
else()
    find_package(spdlog CONFIG REQUIRED)
endif()

set(SPDLOG_TARGET spdlog::spdlog)

add_subdirectory(vendor/args)
add_subdirectory(vendor/DBPFKit)
add_subdirectory(vendor/reflect-cpp)
add_subdirectory(src/shared)

add_subdirectory(src/dll)
add_subdirectory(src/app)

if(EXISTS "${CMAKE_SOURCE_DIR}/docs")
    install(DIRECTORY docs/
        DESTINATION docs
        FILES_MATCHING PATTERN "*"
    )
endif()
