cmake_minimum_required(VERSION 3.16)
project(ats_personal_lab CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
if (MSVC)
    add_compile_options(/W4 /utf-8 /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(trie STATIC src/Utf8.cpp src/Trie.cpp src/Dictionary.cpp src/Benchmark.cpp)
target_include_directories(trie PUBLIC src)

add_executable(trie_app src/main.cpp)
target_link_libraries(trie_app PRIVATE trie)

add_executable(trie_tests tests/tests.cpp)
target_link_libraries(trie_tests PRIVATE trie)

enable_testing()
add_test(NAME trie_tests COMMAND trie_tests)
