#ifndef TEST_HARDWARE_REGS_ADDRESSMAP_H_
#define TEST_HARDWARE_REGS_ADDRESSMAP_H_

#include <stdint.h>

extern uint8_t g_fake_flash[];
#define XIP_BASE ((uintptr_t)g_fake_flash)

#endif
