cmake_minimum_required(VERSION 3.20)
project(OpenCitadelWindowsSmokeTests LANGUAGES CXX)

enable_testing()

add_executable(open_citadel_host_memory_test
    host_memory_test.cpp
    ../../loader/host_memory.cpp
)
target_include_directories(open_citadel_host_memory_test PRIVATE
    ../../loader
)
target_compile_features(open_citadel_host_memory_test PRIVATE cxx_std_17)

add_test(NAME host-memory COMMAND open_citadel_host_memory_test)

add_executable(open_citadel_elf_abi_test elf_abi_test.cpp)
target_include_directories(open_citadel_elf_abi_test PRIVATE ../../loader)
target_compile_features(open_citadel_elf_abi_test PRIVATE cxx_std_17)

add_test(NAME elf32-abi COMMAND open_citadel_elf_abi_test)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(open_citadel_win32_gl_thunk_test win32_gl_thunk_test.cpp)
    target_include_directories(open_citadel_win32_gl_thunk_test PRIVATE
        ../../loader
        ../../src
        ../../thunks
        ../../thunks/libc
    )
    target_compile_features(open_citadel_win32_gl_thunk_test PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(open_citadel_win32_gl_thunk_test PRIVATE
            /wd4081 /wd4141 /wd5280 /wd4068
        )
    endif()

    add_test(NAME win32-stdcall-gl-bridge COMMAND open_citadel_win32_gl_thunk_test)
endif()
