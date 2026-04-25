cmake_minimum_required(VERSION 3.31)
project(CLox C)

set(CMAKE_C_STANDARD 99)

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
