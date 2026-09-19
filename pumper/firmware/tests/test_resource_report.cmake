file(MAKE_DIRECTORY "${TEST_DIRECTORY}")
set(binary "${TEST_DIRECTORY}/firmware.bin")
set(sections "${TEST_DIRECTORY}/sections.txt")
file(WRITE "${binary}" "1234567890")
file(WRITE "${sections}" ".flashtext 1 0\n.ram_vector_table 2 0\n.text 3 0\n.data 4 0\n.bss 5 0\n.heap 6 0\n.stack_dummy 7 0\n.stack1_dummy 8 0\n.scratch_x 9 0\n.scratch_y 10 0\n")

set(base_args -DFIRMWARE_BIN=${binary} -DSIZE_OUTPUT_FILE=${sections} -DRESERVED_SIZE=0
              -DWARNING_PERCENT=80 -P ${REPORT_SCRIPT})
execute_process(COMMAND ${CMAKE_COMMAND} -DFLASH_SIZE=100 -DSRAM_SIZE=100 ${base_args}
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT "${output}${error}" MATCHES "SRAM: 54/100 bytes")
  message(FATAL_ERROR "Resource report totals were not emitted correctly: ${output}${error}")
endif()

file(WRITE "${binary}" "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345")
execute_process(COMMAND ${CMAKE_COMMAND} -DFLASH_SIZE=100 -DSRAM_SIZE=100 ${base_args}
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT "${output}${error}" MATCHES "Firmware flash use is 85.0%")
  message(FATAL_ERROR "The 80% flash warning was not emitted: ${output}${error}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} -DFLASH_SIZE=80 -DSRAM_SIZE=100 ${base_args}
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "only 80 bytes are available")
  message(FATAL_ERROR "Flash overflow did not fail the report: ${output}${error}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} -DFLASH_SIZE=100 -DSRAM_SIZE=50 ${base_args}
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "Static SRAM use is 54 bytes")
  message(FATAL_ERROR "SRAM overflow did not fail the report: ${output}${error}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} -DFIRMWARE_BIN=${binary} -DFLASH_SIZE=90
                        -DRESERVED_SIZE=10 -P ${CHECK_SCRIPT}
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "overlapping the final 10-byte")
  message(FATAL_ERROR "Reserved-profile overlap did not fail: ${output}${error}")
endif()
