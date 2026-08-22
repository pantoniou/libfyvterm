# ---------------------------------------------------------------------------
# Targets, following the libfytimui/libfymd4c layout:
#
#   fyvterm         - primary library (shared when BUILD_SHARED_LIBS)
#   fyvterm_static  - always a static archive
#
# Both are built from the same sources with hidden visibility; only the
# public entry points reach the ABI.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

# Handle absolute include and lib dirs outside of CMAKE_INSTALL_PREFIX
include(${PROJECT_SOURCE_DIR}/cmake/JoinPaths.cmake)
join_paths(PKGCONFIG_INCLUDEDIR "\${prefix}" "${CMAKE_INSTALL_INCLUDEDIR}")
join_paths(PKGCONFIG_LIBDIR "\${prefix}" "${CMAKE_INSTALL_LIBDIR}")
configure_file(${PROJECT_SOURCE_DIR}/cmake/libfyvterm.pc.in libfyvterm.pc @ONLY)

set(_fyvterm_sources
    encoding.c
    keyboard.c
    mouse.c
    parser.c
    pen.c
    screen.c
    state.c
    unicode.c
    fyvt.c
)

set(_fyvterm_incdir ${PROJECT_SOURCE_DIR}/include)

add_library(fyvterm        ${_fyvterm_sources})
add_library(fyvterm_static STATIC ${_fyvterm_sources})

# Keep the archive named libfyvterm.a beside libfyvterm.so on ELF; use a
# distinct name where the two would collide.
set(_fyvterm_static_output fyvterm_static)
if(BUILD_SHARED_LIBS AND NOT WIN32)
    set(_fyvterm_static_output fyvterm)
endif()

include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-Wl,--exclude-libs,ALL")
check_c_source_compiles("int main(void){return 0;}" FYVTERM_HAVE_EXCLUDE_LIBS)
unset(CMAKE_REQUIRED_FLAGS)

foreach(_t fyvterm fyvterm_static)
    target_include_directories(${_t} PUBLIC
        "$<BUILD_INTERFACE:${_fyvterm_incdir}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
    target_include_directories(${_t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${_t} PRIVATE
        "FYVT_VERSION_STRING=\"${PROJECT_VERSION}\"")
    target_compile_definitions(${_t} PRIVATE "$<$<CONFIG:Debug>:DEBUG>")
    if(CMAKE_C_COMPILER_ID MATCHES GNU|Clang)
        # Hidden by default: only the FYVT_EXPORT-tagged entry points in
        # the public headers reach the ABI. CI asserts this with nm.
        target_compile_options(${_t} PRIVATE -fvisibility=hidden)
        if(FYVTERM_HAVE_EXCLUDE_LIBS)
            target_link_options(${_t} PRIVATE "-Wl,--exclude-libs,ALL")
        endif()
    endif()
    if(ASAN_C_FLAGS)
        target_compile_options(${_t} PRIVATE ${ASAN_C_FLAGS})
        target_link_options(${_t} PRIVATE ${ASAN_C_FLAGS} INTERFACE ${ASAN_C_FLAGS})
    endif()
endforeach()

set_target_properties(fyvterm PROPERTIES
    OUTPUT_NAME fyvterm
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME libfyvterm
)
set_target_properties(fyvterm_static PROPERTIES
    OUTPUT_NAME ${_fyvterm_static_output}
    POSITION_INDEPENDENT_CODE ON
    EXPORT_NAME libfyvterm_static
)

add_library(libfyvterm::libfyvterm        ALIAS fyvterm)
add_library(libfyvterm::libfyvterm_static ALIAS fyvterm_static)

install(TARGETS fyvterm fyvterm_static
    EXPORT libfyvterm-targets
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})
# <libfyvterm.h> plus the libfyvterm/ directory it includes. Nothing lands
# at a name a system libvterm also installs.
install(FILES ${_fyvterm_incdir}/libfyvterm.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${_fyvterm_incdir}/libfyvterm
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libfyvterm.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# ---------------------------------------------------------------------------
# Table regeneration.
#
# The generated .inc files are checked in, so an ordinary build needs no
# generator at all. These targets refresh them from the .tbl sources and the
# Python runtime's Unicode database. Regenerating fullwidth.inc picks up
# whatever Unicode version the local Python ships, so it is a deliberate act
# rather than a build step.
# ---------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(regen-tables
        COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/scripts/gen-encoding-tables.py
        COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/scripts/gen-wcwidth.py
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Regenerating encoding and width tables"
        USES_TERMINAL
        VERBATIM)
endif()
