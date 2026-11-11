set(APP_NAME SC4PlopAndPaintCli)

# Collect source files from app directory and services subdirectory
file(GLOB APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(${APP_NAME} ${APP_SOURCES})

add_executable(PieceLabelsTests
    ${CMAKE_SOURCE_DIR}/tests/PieceLabelsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PieceLabels.cpp
)
target_include_directories(PieceLabelsTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME PieceLabelsTests COMMAND PieceLabelsTests)

# Fetching and reading the SC4 Prop Texture Catalog (PackCatalogBuilder).
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(CURL REQUIRED)

target_compile_definitions(${APP_NAME} PRIVATE NOMINMAX)
target_compile_definitions(${APP_NAME}
    PRIVATE
        SC4_PLOP_AND_PAINT_VERSION="${SC4_PLOP_AND_PAINT_APP_VERSION}"
)

target_include_directories(${APP_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include
)

target_link_libraries(${APP_NAME} PRIVATE
    spdlog::spdlog
    SC4PlopAndPaintCore
    taywee::args
    DBPFKitLib
    utf8cpp::utf8cpp
    pugixml::pugixml
    jsoncons::jsoncons
    raylib
    glfw
    unofficial::sqlite3::sqlite3
    CURL::libcurl
    WebP::webp
)

if (WIN32)
    target_compile_definitions(${APP_NAME} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        NOGDI
        NOUSER
        MMNOSOUND
    )

    target_link_options(${APP_NAME} PRIVATE
        /FORCE:MULTIPLE
        /IGNORE:4006,4221
        /MANIFEST:EMBED
        /MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/SC4PlopAndPaintCli.exe.manifest
    )

    target_link_libraries(${APP_NAME} PRIVATE winmm opengl32 gdi32 user32)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Bundle the GCC runtime so the binary runs on distributions with an older libstdc++ than the build machine.
    target_link_options(${APP_NAME} PRIVATE -static-libstdc++ -static-libgcc)
endif()

# Copy DLLs to output directory for execution (only for dynamic triplets)
if(MSVC)
    # vcpkg stores debug DLLs in debug/bin and release DLLs in bin
    # Static triplets (like x86-windows-static-md) don't have a bin directory
    set(VCPKG_RELEASE_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")

    if(EXISTS "${VCPKG_RELEASE_BIN_DIR}")
        set(VCPKG_DEBUG_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin")

        # Copy DLLs to executable directory based on build configuration
        add_custom_command(TARGET ${APP_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<IF:$<CONFIG:Debug>,${VCPKG_DEBUG_BIN_DIR},${VCPKG_RELEASE_BIN_DIR}>"
                "$<TARGET_FILE_DIR:${APP_NAME}>"
            COMMENT "Copying $<IF:$<CONFIG:Debug>,debug,release> DLLs to executable directory"
            VERBATIM
        )
    endif()
endif()

install(TARGETS ${APP_NAME}
    RUNTIME DESTINATION bin
)
