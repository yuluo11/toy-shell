cmake_minimum_required(VERSION 3.20)

project(toysh LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(toysh
    src/main.cpp
)
