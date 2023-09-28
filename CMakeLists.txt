cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(fe VERSION 0.1)

#find_package(absl REQUIRED)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

add_library(fe INTERFACE)
set_property(TARGET fe PROPERTY CXX_STANDARD 20)
target_include_directories(fe
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:i${CMAKE_INSTALL_INCLUDEDIR}>)

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/cmake/fe-config-version.cmake"
    VERSION ${fe_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/fe-config.cmake.in
    ${PROJECT_BINARY_DIR}/cmake/fe-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fe)

install(
    FILES
        ${PROJECT_BINARY_DIR}/cmake/fe-config.cmake
        ${PROJECT_BINARY_DIR}/cmake/fe-config-version.cmake
    DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/cmake/fe)


install(
    TARGETS fe
    EXPORT fe-export
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
    EXPORT fe-export
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fe
    NAMESPACE fe::)

install(
    EXPORT fe-export
    FILE "fe-targets.cmake"
    NAMESPACE fe::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fe)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/fe
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

add_subdirectory(examples)

option(FE_BUILD_DOCS "If ON, documentation will be built (requires Doxygen)." OFF)

if(FE_BUILD_DOCS)
    find_package(Doxygen REQUIRED dot)
    if(DOXYGEN_FOUND)
        add_subdirectory(docs)
    endif()
endif()
