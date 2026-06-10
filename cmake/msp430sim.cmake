# Toolchain file: GNU MSP430 simulator (msp430-elf-run).
#
#   cmake -B build-sim --toolchain cmake/msp430sim.cmake \
#         -DMSP430_GCC_DIR=$HOME/toolchains/msp430-gcc-9.3.1.11_linux64
#
# Runs the kernel test suite on the real MSP430 ISA without hardware:
# CTest launches every test binary inside msp430-elf-run via
# CMAKE_CROSSCOMPILING_EMULATOR. No device support files needed -
# -msim provides the linker script and the syscall stubs (printf/exit).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR msp430)

if(NOT MSP430_GCC_DIR)
    set(MSP430_GCC_DIR "$ENV{MSP430_GCC_DIR}")
endif()
if(NOT MSP430_GCC_DIR)
    message(FATAL_ERROR "Set MSP430_GCC_DIR to your msp430-gcc install")
endif()
set(ENV{MSP430_GCC_DIR} "${MSP430_GCC_DIR}")

set(CMAKE_C_COMPILER ${MSP430_GCC_DIR}/bin/msp430-elf-gcc)
set(CMAKE_CROSSCOMPILING_EMULATOR ${MSP430_GCC_DIR}/bin/msp430-elf-run)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-mcpu=msp430x -msim")
set(CMAKE_C_FLAGS_RELEASE "-Os -g -DNDEBUG" CACHE STRING "")
set(CMAKE_C_FLAGS_DEBUG   "-Os -g" CACHE STRING "")

set(MRTOS_PORT msp430sim CACHE STRING "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
