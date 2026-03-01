set(SHARED_TESTS_NAME SC4PlopAndPaintCore_Tests)

set(SHARED_TEST_SOURCES
    test_main.cpp
    test_entities.cpp
)

add_executable(${SHARED_TESTS_NAME} ${SHARED_TEST_SOURCES})

target_include_directories(${SHARED_TESTS_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/vendor/DBPFKit/vendor/catch2/src
    ${jsoncons_SOURCE_DIR}/include
)

target_link_libraries(${SHARED_TESTS_NAME} PRIVATE
    ${SHARED_CORE_NAME}
    reflectcpp::reflectcpp
    Catch2::Catch2
)

# Enable C++20 for test target
target_compile_features(${SHARED_TESTS_NAME} PRIVATE cxx_std_20)

# Add test discovery
add_test(NAME ${SHARED_TESTS_NAME} COMMAND ${SHARED_TESTS_NAME})
