# Smoke test for the installed package; not part of the fe build.
# Configure it against an install prefix to check that `find_package(fe)` alone suffices:
#   cmake -S tests/consumer -B build-consumer -DCMAKE_PREFIX_PATH=<prefix>
cmake_minimum_required(VERSION 3.29 FATAL_ERROR)

project(fe-consumer VERSION 0.1.0 LANGUAGES CXX)

find_package(fe REQUIRED)

add_executable(fe-consumer main.cpp)
target_link_libraries(fe-consumer PRIVATE fe::fe)
