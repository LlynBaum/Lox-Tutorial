#ifndef clox_common_h
#define clox_common_h

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifndef DEBUG_TRACE_EXECUTION
  // Uncomment the line below to enable it by default in your IDE
  // #define DEBUG_TRACE_EXECUTION
#endif

#ifndef DEBUG_PRINT_CODE
  // #define DEBUG_PRINT_CODE
#endif

#ifndef DEBUG_STRESS_GC
  // #define DEBUG_STRESS_GC
#endif

#ifndef DEBUG_LOG_GC
  // #define DEBUG_LOG_GC
#endif

#define UINT8_COUNT (UINT8_MAX + 1)
#define UINT24_MAX (16777215)
#define UINT24_COUNT (UINT24_MAX + 1)

#endif // clox_common_h
