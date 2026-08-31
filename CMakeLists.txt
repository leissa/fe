cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

project(fe VERSION 0.12.0)

include(CMakePackageConfigHelpers)
include(CheckIncludeFileCXX)
include(GenerateExportHeader)
include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(CTest) # defines BUILD_TESTING
endif()

# fe::Diag - and hence fe::Driver and fe::Error - plus fe::Snippet, fe::dl, fe::sys, and fe::Profiler
# need a translation unit of their own; everything else stays header-only either way.
option(FE_LIB "If ON, build fe-lib with the components that are not header-only." ON)

add_library(fe INTERFACE)

target_sources(fe
    INTERFACE
        FILE_SET headers
        BASE_DIRS include
        TYPE HEADERS
        FILES
            include/fe/algo.h
            include/fe/arena.h
            include/fe/assert.h
            include/fe/bitset.h
            include/fe/cast.h
            include/fe/container.h
            include/fe/dbg.h
            include/fe/diag.h
            include/fe/dl.h
            include/fe/enum.h
            include/fe/error.h
            include/fe/driver.h
            include/fe/hash.h
            include/fe/format.h
            include/fe/lct.h
            include/fe/lexer.h
            include/fe/loc.h
            include/fe/log.h
            include/fe/parser.h
            include/fe/profile.h
            include/fe/restore.h
            include/fe/ring.h
            include/fe/snippet.h
            include/fe/span.h
            include/fe/src.h
            include/fe/sym.h
            include/fe/sys.h
            include/fe/term.h
            include/fe/utf8.h
            include/fe/vector.h
            include/fe/worklist.h
            include/fe/xtrie.h
)
target_include_directories(fe INTERFACE $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(fe INTERFACE cxx_std_23)

if(MSVC)
    target_compile_definitions(fe INTERFACE _CTYPE_DISABLE_MACROS)
    target_compile_options(fe INTERFACE /utf-8 /wd4146 /wd4245)
endif()

option(FE_ABSL "If ON, use abseil containers, otherwise use std containers" OFF)
if(FE_ABSL)
    target_compile_definitions(fe INTERFACE FE_ABSL)
    target_link_libraries(fe
        INTERFACE
            absl::flat_hash_map absl::flat_hash_set
            absl::inlined_vector
            absl::node_hash_map absl::node_hash_set
    )
endif()

# An OBJECT library so that a consumer can compile it into one shared library of its own;
# every other consumer then resolves these symbols there instead of carrying a copy.
if(FE_LIB)
    add_library(fe-lib OBJECT)
    target_sources(fe-lib
        PRIVATE
            src/fe/diag.cpp
            src/fe/dl.cpp
            src/fe/driver.cpp
            src/fe/loc.cpp
            src/fe/profile.cpp
            src/fe/snippet.cpp
            src/fe/sys.cpp
    )
    target_link_libraries(fe-lib
        PUBLIC
            fe
        PRIVATE
            ${CMAKE_DL_LIBS}
    )
    set_target_properties(fe-lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # A vtable is a data symbol and Windows resolves an exported one only through __declspec(dllimport);
    # see the FE_API annotation on fe::Diag.
    generate_export_header(fe-lib
        BASE_NAME FE
        EXPORT_MACRO_NAME FE_API
        EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/fe/api.h
    )
    # CMake defines <target>_EXPORTS on its own only for a SHARED or MODULE library.
    # fe-lib is an OBJECT library, so whoever links it into a shared library of their own has to say so, too.
    target_compile_definitions(fe-lib PRIVATE fe_lib_EXPORTS)
    # A standalone build has no shared library to export from; a consumer that links fe-lib into a
    # static one of its own defines FE_STATIC_DEFINE itself.
    if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
        target_compile_definitions(fe INTERFACE FE_STATIC_DEFINE)
    endif()
    target_include_directories(fe INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)

    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/include/fe/api.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fe"
    )
endif()

set(targets_export_name "fe-targets")

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
    EXPORT ${targets_export_name}
    FILE_SET headers
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
if(FE_LIB)
    install(
        TARGETS fe-lib
        EXPORT ${targets_export_name}
        OBJECTS DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/fe-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/fe-config-version.cmake"
    DESTINATION
        "${CMAKE_INSTALL_LIBDIR}/cmake/fe"
)
install(
    EXPORT ${targets_export_name}
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/fe"
    NAMESPACE fe::
)
install(
    DIRECTORY include/fe
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h"
)

if(BUILD_TESTING AND CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    add_subdirectory(submodules/doctest)
    add_subdirectory(tests)
endif()

option(FE_BUILD_DOCS "If ON, documentation will be built (requires Doxygen)." OFF)
if(FE_BUILD_DOCS)
    find_package(Doxygen REQUIRED dot)
    if(DOXYGEN_FOUND)
        add_subdirectory(docs)
    endif()
endif()
