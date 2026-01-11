set(SHARED_CORE_NAME SC4AdvancedLotPlopCore)

set(SHARED_CORE_SOURCES
    ReflectEntities.cpp
)

add_library(${SHARED_CORE_NAME} STATIC ${SHARED_CORE_SOURCES})

target_include_directories(${SHARED_CORE_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${SHARED_CORE_NAME} PUBLIC reflectcpp)
