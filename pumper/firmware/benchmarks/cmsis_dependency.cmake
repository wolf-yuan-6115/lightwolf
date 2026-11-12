# Pinned, benchmark-only dependency. Production firmware never fetches CMSIS-DSP.
set(PUMPER_CMSIS_DSP_PATH "" CACHE PATH "Existing CMSIS-DSP v1.16.2 source tree")
if(NOT PUMPER_CMSIS_DSP_PATH)
  include(FetchContent)
  FetchContent_Declare(pumper_cmsis_dsp
    URL https://codeload.github.com/ARM-software/CMSIS-DSP/tar.gz/refs/tags/v1.16.2
    URL_HASH SHA256=725c006d9120a71c462124c3becdae2600fa318d0a86b07150e236a6fc757fa6)
  FetchContent_GetProperties(pumper_cmsis_dsp)
  if(NOT pumper_cmsis_dsp_POPULATED)
    FetchContent_Populate(pumper_cmsis_dsp)
  endif()
  set(PUMPER_CMSIS_DSP_PATH "${pumper_cmsis_dsp_SOURCE_DIR}")
endif()
add_library(pumper_cmsis_kernel STATIC
  ${PUMPER_CMSIS_DSP_PATH}/Source/FilteringFunctions/arm_biquad_cascade_df1_32x64_q31.c)
target_include_directories(pumper_cmsis_kernel SYSTEM PUBLIC ${PUMPER_CMSIS_DSP_PATH}/Include)
target_compile_definitions(pumper_cmsis_kernel PUBLIC ARM_MATH_LOOPUNROLL)
# Upstream's scalar implementation assumes two's-complement shifts/wrap.
# Audit its arithmetic with the independent checked oracle, not shift-UBSan.
target_compile_options(pumper_cmsis_kernel PRIVATE -O3 -fwrapv -fno-sanitize=shift)
if(PICO_PLATFORM STREQUAL "rp2350-arm-s")
  target_include_directories(pumper_cmsis_kernel SYSTEM PUBLIC
    ${PICO_SDK_PATH}/src/rp2_common/cmsis/stub/CMSIS/Core/Include)
else()
  target_compile_definitions(pumper_cmsis_kernel PUBLIC __GNUC_PYTHON__)
endif()
