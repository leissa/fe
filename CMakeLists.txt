cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(fe VERSION 0.1)

set(CMAKE_CXX_STANDARD 20)
if(MSVC)
    add_compile_options(/utf-8 /wd4146 /wd4245)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(absl REQUIRED)

add_subdirectory(include)
add_subdirectory(examples)

option(FE_BUILD_DOCS "If ON, documentation will be built (requires Doxygen)." OFF)

if(FE_BUILD_DOCS)
    find_package(Doxygen REQUIRED dot)
    if(DOXYGEN_FOUND)
        add_subdirectory(docs)
    endif()
endif()
