set(APP_NAME SC4AdvancedLotPlopCli)

file(GLOB APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(${APP_NAME} ${APP_SOURCES})

target_compile_definitions(${APP_NAME}
    PRIVATE
        SC4_ADVANCED_LOT_PLOP_VERSION="${PROJECT_VERSION}"
)

target_include_directories(${APP_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(${APP_NAME} PRIVATE ${SPDLOG_TARGET} SC4AdvancedLotPlopCore taywee::args DBPFKitLib)

install(TARGETS ${APP_NAME}
    RUNTIME DESTINATION bin
)
