# Standalone tools built against the library's public API.
#
# Every tool is fy-prefixed: plain unterm/vterm-ctrl/vterm-dump would collide
# with the tools a system libvterm installs.
#
#   fyvt-unterm - replay captured input and dump the final screen state
#   fyvt-ctrl   - emit control sequences to a live terminal
#   fyvt-dump   - parser-layer dump of captured input
set(_fyvterm_tools
    unterm:fyvt-unterm
    vterm-ctrl:fyvt-ctrl
    vterm-dump:fyvt-dump
)

foreach(_entry ${_fyvterm_tools})
    string(REPLACE ":" ";" _pair ${_entry})
    list(GET _pair 0 _src)
    list(GET _pair 1 _name)

    add_executable(${_name} ${_src}.c)
    target_link_libraries(${_name} PRIVATE fyvterm)
    if(ASAN_C_FLAGS)
        target_compile_options(${_name} PRIVATE ${ASAN_C_FLAGS})
        target_link_options(${_name} PRIVATE ${ASAN_C_FLAGS})
    endif()
    install(TARGETS ${_name} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endforeach()
