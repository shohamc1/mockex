cmake_minimum_required(VERSION 3.20)
project(mini-trading-stack LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(FEED_SRCS
    src/feed/replay_reader.cpp
    src/feed/parser.cpp
    src/feed/itch_replay_reader.cpp
    src/feed/itch_parser.cpp
)

add_executable(mini_trader
    ${FEED_SRCS}
    src/app/main_replay.cpp
)

add_executable(mini_trader_bench
    ${FEED_SRCS}
    src/app/main_bench.cpp
)

add_executable(mini_trader_bench_bookonly
    src/feed/replay_reader.cpp
    src/feed/parser.cpp
    src/app/main_bench_bookonly.cpp
)

function(add_test_exe name test_file)
    add_executable(${name}
        src/feed/replay_reader.cpp
        src/feed/parser.cpp
        ${test_file}
    )
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()

add_test_exe(test_book tests/book_tests.cpp)
add_test_exe(test_parser tests/parser_tests.cpp)
add_test_exe(test_risk tests/risk_tests.cpp)
add_test_exe(test_exchange tests/exchange_tests.cpp)

target_include_directories(mini_trader PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(mini_trader_bench PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(mini_trader_bench_bookonly PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_options(mini_trader PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(mini_trader_bench PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(mini_trader_bench_bookonly PRIVATE -Wall -Wextra -Wpedantic)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(mini_trader PRIVATE -O3 -march=native -flto -fno-exceptions -fno-rtti)
    target_compile_options(mini_trader_bench PRIVATE -O3 -march=native -flto -fno-exceptions -fno-rtti)
    target_compile_options(mini_trader_bench_bookonly PRIVATE -O3 -march=native -flto -fno-exceptions -fno-rtti)
    target_link_options(mini_trader PRIVATE -flto)
    target_link_options(mini_trader_bench PRIVATE -flto)
    target_link_options(mini_trader_bench_bookonly PRIVATE -flto)
endif()
