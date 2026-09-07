cmake_minimum_required(VERSION 3.20)
project(chronicle VERSION 1.0.0 LANGUAGES CXX)

option(CHRONICLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
find_package(Threads REQUIRED)

add_library(chronicle
    src/bloom_filter.cpp
    src/crc32.cpp
    src/database.cpp
    src/skip_list.cpp
    src/sstable.cpp
    src/wal.cpp
)
target_include_directories(chronicle PUBLIC include)
target_compile_features(chronicle PUBLIC cxx_std_20)
target_compile_options(chronicle PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(chronicle PUBLIC Threads::Threads)

if(CHRONICLE_SANITIZERS)
    target_compile_options(chronicle PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(chronicle PUBLIC -fsanitize=address,undefined)
endif()

add_executable(chronicle_cli src/cli.cpp)
target_link_libraries(chronicle_cli PRIVATE chronicle)
target_compile_options(chronicle_cli PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(chronicle_tests tests/chronicle_tests.cpp)
target_link_libraries(chronicle_tests PRIVATE chronicle)
target_compile_options(chronicle_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(chronicle_benchmark benchmarks/benchmark.cpp)
target_link_libraries(chronicle_benchmark PRIVATE chronicle)
target_compile_options(chronicle_benchmark PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(CTest)
add_test(NAME chronicle_tests COMMAND chronicle_tests)
