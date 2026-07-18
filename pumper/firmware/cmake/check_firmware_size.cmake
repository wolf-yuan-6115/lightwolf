if(NOT EXISTS "${FIRMWARE_BIN}")
  message(FATAL_ERROR "Firmware binary was not generated: ${FIRMWARE_BIN}")
endif()

file(SIZE "${FIRMWARE_BIN}" FIRMWARE_SIZE)
if(NOT DEFINED RESERVED_SIZE)
  message(FATAL_ERROR "RESERVED_SIZE was not provided")
endif()
math(EXPR MAX_FIRMWARE_SIZE "${FLASH_SIZE} - ${RESERVED_SIZE}")
if(FIRMWARE_SIZE GREATER MAX_FIRMWARE_SIZE)
  message(FATAL_ERROR
    "Firmware uses ${FIRMWARE_SIZE} bytes, overlapping the final ${RESERVED_SIZE}-byte EQ profile area "
    "(${MAX_FIRMWARE_SIZE} bytes available).")
endif()
