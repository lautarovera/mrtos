# Toolchain file: TI msp430-gcc targeting the MSP430FR5994.
#
#   cmake -B build-msp430 --toolchain cmake/msp430fr5994.cmake \
#         -DMSP430_GCC_DIR=$HOME/toolchains/msp430-gcc-9.3.1.11_linux64 \
#         -DMSP430_SUPPORT_DIR=$HOME/toolchains/msp430-gcc-support-files/include
#
# MSP430_GCC_DIR may also come from the environment. MSP430_SUPPORT_DIR
# defaults to <gcc-dir>/include (correct for TI's full installer; the
# standalone tarball needs the separately downloaded support files).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR msp430)

if(NOT MSP430_GCC_DIR)
    set(MSP430_GCC_DIR "$ENV{MSP430_GCC_DIR}")
endif()
if(NOT MSP430_GCC_DIR)
    message(FATAL_ERROR "Set MSP430_GCC_DIR to your msp430-gcc install")
endif()
if(NOT MSP430_SUPPORT_DIR)
    if(DEFINED ENV{MSP430_SUPPORT_DIR})
        set(MSP430_SUPPORT_DIR "$ENV{MSP430_SUPPORT_DIR}")
    else()
        set(MSP430_SUPPORT_DIR "${MSP430_GCC_DIR}/include")
    endif()
endif()
# This file is re-read by try_compile sub-projects, which do not inherit
# -D cache entries; the environment is the reliable carrier.
set(ENV{MSP430_GCC_DIR}     "${MSP430_GCC_DIR}")
set(ENV{MSP430_SUPPORT_DIR} "${MSP430_SUPPORT_DIR}")

set(CMAKE_C_COMPILER   ${MSP430_GCC_DIR}/bin/msp430-elf-gcc)
set(CMAKE_SIZE_UTIL    ${MSP430_GCC_DIR}/bin/msp430-elf-size CACHE FILEPATH "")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# -Os: smallest code; mRTOS is sized for FRAM parts.
set(CMAKE_C_FLAGS_INIT
    "-mmcu=msp430fr5994 -mhwmult=f5series -ffunction-sections -fdata-sections -I${MSP430_SUPPORT_DIR}")
# Pin the per-config flags in the cache: the platform defaults are
# appended after *_INIT, and their -O3 would override -Os.
set(CMAKE_C_FLAGS_RELEASE    "-Os -g -DNDEBUG" CACHE STRING "")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -g -DNDEBUG" CACHE STRING "")
# -mmcu lives only in the compile flags: CMake repeats C flags on the
# link line, and a second -mmcu makes ld see the device script twice.
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-L${MSP430_SUPPORT_DIR} -Wl,--gc-sections")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
