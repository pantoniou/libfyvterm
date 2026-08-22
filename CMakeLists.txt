cmake_minimum_required(VERSION 3.12)

project(libfyvterm
    VERSION "0.0.1"
    HOMEPAGE_URL "https://github.com/pantoniou/libfyvterm"
    LANGUAGES C
)

# The test suite under t/ and the tools under bin/ are not wanted when this
# project is pulled in as a subdirectory of a larger build, so both default
# off in that case.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_fyvterm_aux_default ON)
else()
    set(_fyvterm_aux_default OFF)
endif()

option(BUILD_FYVTERM_TESTS "Build the test harness and register the tests" ${_fyvterm_aux_default})
option(BUILD_FYVTERM_TOOLS "Build the unterm/vterm-ctrl/vterm-dump tools" ${_fyvterm_aux_default})
option(ENABLE_ASAN "Enable ASAN support" OFF)

if(WIN32)
    option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)
else()
    option(BUILD_SHARED_LIBS "Build using shared libraries" ON)
endif()

set(CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo MinSizeRel)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE $ENV{CMAKE_BUILD_TYPE})
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release")
    endif()
endif()

# C dialect and warnings, matching the fyai tree this library is built for:
# the newest standard the compiler supports, plus -Wall -Wextra.
#
# fyai also sets -Wdeclaration-after-statement; that is not carried here.
# This code is written C99-style throughout, with declarations at the point
# of first use, and rewriting ~150 sites to C89 placement would churn dense
# terminal state machines for a style rule.
if(CMAKE_C_COMPILER_ID MATCHES GNU|Clang)
    include(CheckCCompilerFlag)

    check_c_compiler_flag("-std=gnu2x" FYVT_HAVE_GNU2X)
    if(FYVT_HAVE_GNU2X)
        set(FYVT_C_DIALECT "-std=gnu2x")
    else()
        set(FYVT_C_DIALECT "-std=gnu11")
    endif()
    add_compile_options(${FYVT_C_DIALECT})

    set(FYVT_C_WARNINGS -Wall -Wextra)
    add_compile_options(${FYVT_C_WARNINGS})

    add_compile_definitions(_GNU_SOURCE)
elseif(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/W3)
endif()

set(ASAN_C_FLAGS)
if(ENABLE_ASAN)
    set(ASAN_C_FLAGS -fsanitize=address -fsanitize=undefined
                     -fno-omit-frame-pointer)
endif()

add_subdirectory(src)

if(BUILD_FYVTERM_TOOLS)
    add_subdirectory(bin)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/libfyvterm-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/libfyvterm-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libfyvterm)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/libfyvterm-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT libfyvterm-targets
    FILE libfyvterm-targets.cmake
    NAMESPACE libfyvterm::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libfyvterm)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/libfyvterm-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/libfyvterm-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libfyvterm)

if(BUILD_FYVTERM_TESTS)
    enable_testing()
    add_subdirectory(t)
endif()
