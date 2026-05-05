cmake_minimum_required(VERSION 3.13)

if (DEFINED ENV{PICO_SDK_PATH})
  include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
else()
  include(pico_sdk_import.cmake)
endif()

project(rp2350_usb_dac C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(rp2350_usb_dac
  src/main.c
  src/usb_descriptors.c
  src/i2s_out.c
  src/eq_dsp.c
)

pico_generate_pio_header(rp2350_usb_dac ${CMAKE_CURRENT_LIST_DIR}/src/audio_i2s.pio)

target_include_directories(rp2350_usb_dac PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(rp2350_usb_dac PRIVATE
  pico_stdlib
  tinyusb_device
  tinyusb_board
  hardware_pio
  hardware_dma
  hardware_irq
  hardware_clocks
  hardware_pwm
  m
)

pico_enable_stdio_usb(rp2350_usb_dac 0)
pico_enable_stdio_uart(rp2350_usb_dac 0)
pico_add_extra_outputs(rp2350_usb_dac)
