cmake_minimum_required(VERSION 3.16)
project(text_quality_service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(gateway)
add_subdirectory(worker)