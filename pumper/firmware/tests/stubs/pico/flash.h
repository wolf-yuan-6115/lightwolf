#ifndef TEST_PICO_FLASH_H_
#define TEST_PICO_FLASH_H_

#include <stdint.h>

#define PICO_OK 0

int flash_safe_execute_core_init(void);
int flash_safe_execute(void (*callback)(void *), void *param, uint32_t timeout_ms);

#endif
