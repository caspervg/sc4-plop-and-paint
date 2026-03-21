if(NOT WIN32)
    message(STATUS "SC4 VERSION proxy is only built on Win32. Skipping the version_proxy target.")
    return()
endif()

set(SC4_VERSION_PROXY_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/VersionProxy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SC4UserDirFix.cpp
)

set(SC4_VERSION_PROXY_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/SC4UserDirFix.h
)

set(SC4_VERSION_PROXY_DEF "${CMAKE_CURRENT_SOURCE_DIR}/VersionProxy.def")

add_library(SC4VersionProxy SHARED
    ${SC4_VERSION_PROXY_SOURCES}
    ${SC4_VERSION_PROXY_HEADERS}
    ${SC4_VERSION_PROXY_DEF}
)

target_include_directories(SC4VersionProxy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(SC4VersionProxy PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

target_link_libraries(SC4VersionProxy PRIVATE
    kernel32
)

set_target_properties(SC4VersionProxy PROPERTIES
    OUTPUT_NAME "VERSION"
    SUFFIX ".dll"
    PREFIX ""
)

if(MSVC)
    set_target_properties(SC4VersionProxy PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()

install(TARGETS SC4VersionProxy
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
