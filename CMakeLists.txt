cmake_minimum_required(VERSION 3.20)
project(low_latency_order_matching_engine_2025 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(lom_core
    src/core/order_book.cpp
    src/engine/matching_engine.cpp
    src/net/websocket_server.cpp
    src/util/base64.cpp
    src/util/sha1.cpp
)

target_include_directories(lom_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/net
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
)

target_link_libraries(lom_core
    PUBLIC
        nlohmann_json::nlohmann_json
)

if (WIN32)
    target_link_libraries(lom_core PRIVATE ws2_32)
endif()

add_executable(lom_engine src/main.cpp)
target_link_libraries(lom_engine PRIVATE lom_core)

add_executable(lom_benchmark src/benchmark.cpp)
target_link_libraries(lom_benchmark PRIVATE lom_core)

enable_testing()
add_executable(lom_tests tests/order_book_tests.cpp)
target_link_libraries(lom_tests PRIVATE lom_core)
add_test(NAME lom_tests COMMAND lom_tests)

