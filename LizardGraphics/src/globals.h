#pragma once

#if defined(_MSC_VER)
    #define DEBUG_BREAK __debugbreak();
#elif defined(__clang__) || defined(__GNUC__)
#if defined(__APPLE__) && defined(__aarch64__)
#define DEBUG_BREAK __asm__ volatile("brk #0");
#elif defined(__x86_64__) || defined(__i386__)
#define DEBUG_BREAK __asm__ volatile("int $0x03");  // x86/x86_64
#else
#define DEBUG_BREAK (SIGTRAP);  // Portable fallback
#endif
#else
#define DEBUG_BREAK raise(SIGTRAP);
#endif

#define HANDLE_VK_ERROR(func) \
    if (auto res = func != VK_SUCCESS) \
    { \
        LLogger::LogString(res, true); \
        DEBUG_BREAK \
    } \

#ifndef int64
#ifdef __APPLE__
typedef char __int8;
typedef short __int16;
typedef long __int32;
typedef long long __int64;
#else
#error Must define __int64 for your platform
#endif
#endif

using int8 = __int8;
using int16 = __int16;
using int32 = __int32;
using int64 = __int64;

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
