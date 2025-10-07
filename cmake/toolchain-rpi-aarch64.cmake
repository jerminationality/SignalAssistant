# Toolchain for cross-compiling GuitarPi to a 64-bit Raspberry Pi (Pi 4/5)
# Usage:
#   cmake -S . -B build-rpi \
#         -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-rpi-aarch64.cmake \
#         -DPI_SYSROOT=/path/to/sysroot \
#         -DPI_TOOLCHAIN_PREFIX=aarch64-linux-gnu

if(NOT PI_SYSROOT)
    message(FATAL_ERROR "Set -DPI_SYSROOT to your Raspberry Pi sysroot path")
endif()

if(NOT PI_TOOLCHAIN_PREFIX)
    set(PI_TOOLCHAIN_PREFIX aarch64-linux-gnu)
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_SYSROOT "${PI_SYSROOT}")
set(CMAKE_STAGING_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../stage")

set(CMAKE_C_COMPILER   "${PI_TOOLCHAIN_PREFIX}-gcc")
set(CMAKE_CXX_COMPILER "${PI_TOOLCHAIN_PREFIX}-g++")
set(CMAKE_FIND_ROOT_PATH "${PI_SYSROOT}")

set(CMAKE_C_FLAGS_INIT   "-march=armv8-a -mtune=cortex-a76")
set(CMAKE_CXX_FLAGS_INIT "-march=armv8-a -mtune=cortex-a76")

set(QT_HOST_PATH $ENV{QT_HOST_PATH} CACHE PATH "Path to host Qt installation")
if(NOT EXISTS "${QT_HOST_PATH}")
    message(STATUS "Optional: set QT_HOST_PATH to speed up host tools lookup")
endif()

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SYSROOT}/usr/lib/cmake")
