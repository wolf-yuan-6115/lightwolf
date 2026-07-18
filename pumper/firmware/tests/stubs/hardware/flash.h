#ifndef TEST_HARDWARE_FLASH_H_
#define TEST_HARDWARE_FLASH_H_

#include <stddef.h>
#include <stdint.h>

#define FLASH_PAGE_SIZE 256u
#define FLASH_SECTOR_SIZE 4096u

void flash_range_erase(uint32_t offset, size_t count);
void flash_range_program(uint32_t offset, uint8_t const *data, size_t count);

#endif
