#ifndef _HOBBYL_COMMON_H
#define _HOBBYL_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// #define hl_DEBUG_TRACE_EXECUTION
// #define hl_DEBUG_PRINT_CODE

// #define hl_DEBUG_STRESS_GC
// #define hl_DEBUG_LOG_GC

#define NAN_BOXING

#define hl_UNUSED __attribute__((unused))
#define hl_FALLTHROUGH __attribute__((fallthrough))

#define hl_U8_COUNT (UINT8_MAX + 1)

typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef int32_t  s32;
typedef uint64_t u64;
typedef int64_t  s64;

typedef float    f32;
typedef double   f64;

#endif // _HOBBYL_COMMON_H
