cmake_minimum_required(VERSION 3.13)
project(pumper_firmware_host_tests C)

set(CMAKE_C_STANDARD 11)
enable_testing()

add_executable(eq_host_tests
  test_eq.c
  ../src/eq_config.c
  ../src/eq_dsp.c
  ../src/eq_protocol.c
)
target_include_directories(eq_host_tests PRIVATE ../src)
target_compile_options(eq_host_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(eq_host_tests PRIVATE m)

add_test(NAME eq_host_tests COMMAND eq_host_tests)

add_executable(profile_store_tests
  test_profile_store.c
  ../src/eq_settings.c
  ../src/eq_config.c
  ../src/eq_protocol.c
)
target_include_directories(profile_store_tests PRIVATE stubs ../src)
target_compile_definitions(profile_store_tests PRIVATE
  EQ_SETTINGS_TEST=1
  PICO_FLASH_SIZE_BYTES=2097152
)
target_compile_options(profile_store_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(profile_store_tests PRIVATE m)

add_test(NAME profile_store_tests COMMAND profile_store_tests)
