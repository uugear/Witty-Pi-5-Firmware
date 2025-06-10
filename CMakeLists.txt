cmake_minimum_required(VERSION 3.20)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_PLATFORM "rp2350" CACHE INTERNAL "")
set(PICO_BOARD "uugear_wittypi5_hat_plus" CACHE INTERNAL "")

set(PICO_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lib/pico-sdk")
include(pico_sdk_import.cmake)

project(wp5fw C CXX ASM)

execute_process(COMMAND ${CMAKE_C_COMPILER} --version OUTPUT_VARIABLE GCC_VERSION_OUTPUT)
string(REGEX MATCH "([0-9]+\\.[0-9]+)" GCC_MAJOR_MINOR_VERSION "${GCC_VERSION_OUTPUT}")
message(STATUS "Detected GCC Version: ${GCC_MAJOR_MINOR_VERSION}")
if (GCC_MAJOR_MINOR_VERSION VERSION_LESS "14.2")
    message(FATAL_ERROR "GCC version ${GCC_MAJOR_MINOR_VERSION} is too old. Please use GCC 12.0 or newer.")
endif()

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.0.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 2.0.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

pico_sdk_init()

file(GLOB_RECURSE SOURCES "src/*.c")
add_executable(${PROJECT_NAME} ${SOURCES}
    ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source/ff.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source
)

# pull in dependencies
target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    tinyusb_board
    tinyusb_device
    hardware_powman
    pico_i2c_slave
    hardware_i2c
    hardware_adc
)

# enable usb output and uart output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 1)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(${PROJECT_NAME})
