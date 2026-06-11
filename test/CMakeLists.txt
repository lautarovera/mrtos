# Test suite: runs on the POSIX port (native) and on the GNU MSP430
# simulator (CTest invokes msp430-elf-run via CROSSCOMPILING_EMULATOR).
set(MRTOS_TESTS
    test_unit_task
    test_unit_sem
    test_unit_mutex
    test_unit_queue
    test_unit_stack
    test_integration)
if(MRTOS_PORT STREQUAL "posix")
    # Original smoke test: hardcodes ucontext-sized stacks, host-only.
    list(APPEND MRTOS_TESTS test_kernel)
endif()

foreach(t IN LISTS MRTOS_TESTS)
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE mrtos::mrtos)
    target_compile_options(${t} PRIVATE -Wall -Wextra)
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES TIMEOUT 30)
endforeach()
