cmake_minimum_required(VERSION 3.20)

project(rivet
    VERSION 0.1.0
    DESCRIPTION "C++ concurrency and low-latency pipeline framework for robotics"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RIVET_ENABLE_TSAN "Build with ThreadSanitizer" OFF)
if(RIVET_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -g)
    add_link_options(-fsanitize=thread)
endif()

option(RIVET_BUILD_EXPERIMENTS "Build experiment programs" ON)
if(RIVET_BUILD_EXPERIMENTS)
    add_subdirectory(experiments)
endif()
