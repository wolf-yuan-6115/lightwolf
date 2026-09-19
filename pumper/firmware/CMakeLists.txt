cmake_minimum_required(VERSION 3.13)

if (DEFINED ENV{PICO_SDK_PATH})
  include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
else()
  include(pico_sdk_import.cmake)
endif()

project(rp2350_usb_dac C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_FLASH_SIZE_BYTES "2097152" CACHE STRING "RP2354 internal flash capacity")
set(PUMPER_SRAM_SIZE_BYTES "532480" CACHE STRING "RP2350 usable SRAM capacity")
set(PUMPER_PROFILE_RESERVED_BYTES "8192" CACHE STRING "Flash reserved for settings banks")
set(PUMPER_RESOURCE_WARNING_PERCENT "80" CACHE STRING "Flash/SRAM resource warning threshold")

if(NOT CMAKE_SIZE)
  find_program(CMAKE_SIZE NAMES arm-none-eabi-size REQUIRED)
endif()

pico_sdk_init()

add_executable(rp2350_usb_dac
  src/main.c
  src/usb_descriptors.c
  src/i2s_out.c
  src/audio_controls.c
  src/audio_format.c
  src/audio_block_queue.c
  src/hid_activity.c
  src/audio_feedback.c
  src/quirk_os_guessing.c
  src/eq_config.c
  src/eq_dsp.c
  src/eq_protocol.c
  src/eq_settings.c
)

pico_override_flash_size(rp2350_usb_dac ${PICO_FLASH_SIZE_BYTES})
pico_set_binary_type(rp2350_usb_dac copy_to_ram)

pico_generate_pio_header(rp2350_usb_dac ${CMAKE_CURRENT_LIST_DIR}/src/audio_i2s.pio)

target_include_directories(rp2350_usb_dac PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(rp2350_usb_dac PRIVATE
  pico_stdlib
  pico_multicore
  pico_flash
  tinyusb_device
  tinyusb_board
  hardware_pio
  hardware_dma
  hardware_adc
  hardware_irq
  hardware_clocks
  hardware_vreg
  hardware_pwm
  hardware_flash
  hardware_watchdog
  pico_bootrom
  m
)

target_compile_options(rp2350_usb_dac PRIVATE -fno-trapping-math -fno-rounding-math)
set_source_files_properties(
  src/main.c src/usb_descriptors.c src/i2s_out.c src/audio_controls.c src/audio_format.c src/audio_block_queue.c
  src/hid_activity.c src/audio_feedback.c src/quirk_os_guessing.c src/eq_config.c
  src/eq_dsp.c src/eq_protocol.c src/eq_settings.c
  PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Release>:-flto>"
)
target_link_options(rp2350_usb_dac PRIVATE "$<$<CONFIG:Release>:-flto>")

pico_enable_stdio_usb(rp2350_usb_dac 0)
pico_enable_stdio_uart(rp2350_usb_dac 0)
pico_add_extra_outputs(rp2350_usb_dac)

add_custom_command(TARGET rp2350_usb_dac POST_BUILD
  COMMAND ${CMAKE_COMMAND}
    -DFIRMWARE_BIN=${CMAKE_CURRENT_BINARY_DIR}/rp2350_usb_dac.bin
    -DFLASH_SIZE=${PICO_FLASH_SIZE_BYTES}
    -DRESERVED_SIZE=${PUMPER_PROFILE_RESERVED_BYTES}
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/check_firmware_size.cmake
  COMMENT "Checking reserved EQ profile banks"
)

add_custom_command(TARGET rp2350_usb_dac POST_BUILD
  COMMAND ${CMAKE_COMMAND}
    -DFIRMWARE_ELF=$<TARGET_FILE:rp2350_usb_dac>
    -DFIRMWARE_BIN=${CMAKE_CURRENT_BINARY_DIR}/rp2350_usb_dac.bin
    -DSIZE_TOOL=${CMAKE_SIZE}
    -DFLASH_SIZE=${PICO_FLASH_SIZE_BYTES}
    -DRESERVED_SIZE=${PUMPER_PROFILE_RESERVED_BYTES}
    -DSRAM_SIZE=${PUMPER_SRAM_SIZE_BYTES}
    -DWARNING_PERCENT=${PUMPER_RESOURCE_WARNING_PERCENT}
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/report_firmware_usage.cmake
  COMMENT "Reporting firmware resources"
)
