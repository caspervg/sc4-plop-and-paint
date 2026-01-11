if(NOT WIN32)
    message(STATUS "SC4AdvancedLotPlop DLL is only built on Win32. Skipping the dll target.")
    return()
endif()

set(IMGUI_SERVICE_ROOT "${CMAKE_SOURCE_DIR}/vendor/sc4-imgui-backend" CACHE PATH "Path to sc4-imgui-backend")
set(IMGUI_PUBLIC_DIR "${IMGUI_SERVICE_ROOT}/src")
set(IMGUI_INCLUDE_DIR "${IMGUI_SERVICE_ROOT}/vendor/d3d7imgui/ImGui")
set(IMGUI_LIB_DIR "${IMGUI_SERVICE_ROOT}" CACHE PATH "Path containing imgui.lib from sc4-imgui-backend build")

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

file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
set(SC4_DLL_DEF "${CMAKE_CURRENT_SOURCE_DIR}/SC4AdvancedLotPlop.def")

add_library(${PROJECT_NAME} SHARED ${PROJECT_SOURCES} ${SC4_DLL_DEF})

# Include paths required by the plugin
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/vendor/DirectXTK/Inc
    ${IMGUI_PUBLIC_DIR}
    ${IMGUI_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/vendor/wil/include
)

# Link against gzcom2 and spdlog
set(SC4_DLL_LINK_LIBS
    ${SPDLOG_TARGET}
    gzcom2
)
list(APPEND SC4_DLL_LINK_LIBS SC4AdvancedLotPlopCore)

# Locate imgui import library produced by sc4-imgui-backend
file(GLOB_RECURSE IMGUI_LIB_CANDIDATES
    "${IMGUI_LIB_DIR}/imgui.lib"
    "${IMGUI_LIB_DIR}/*/imgui.lib"
    "${IMGUI_LIB_DIR}/*/*/imgui.lib"
    "${IMGUI_LIB_DIR}/*/*/*/imgui.lib"
)
list(LENGTH IMGUI_LIB_CANDIDATES IMGUI_LIB_COUNT)
if(IMGUI_LIB_COUNT GREATER 0)
    list(GET IMGUI_LIB_CANDIDATES 0 IMGUI_LIB)
    list(APPEND SC4_DLL_LINK_LIBS "${IMGUI_LIB}")
    message(STATUS "Using imgui.lib: ${IMGUI_LIB}")
else()
    message(FATAL_ERROR "imgui.lib not found. Set IMGUI_LIB_DIR to the directory containing imgui.lib from sc4-imgui-backend.")
endif()

# Core dependencies
if(WIN32)
    list(APPEND SC4_DLL_LINK_LIBS kernel32 user32 Version d3d11 dxgi windowscodecs ddraw dxguid)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ${SC4_DLL_LINK_LIBS})

# Build settings
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "SC4AdvancedLotPlop"
    SUFFIX ".dll"
    PREFIX ""
)

if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
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
            COMMENT "Deploying SC4AdvancedLotPlop.dll to Plugins folder"
        )

        message(STATUS "Deployment targets configured:")
        message(STATUS "  - SC4AdvancedLotPlop.dll -> ${SC4_PLUGINS_DIR}/")
    else()
        message(WARNING "SC4 installation not found. Manual deployment required.")
    endif()
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
