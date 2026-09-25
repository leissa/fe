cmake_minimum_required(VERSION 3.29 FATAL_ERROR)

project(fe VERSION 0.15.0)

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
    # Make `make test`/`ninja test` build first instead of testing a stale binary.
    set(CMAKE_SKIP_TEST_ALL_DEPENDENCY FALSE)
    include(CTest) # defines BUILD_TESTING
endif()

# CONFIGURE_DEPENDS so that a new header re-runs CMake instead of silently going uninstalled.
file(GLOB fe_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/fe/*.h")

# An OBJECT library so that a consumer can compile it into one shared library of its own;
# every other consumer then resolves these symbols there instead of carrying a copy.
add_library(fe OBJECT)
# So that `fe::fe` names the target whether fe is embedded or found via find_package.
add_library(fe::fe ALIAS fe)

target_sources(fe
    PRIVATE
        src/fe/bitset.cpp
        src/fe/cli.cpp
        src/fe/diag.cpp
        src/fe/dl.cpp
        src/fe/driver.cpp
        src/fe/error.cpp
        src/fe/loc.cpp
        src/fe/log.cpp
        src/fe/profile.cpp
        src/fe/snippet.cpp
        src/fe/src.cpp
        src/fe/sym.cpp
        src/fe/sys.cpp
        src/fe/term.cpp
        src/fe/utf8.cpp
    PUBLIC
        FILE_SET headers
        BASE_DIRS include
        TYPE HEADERS
        FILES ${fe_headers}
)
target_include_directories(fe
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(fe PUBLIC cxx_std_23)
target_link_libraries(fe PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(fe PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_definitions(fe PUBLIC _CTYPE_DISABLE_MACROS)
    target_compile_options(fe PUBLIC /utf-8 /wd4146 /wd4245)
endif()

# Public headers include these, so an installed fe ships them alongside its own.
set(fe_ankerl_headers
    ${CMAKE_CURRENT_SOURCE_DIR}/submodules/svector/include/ankerl/svector.h
    ${CMAKE_CURRENT_SOURCE_DIR}/submodules/unordered_dense/include/ankerl/stl.h
    ${CMAKE_CURRENT_SOURCE_DIR}/submodules/unordered_dense/include/ankerl/unordered_dense.h
)
if(NOT TARGET svector::svector)
    add_subdirectory(submodules/svector SYSTEM)
endif()
if(NOT TARGET unordered_dense::unordered_dense)
    add_subdirectory(submodules/unordered_dense SYSTEM)
endif()
target_link_libraries(fe
    PUBLIC
        $<BUILD_INTERFACE:svector::svector>
        $<BUILD_INTERFACE:unordered_dense::unordered_dense>
)

# Windows resolves an exported vtable only through __declspec(dllimport); see FE_API on fe::Diag.
generate_export_header(fe
    BASE_NAME FE
    EXPORT_MACRO_NAME FE_API
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/fe/api.h
)
# CMake defines <target>_EXPORTS on its own only for a SHARED or MODULE library, so an OBJECT library
# leaves it to whoever links it into a shared library of their own.
target_compile_definitions(fe PRIVATE fe_EXPORTS)
# A standalone build has no shared library to export from.
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    target_compile_definitions(fe PUBLIC FE_STATIC_DEFINE)
endif()

# Installing an embedded fe would scatter its objects through the consumer's prefix; it links them in.
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(fe_install ON)
else()
    set(fe_install OFF)
endif()
option(FE_INSTALL "If ON, generate install rules for fe." ${fe_install})

if(FE_INSTALL)
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/include/fe/api.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fe"
    )
    install(FILES ${fe_ankerl_headers} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ankerl")

    set(targets_export_name "fe-targets")

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/fe-config-version.cmake"
        VERSION ${fe_VERSION}
        # Every 0.x bump is free to break the API, so only the same minor is compatible.
        COMPATIBILITY SameMinorVersion
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
        OBJECTS DESTINATION ${CMAKE_INSTALL_LIBDIR}
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
        EXPORT ${targets_export_name}
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/fe"
        NAMESPACE fe::
    )
    install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/fe")
    install(FILES submodules/svector/LICENSE DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/fe/svector")
    install(FILES submodules/unordered_dense/LICENSE DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/fe/unordered_dense")
endif()

if(BUILD_TESTING AND CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(DOCTEST_NO_INSTALL ON)
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
