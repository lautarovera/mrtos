# Host test suite (POSIX port only).
set(MRTOS_TESTS
    test_unit_task
    test_unit_sem
    test_unit_mutex
    test_unit_queue
    test_integration
    test_kernel)          # original smoke test, kept as-is

foreach(t IN LISTS MRTOS_TESTS)
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE mrtos::mrtos)
    target_compile_options(${t} PRIVATE -Wall -Wextra)
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES TIMEOUT 30)
endforeach()
