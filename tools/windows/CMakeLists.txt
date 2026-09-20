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
