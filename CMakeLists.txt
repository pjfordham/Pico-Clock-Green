cmake_minimum_required(VERSION 3.18)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(Pico-Clock-Green C CXX ASM)

set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(${PROJECT_NAME}
        Pico-Clock-Green.c
        Ds3231.c Ds3231.h)

target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIFI_SSID=\"PeteNet\"
        WIFI_PASSWORD=\"PeterFordham\"
        )

target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        )

# Pull in our pico_stdlib which pulls in commonly used features
target_link_libraries(${PROJECT_NAME} pico_cyw43_arch_lwip_threadsafe_background pico_stdlib hardware_i2c hardware_adc hardware_pwm  )

# enable usb or uart output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(${PROJECT_NAME})

