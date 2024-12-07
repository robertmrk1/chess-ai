cmake_minimum_required(VERSION 3.5.0)
project(chess_ai VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)



include_directories(include)

add_executable(chess_ai bot.cpp)
add_executable(test_ai test.cpp)

