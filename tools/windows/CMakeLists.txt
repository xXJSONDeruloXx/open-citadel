cmake_minimum_required(VERSION 3.20)
project(OpenCitadelWindows LANGUAGES CXX)

option(OPEN_CITADEL_BUILD_APP "Build the native Win32 Epic Citadel host" OFF)

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

add_executable(open_citadel_keyboard_controls_test keyboard_controls_test.cpp)
target_include_directories(open_citadel_keyboard_controls_test PRIVATE
    ../../open_citadel
)
target_compile_features(open_citadel_keyboard_controls_test PRIVATE cxx_std_17)
add_test(NAME keyboard-controls COMMAND open_citadel_keyboard_controls_test)

add_executable(open_citadel_settings_test settings_test.cpp)
target_include_directories(open_citadel_settings_test PRIVATE
    ../../open_citadel
)
target_compile_features(open_citadel_settings_test PRIVATE cxx_std_17)
add_test(NAME user-settings COMMAND open_citadel_settings_test)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(open_citadel_android_input_abi_test
        android_input_abi_test.cpp
    )
    target_include_directories(open_citadel_android_input_abi_test PRIVATE
        ../../open_citadel
    )
    target_compile_features(open_citadel_android_input_abi_test PRIVATE cxx_std_17)
    add_test(NAME android-input-abi COMMAND open_citadel_android_input_abi_test)

    add_executable(open_citadel_guest_wchar32_test guest_wchar32_test.cpp)
    target_include_directories(open_citadel_guest_wchar32_test PRIVATE
        ../../open_citadel
    )
    target_compile_features(open_citadel_guest_wchar32_test PRIVATE cxx_std_17)
    add_test(NAME guest-wchar32 COMMAND open_citadel_guest_wchar32_test)

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

if(OPEN_CITADEL_BUILD_APP)
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "The Epic Citadel x86 ELF host requires 32-bit Windows")
    endif()

    find_package(SDL2 CONFIG REQUIRED)
    find_package(unofficial-angle CONFIG REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(PThreads4W CONFIG REQUIRED)
    find_path(DIRENT_INCLUDE_DIR NAMES dirent.h REQUIRED)

    add_executable(open-citadel
        ../../loader/host_memory.cpp
        ../../loader/so_util.cpp
        ../../loader/so_util_x86.cpp
        ../../open_citadel/crash_win32.cpp
        ../../open_citadel/gles2_probe.cpp
        ../../open_citadel/io_util.cpp
        ../../open_citadel/main.cpp
        ../../open_citadel/stdio_win32.cpp
        ../../open_citadel/symtab.cpp
        ../../open_citadel/symtab_libc_win32.cpp
        ../../open_citadel/symtab_libm_win32.cpp
        ../../open_citadel/symtab_runtime.cpp
        ../../open_citadel/symtab_zlib.cpp
        ../../android/asset_manager.cpp
        ../../android/egl_shim.cpp
        ../../android/log.cpp
        ../../jni/class_registry.cpp
        ../../jni/jni.cpp
        ../../jni/classes/android_input_constants.cpp
        ../../jni/classes/bytebuffer.cpp
        ../../jni/classes/java_io_FileDescriptor.cpp
        ../../jni/classes/lang_ClassLoader.cpp
        ../../jni/classes/os_Build.cpp
        ../../jni/classes/string.cpp
        ../../jni/classes/ue3_java_app.cpp
        ../../src/gl_diag.cpp
        ../../src/atc_decompress.cpp
        ../../src/symtab_bionic.cpp
        ../../src/symtab_io.cpp
        ../../src/symtab_pthread.cpp
        ../../src/symtab_sem.cpp
        ../../src/symtab_stat.cpp
        ../../src/symtab_time.cpp
        ../../src/symtab_unwind.cpp
        ../../src/trace.cpp
        ../../thunks/khronos/gles2.cpp
        ../../thunks/libc/ctype.cpp
    )
    target_include_directories(open-citadel PRIVATE
        ../..
        ../../open_citadel
        ../../loader
        ../../src
        ../../thunks
        ../../thunks/libc
        ../../thunks/khronos
        ../../android
        ../../jni
        ../../jni/classes
        ${DIRENT_INCLUDE_DIR}
    )
    target_compile_features(open-citadel PRIVATE cxx_std_20)
    target_compile_definitions(open-citadel PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(MSVC)
        target_compile_options(open-citadel PRIVATE
            /EHsc /wd4081 /wd4141 /wd5280 /wd4068
        )
    endif()

    target_link_libraries(open-citadel PRIVATE
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
        unofficial::angle::libEGL
        unofficial::angle::libGLESv2
        ZLIB::ZLIB
        PThreads4W::PThreads4W
        ws2_32
    )

    foreach(runtime_target IN ITEMS
            SDL2::SDL2
            unofficial::angle::libEGL
            unofficial::angle::libGLESv2
            ZLIB::ZLIB
            PThreads4W::PThreads4W)
        if(TARGET ${runtime_target})
            add_custom_command(TARGET open-citadel POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:${runtime_target}>"
                    "$<TARGET_FILE_DIR:open-citadel>"
                VERBATIM
            )
        endif()
    endforeach()
endif()
