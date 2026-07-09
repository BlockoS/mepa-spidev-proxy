# Copyright (c) 2026 Vincent Jardin, Vincent Cruz, Free Mobile
# SPDX-License-Identifier: BSD-4-Clause

cmake_minimum_required(VERSION 3.15)
project(mepa_spidev_proxy VERSION 2026.07 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default to an optimized build when none is chosen, so _FORTIFY_SOURCE
# (below) is effective -- it is inert without -O1+.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wshadow)

# Hardening: stack canaries on functions with local buffers/arrays.
add_compile_options(-fstack-protector-strong)

# Fortify libc buffer calls (runtime overflow checks). Only in optimized
# configs (inert otherwise); -U first so it stays warning-free even when a
# buildroot/yocto toolchain already defines _FORTIFY_SOURCE.
add_compile_options(
    "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-U_FORTIFY_SOURCE>"
    "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-D_FORTIFY_SOURCE=2>")

option(SPIPROXY_STATIC "Link the executables statically" OFF)

# development package: the wire-protocol API/contract
add_library(spiproxy INTERFACE)
add_library(mepa_spidev_proxy::spiproxy ALIAS spiproxy)
target_include_directories(spiproxy INTERFACE
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

install(FILES spiproxy.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spi_proxy)
install(TARGETS spiproxy EXPORT mepa_spidev_proxyTargets)

# the spi proxy daemon + debug CLI
add_executable(lan80xx_spid lan80xx_spid.c log.c)
set_target_properties(lan80xx_spid PROPERTIES OUTPUT_NAME "lan80xx-spid")

add_executable(spiproxy_cli spiproxy_cli.c)
set_target_properties(spiproxy_cli PROPERTIES OUTPUT_NAME "spiproxy-cli")

if (SPIPROXY_STATIC)
    target_link_options(lan80xx_spid PRIVATE -static)
    target_link_options(spiproxy_cli PRIVATE -static)
endif()

install(TARGETS lan80xx_spid spiproxy_cli
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(EXPORT mepa_spidev_proxyTargets
        FILE mepa_spidev_proxyTargets.cmake
        NAMESPACE mepa_spidev_proxy::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mepa_spidev_proxy)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/mepa_spidev_proxyConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/mepa_spidev_proxyConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mepa_spidev_proxy)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/mepa_spidev_proxyConfigVersion.cmake
    COMPATIBILITY AnyNewerVersion)
install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/mepa_spidev_proxyConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/mepa_spidev_proxyConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mepa_spidev_proxy)
