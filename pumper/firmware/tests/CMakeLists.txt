cmake_minimum_required(VERSION 3.13)
project(pumper_firmware_host_tests C)

set(CMAKE_C_STANDARD 11)
enable_testing()

add_executable(eq_host_tests
  test_eq.c
  ../src/eq_config.c
  ../src/eq_dsp.c
  ../src/audio_controls.c
  ../src/eq_protocol.c
)
target_include_directories(eq_host_tests PRIVATE ../src)
target_compile_options(eq_host_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(eq_host_tests PRIVATE m)

add_test(NAME eq_host_tests COMMAND eq_host_tests)

add_executable(audio_feedback_tests
  test_audio_feedback.c
  ../src/audio_feedback.c
)
target_include_directories(audio_feedback_tests PRIVATE ../src)
target_compile_options(audio_feedback_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME audio_feedback_tests COMMAND audio_feedback_tests)

add_executable(profile_store_tests
  test_profile_store.c
  ../src/eq_settings.c
  ../src/eq_config.c
  ../src/audio_controls.c
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

add_executable(audio_controls_tests
  test_audio_controls.c
  ../src/audio_controls.c
  ../src/eq_protocol.c
  ../src/eq_config.c
  ../src/eq_dsp.c
)
target_include_directories(audio_controls_tests PRIVATE ../src)
target_compile_options(audio_controls_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(audio_controls_tests PRIVATE m)
add_test(NAME audio_controls_tests COMMAND audio_controls_tests)

add_executable(audio_format_tests test_audio_format.c ../src/audio_format.c)
target_include_directories(audio_format_tests PRIVATE ../src)
target_compile_options(audio_format_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(audio_format_tests PRIVATE m)
add_test(NAME audio_format_tests COMMAND audio_format_tests)

add_executable(audio_block_queue_tests test_audio_block_queue.c ../src/audio_block_queue.c)
target_include_directories(audio_block_queue_tests PRIVATE ../src)
target_compile_options(audio_block_queue_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME audio_block_queue_tests COMMAND audio_block_queue_tests)

add_executable(hid_activity_tests test_hid_activity.c ../src/hid_activity.c)
target_include_directories(hid_activity_tests PRIVATE ../src)
target_compile_options(hid_activity_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME hid_activity_tests COMMAND hid_activity_tests)

add_test(NAME resource_report_tests
  COMMAND ${CMAKE_COMMAND}
    -DREPORT_SCRIPT=${CMAKE_CURRENT_LIST_DIR}/../cmake/report_firmware_usage.cmake
    -DCHECK_SCRIPT=${CMAKE_CURRENT_LIST_DIR}/../cmake/check_firmware_size.cmake
    -DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/resource-report-test
    -P ${CMAKE_CURRENT_LIST_DIR}/test_resource_report.cmake
)
