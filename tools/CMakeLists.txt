# Tools for SC4 Plop and Paint

# Property XML to JSON converter
add_executable(convert_properties convert_properties.cpp)

target_compile_definitions(convert_properties PRIVATE NOMINMAX)

target_include_directories(convert_properties PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(convert_properties PRIVATE
    SC4PlopAndPaintCore
    pugixml::pugixml
    reflectcpp::reflectcpp
)

# Set output directory
set_target_properties(convert_properties PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
)

# Copy DLLs to output directory for execution
if(MSVC)
    # vcpkg stores debug DLLs in debug/bin and release DLLs in bin
    set(VCPKG_DEBUG_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin")
    set(VCPKG_RELEASE_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")

    # Copy DLLs to executable directory based on build configuration
    add_custom_command(TARGET convert_properties POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<IF:$<CONFIG:Debug>,${VCPKG_DEBUG_BIN_DIR},${VCPKG_RELEASE_BIN_DIR}>"
            "$<TARGET_FILE_DIR:convert_properties>"
        COMMENT "Copying $<IF:$<CONFIG:Debug>,debug,release> DLLs to executable directory"
        VERBATIM
    )
endif()
