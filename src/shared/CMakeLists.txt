set(SHARED_CORE_NAME SC4AdvancedLotPlopCore)

# Create header-only interface library
add_library(${SHARED_CORE_NAME} INTERFACE)

target_include_directories(${SHARED_CORE_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${SHARED_CORE_NAME} INTERFACE reflectcpp)

# Add tests subdirectory
add_subdirectory(tests)
