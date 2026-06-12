# mRTOS - middleware-style integration.
#
# Consume from a parent project:
#
#   set(MRTOS_CONFIG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/config)  # your mrtos_config.h
#   add_subdirectory(third_party/mrtos)
#   target_link_libraries(app PRIVATE mrtos::mrtos)
#
# Port selection is automatic: cross builds (CMAKE_SYSTEM_NAME=Generic,
# e.g. via cmake/msp430fr5994.cmake) get the msp430fr59xx port, native
# builds get the POSIX port. Override with -DMRTOS_PORT=<name>.
cmake_minimum_required(VERSION 3.20)
project(mrtos C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED MRTOS_PORT)
    if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
        set(MRTOS_PORT msp430fr59xx)
    else()
        set(MRTOS_PORT posix)
    endif()
endif()
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/port/${MRTOS_PORT}/port.c)
    message(FATAL_ERROR "mRTOS: unknown port '${MRTOS_PORT}'")
endif()
message(STATUS "mRTOS: port = ${MRTOS_PORT}")

add_library(mrtos STATIC
    kernel/mrtos.c
    port/${MRTOS_PORT}/port.c)
add_library(mrtos::mrtos ALIAS mrtos)

target_include_directories(mrtos PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel
    ${CMAKE_CURRENT_SOURCE_DIR}/port/${MRTOS_PORT})

# The application owns mrtos_config.h. Standalone builds fall back to
# the in-tree configs so the library and tests are buildable as-is.
if(DEFINED MRTOS_CONFIG_DIR)
    target_include_directories(mrtos PUBLIC ${MRTOS_CONFIG_DIR})
elseif(MRTOS_PORT STREQUAL "posix")
    target_include_directories(mrtos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/test)
elseif(MRTOS_PORT STREQUAL "msp430sim")
    target_include_directories(mrtos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/test/sim)
else()
    target_include_directories(mrtos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app)
endif()

if(PROJECT_IS_TOP_LEVEL)
    if(MRTOS_PORT MATCHES "^(posix|msp430sim)$")
        option(MRTOS_BUILD_TESTS "Build the host test suite" ON)
        if(MRTOS_BUILD_TESTS)
            enable_testing()
            add_subdirectory(test)
        endif()
        if(MRTOS_PORT STREQUAL "msp430sim")
            # Instruction-count micro-benchmark, driven by tools/bench.py
            # (gdb target sim counts instructions between markers).
            add_executable(bench bench/bench.c)
            target_link_libraries(bench PRIVATE mrtos::mrtos)
        endif()
    elseif(MRTOS_PORT STREQUAL "msp430fr59xx")
        # Reference application: LaunchPad MSP-EXP430FR5994 demo.
        add_executable(mrtos_fr5994.elf app/main_fr5994.c)
        target_link_libraries(mrtos_fr5994.elf PRIVATE mrtos::mrtos)
        add_custom_command(TARGET mrtos_fr5994.elf POST_BUILD
            COMMAND ${CMAKE_SIZE_UTIL} $<TARGET_FILE:mrtos_fr5994.elf>
            COMMENT "Section sizes")
    endif()
endif()
