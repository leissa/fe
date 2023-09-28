cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(fe++ VERSION 0.1)

set(CMAKE_CXX_STANDARD 20)
if(MSVC)
    add_compile_options(/utf-8 /wd4146 /wd4245)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}; shared libs: ${BUILD_SHARED_LIBS}")
find_package(absl REQUIRED)

if(WIN32)
    add_compile_definitions(NOMINMAX) # prevents windows.h defining min/max macros
else()
    add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(src)
add_subdirectory(examples)
