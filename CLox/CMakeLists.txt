cmake_minimum_required(VERSION 3.31)
project(CLox C)

set(CMAKE_C_STANDARD 99)

# 1. Define the options (these show up in your IDE UI)
option(DEBUG_TRACE_EXECUTION "Enable execution tracing" OFF)
option(DEBUG_PRINT_CODE      "Enable code printing"      OFF)
option(DEBUG_STRESS_GC       "Enable GC stress testing"  OFF)
option(DEBUG_LOG_GC          "Enable GC logging"         OFF)

# 2. Pass them to the compiler if they are turned ON
if(DEBUG_TRACE_EXECUTION)
        add_compile_definitions(DEBUG_TRACE_EXECUTION)
endif()

if(DEBUG_PRINT_CODE)
        add_compile_definitions(DEBUG_PRINT_CODE)
endif()

if(DEBUG_STRESS_GC)
        add_compile_definitions(DEBUG_STRESS_GC)
endif()

if(DEBUG_LOG_GC)
        add_compile_definitions(DEBUG_LOG_GC)
endif()

add_executable(CLox clox.c
        common.h
        chunk.h
        chunk.c
        memory.h
        memory.c
        debug.h
        debug.c
        value.h
        value.c
        vm.c
        vm.h
        compiler.c
        compiler.h
        scanner.c
        scanner.h
        object.h
        object.c
        table.c
        table.h
        global.c
        global.h
        utils/io.c
        utils/io.h
        utils/coerce.c
        utils/coerce.h
        utils/stringUtils.c
        utils/stringUtils.h
        stdlib/time.c
        stdlib/time.h
        stdlib/nativeIo.c
        stdlib/nativeIo.h
        stdlib/nativeErr.c
        stdlib/nativeErr.h
        stdlib/cast.c
        stdlib/cast.h
        stdlib/joinStr.c
        stdlib/joinStr.h
        stdlib/classUtils.c
        stdlib/classUtils.h
)

if(CMAKE_SYSTEM MATCHES Linux)
        target_link_libraries(CLox m)
endif()
