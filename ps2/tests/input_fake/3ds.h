#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef volatile uint32_t vu32;
typedef volatile uint64_t vu64;
#define KEY_A 1u
#define KEY_B (1u << 1)
#define KEY_SELECT (1u << 2)
#define KEY_START (1u << 3)
#define KEY_DRIGHT (1u << 4)
#define KEY_DLEFT (1u << 5)
#define KEY_DUP (1u << 6)
#define KEY_DDOWN (1u << 7)
#define KEY_R (1u << 8)
#define KEY_L (1u << 9)
#define KEY_X (1u << 10)
#define KEY_Y (1u << 11)
#define KEY_ZL (1u << 14)
#define KEY_ZR (1u << 15)
#define KEY_TOUCH (1u << 20)
#define KEY_CPAD_RIGHT (1u << 28)
#define KEY_CPAD_LEFT (1u << 29)
#define KEY_CPAD_UP (1u << 30)
#define KEY_CPAD_DOWN (1u << 31)
#define KEY_UP (KEY_DUP | KEY_CPAD_UP)
#define CPU_TICKS_PER_MSEC 268111.856
extern vu32 *hidSharedMem;
u64 svcGetSystemTick(void);
