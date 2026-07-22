cmake_minimum_required(VERSION 3.16)
project(game_core_tests LANGUAGES CXX)

add_executable(
    game_core_tests
    game_engine_test.cpp
    ../game_engine.cpp
)

target_include_directories(game_core_tests PRIVATE ../include)
target_compile_features(game_core_tests PRIVATE cxx_std_17)
