cmake_minimum_required(VERSION 3.23 FATAL_ERROR)

project(fe VERSION 0.1)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

option(FE_ABSL "If ON, use abseil containers, otherwise use std contaienrs" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_library(fe INTERFACE)
target_compile_features(fe INTERFACE cxx_std_20)
target_sources(fe
    INTERFACE FILE_SET headers
    BASE_DIRS include
    TYPE HEADERS
    FILES
        include/fe/arena.h
        include/fe/assert.h
        include/fe/cast.h
        include/fe/lexer.h
        include/fe/loc.h
        include/fe/parser.h
        include/fe/ring.h
        include/fe/sym.h
        include/fe/utf8.h
)
target_include_directories(fe
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include> # for config.h
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
configure_file(
    include/fe/config.h.in
    include/fe/config.h
)
if(FE_ABSL)
    target_link_libraries(fe
        INTERFACE
            absl::flat_hash_map absl::flat_hash_set
            absl::node_hash_map absl::node_hash_set
    )
endif()

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/fe-config-version.cmake"
    VERSION ${fe_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    cmake/fe-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/fe-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/fe"
)
install(
    TARGETS fe
    EXPORT fe-targets
    FILE_SET headers
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/fe-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/fe-config-version.cmake"
    DESTINATION
        "${CMAKE_INSTALL_LIBDIR}/cmake/fe"
)
install(
    FILES
    "${CMAKE_CURRENT_BINARY_DIR}/include/fe/config.h"
    DESTINATION
        "${CMAKE_INSTALL_INCLUDEDIR}/fe"
)
install(
    EXPORT fe-targets
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/fe"
    NAMESPACE fe::
)
install(
    DIRECTORY include/fe
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h"
)
add_subdirectory(examples)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(external/doctest)
        add_subdirectory(tests)
    endif()
endif()

option(FE_BUILD_DOCS "If ON, documentation will be built (requires Doxygen)." OFF)
if(FE_BUILD_DOCS)
    find_package(Doxygen REQUIRED dot)
    if(DOXYGEN_FOUND)
        add_subdirectory(docs)
    endif()
endif()
