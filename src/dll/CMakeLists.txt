# SimCity 4 only loads 32-bit DLLs, so x64 builds (the cache builder CLI) skip the plugin and its
# SC4RenderServices dependency.
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(STATUS "SC4PlopAndPaint DLL is only built for 32-bit Windows. Skipping the dll target.")
    return()
endif()

set(SC4RS_ROOT "${CMAKE_SOURCE_DIR}/vendor/sc4-render-services" CACHE PATH "Path to sc4-render-services")

set(GZCOM_DIR ${CMAKE_SOURCE_DIR}/vendor/gzcom-dll/gzcom-dll)

file(GLOB_RECURSE GZCOM_HEADERS
    ${GZCOM_DIR}/include/*.h
    ${GZCOM_DIR}/include/*.hpp
)
file(GLOB_RECURSE GZCOM_SOURCES
    ${GZCOM_DIR}/src/*.cpp
)
list(REMOVE_ITEM GZCOM_SOURCES
    ${GZCOM_DIR}/src/main.cpp
    ${GZCOM_DIR}/src/cGZExampleDllDirector.cpp
    ${GZCOM_DIR}/src/cGZExtraCheatsDllDirector.cpp
    ${GZCOM_DIR}/src/cGZExtraExtraCheatsDllDirector.cpp
    ${GZCOM_DIR}/src/cGZCityHallUpgradeDllDirector.cpp
)

add_library(gzcom2 STATIC ${GZCOM_SOURCES} ${GZCOM_HEADERS})
target_include_directories(gzcom2 PUBLIC ${GZCOM_DIR}/include)

# Builds SC4RenderServices from the submodule, reusing gzcom2 and spdlog::spdlog from this project.
# EXCLUDE_FROM_ALL keeps its other targets out of the build.
add_subdirectory(${SC4RS_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/sc4-render-services EXCLUDE_FROM_ALL)

file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
set(SC4_DLL_DEF "${CMAKE_CURRENT_SOURCE_DIR}/SC4PlopAndPaint.def")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/version.rc @ONLY)

add_library(${PROJECT_NAME} SHARED ${PROJECT_SOURCES} ${SC4_DLL_DEF} ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX SC4_PLOP_AND_PAINT_VERSION="${PROJECT_VERSION}")

# Include paths required by the plugin
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/vendor/mINI/src
    ${SC4RS_ROOT}/src
    ${CMAKE_SOURCE_DIR}/vendor/DBPFKit/src
)

# Link against gzcom2 and spdlog
set(SC4_DLL_LINK_LIBS
    spdlog::spdlog
    reflectcpp::reflectcpp
    gzcom2
)
list(APPEND SC4_DLL_LINK_LIBS SC4PlopAndPaintCore)
list(APPEND SC4_DLL_LINK_LIBS DBPFKitLib)

# Core dependencies
if(WIN32)
    list(APPEND SC4_DLL_LINK_LIBS kernel32 user32 Version wininet)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ${SC4_DLL_LINK_LIBS})

# ImGui is compiled into SC4RenderServices.dll and exported from it, so link that, NOT the vcpkg
# imgui. The import is delay-loaded: the game may load this plugin before SC4RenderServices.dll.
sc4rs_use_imgui(${PROJECT_NAME})

# Build settings
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "SC4PlopAndPaint"
    SUFFIX ".dll"
    PREFIX ""
)

# Use dynamic runtime library to match other SC4 plugins and the game's runtime.
# This is critical for plugin compatibility.
if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()

# Deploy to the official Plugins folder if the installation can be located
if(WIN32)
    set(SC4_INSTALL_PATHS
        "C:/Program Files (x86)/SimCity 4 Deluxe Edition"
        "C:/Program Files (x86)/Maxis/SimCity 4 Deluxe"
        "C:/Program Files/Maxis/SimCity 4 Deluxe"
        "C:/Program Files (x86)/Steam/steamapps/common/SimCity 4 Deluxe"
        "C:/Program Files/Steam/steamapps/common/SimCity 4 Deluxe"
        "C:/Program Files (x86)/GOG Galaxy/Games/SimCity 4 Deluxe Edition"
        "C:/GOG Games/SimCity 4 Deluxe Edition"
    )

    foreach(SC4_PATH ${SC4_INSTALL_PATHS})
        if(EXISTS "${SC4_PATH}/Apps/SimCity 4.exe")
            set(SC4_INSTALL_DIR "${SC4_PATH}")
            break()
        endif()
    endforeach()

    set(USERPROFILE_DIR "$ENV{USERPROFILE}")
    set(SC4_PLUGINS_DIR "${USERPROFILE_DIR}/Documents/SimCity 4/Plugins")

    if(SC4_INSTALL_DIR)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SC4_PLUGINS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${PROJECT_NAME}>
                "${SC4_PLUGINS_DIR}/"
            COMMAND ${CMAKE_COMMAND}
                -DSRC=${CMAKE_SOURCE_DIR}/dist/SC4PlopAndPaint.ini
                -DDST=${SC4_PLUGINS_DIR}/SC4PlopAndPaint.ini
                -P ${SC4RS_ROOT}/cmake/CopyIfNotExists.cmake
            COMMENT "Deploying SC4PlopAndPaint.dll to Plugins folder (INI only if missing)"
        )

        message(STATUS "Deployment targets configured:")
        message(STATUS "  - SC4PlopAndPaint.dll -> ${SC4_PLUGINS_DIR}/")
        message(STATUS "  - SC4PlopAndPaint.ini -> ${SC4_PLUGINS_DIR}/ (if missing)")
    else()
        message(WARNING "SC4 installation not found. Manual deployment required.")
    endif()
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
