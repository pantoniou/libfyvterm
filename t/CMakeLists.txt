# ---------------------------------------------------------------------------
# The test suite.
#
# run-test drives the .test scripts directly: it links the harness command
# interpreter into the same binary, so there is no separate driver process,
# no pipes, and no interpreter needed at test time. One CTest case per .test
# file, so failures are reported per file and the suite runs in parallel.
# ---------------------------------------------------------------------------
add_executable(fyvt-run-test run-test.c harness.c)

target_include_directories(fyvt-run-test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR})

# The harness reaches into the library's internals, so it links the static
# archive rather than going through the public ABI.
target_link_libraries(fyvt-run-test PRIVATE fyvterm_static)

if(ASAN_C_FLAGS)
    target_compile_options(fyvt-run-test PRIVATE ${ASAN_C_FLAGS})
    target_link_options(fyvt-run-test PRIVATE ${ASAN_C_FLAGS})
endif()

file(GLOB _fyvterm_tests CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/[0-9]*.test")
list(SORT _fyvterm_tests)

foreach(_t ${_fyvterm_tests})
    get_filename_component(_name ${_t} NAME_WE)
    add_test(NAME fyvterm.${_name}
        COMMAND fyvt-run-test ${_t})
endforeach()
