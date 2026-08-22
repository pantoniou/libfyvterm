# Standalone tools built against the library's public API.
#
# Every tool is fy-prefixed: plain unterm/vterm-ctrl/vterm-dump would collide
# with the tools a system libvterm installs.
#
#   fyvt-unterm - replay captured input and dump the final screen state
#   fyvt-ctrl   - emit control sequences to a live terminal
#   fyvt-dump   - parser-layer dump of captured input
foreach(_tool fyvt-unterm fyvt-ctrl fyvt-dump)
    add_executable(${_tool} ${_tool}.c)
    target_link_libraries(${_tool} PRIVATE fyvterm)
    if(ASAN_C_FLAGS)
        target_compile_options(${_tool} PRIVATE ${ASAN_C_FLAGS})
        target_link_options(${_tool} PRIVATE ${ASAN_C_FLAGS})
    endif()
    install(TARGETS ${_tool} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endforeach()
