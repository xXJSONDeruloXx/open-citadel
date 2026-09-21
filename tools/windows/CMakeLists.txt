cmake_minimum_required(VERSION 3.21)
project(OpenCitadelWindows VERSION 0.1.0 LANGUAGES CXX)

if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

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

add_executable(open_citadel_window_geometry_test window_geometry_test.cpp)
target_include_directories(open_citadel_window_geometry_test PRIVATE
    ../../open_citadel
)
target_compile_features(open_citadel_window_geometry_test PRIVATE cxx_std_17)
add_test(NAME window-geometry COMMAND open_citadel_window_geometry_test)

add_executable(open_citadel_mouse_look_test mouse_look_test.cpp)
target_include_directories(open_citadel_mouse_look_test PRIVATE
    ../../open_citadel
)
target_compile_features(open_citadel_mouse_look_test PRIVATE cxx_std_17)
add_test(NAME native-mouse-look COMMAND open_citadel_mouse_look_test)

add_executable(open_citadel_frame_pacing_test frame_pacing_test.cpp)
target_include_directories(open_citadel_frame_pacing_test PRIVATE
    ../../open_citadel
)
target_compile_features(open_citadel_frame_pacing_test PRIVATE cxx_std_17)
add_test(NAME frame-pacing COMMAND open_citadel_frame_pacing_test)

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
    add_executable(open_citadel_gamepad_input_test gamepad_input_test.cpp)
    target_include_directories(open_citadel_gamepad_input_test PRIVATE
        ../../open_citadel
    )
    target_compile_features(open_citadel_gamepad_input_test PRIVATE cxx_std_17)
    target_link_libraries(open_citadel_gamepad_input_test PRIVATE
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    )
    if(TARGET SDL2::SDL2)
        add_custom_command(TARGET open_citadel_gamepad_input_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL2::SDL2>"
                "$<TARGET_FILE_DIR:open_citadel_gamepad_input_test>"
            VERBATIM
        )
    endif()
    add_test(NAME gamepad-input-mapping COMMAND open_citadel_gamepad_input_test)

    find_package(unofficial-angle CONFIG REQUIRED)
    find_package(mpg123 CONFIG REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(PThreads4W CONFIG REQUIRED)
    find_path(DIRENT_INCLUDE_DIR NAMES dirent.h REQUIRED)

    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Declare(open_citadel_imgui
        URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
        URL_HASH SHA256=21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
    )
    FetchContent_GetProperties(open_citadel_imgui)
    if(NOT open_citadel_imgui_POPULATED)
        FetchContent_Populate(open_citadel_imgui)
    endif()

    add_library(open_citadel_imgui STATIC
        ${open_citadel_imgui_SOURCE_DIR}/imgui.cpp
        ${open_citadel_imgui_SOURCE_DIR}/imgui_draw.cpp
        ${open_citadel_imgui_SOURCE_DIR}/imgui_tables.cpp
        ${open_citadel_imgui_SOURCE_DIR}/imgui_widgets.cpp
        ../../open_citadel/imgui_impl_opengl3_glad.cpp
        ${open_citadel_imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    )
    target_include_directories(open_citadel_imgui PUBLIC
        ${open_citadel_imgui_SOURCE_DIR}
    )
    target_include_directories(open_citadel_imgui PRIVATE
        ../../thunks
    )
    target_compile_features(open_citadel_imgui PUBLIC cxx_std_17)
    target_compile_definitions(open_citadel_imgui PRIVATE
        IMGUI_IMPL_OPENGL_ES2
        IMGUI_IMPL_OPENGL_LOADER_CUSTOM
    )
    target_link_libraries(open_citadel_imgui PRIVATE
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
        unofficial::angle::libGLESv2
    )

    add_executable(open-citadel
        ../../loader/host_memory.cpp
        ../../loader/so_util.cpp
        ../../loader/so_util_x86.cpp
        ../../open_citadel/crash_win32.cpp
        ../../open_citadel/desktop_overlay.cpp
        ../../open_citadel/gles2_probe.cpp
        ../../open_citadel/io_util.cpp
        ../../open_citadel/main.cpp
        ../../open_citadel/opensles_compat.cpp
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
        ../../jni/classes/citadel_audio.cpp
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
        MPG123::libmpg123
        ZLIB::ZLIB
        PThreads4W::PThreads4W
        open_citadel_imgui
        ws2_32
    )

    foreach(runtime_target IN ITEMS
            SDL2::SDL2
            unofficial::angle::libEGL
            unofficial::angle::libGLESv2
            MPG123::libmpg123
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

    install(TARGETS open-citadel
        RUNTIME_DEPENDENCY_SET open_citadel_runtime
        RUNTIME DESTINATION .
    )
    install(RUNTIME_DEPENDENCY_SET open_citadel_runtime
        PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
        POST_EXCLUDE_REGEXES
            ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\].*"
        RUNTIME DESTINATION .
    )
    foreach(angle_target IN ITEMS
            unofficial::angle::libEGL
            unofficial::angle::libGLESv2)
        install(FILES "$<TARGET_FILE:${angle_target}>" DESTINATION .)
    endforeach()
    install(FILES
        "${CMAKE_SOURCE_DIR}/../../WINDOWS_QUICKSTART.md"
        "${CMAKE_SOURCE_DIR}/../../LICENSE"
        "${CMAKE_SOURCE_DIR}/../../NOTICE.md"
        DESTINATION .
    )
    install(FILES
        "${CMAKE_SOURCE_DIR}/../../README.md"
        "${CMAKE_SOURCE_DIR}/../../OPEN_CITADEL.md"
        DESTINATION docs
    )
    install(FILES
        "${CMAKE_SOURCE_DIR}/../../tools/open_citadel_import.py"
        DESTINATION .
    )
    install(FILES "${open_citadel_imgui_SOURCE_DIR}/LICENSE.txt"
        DESTINATION licenses/imgui
    )

    set(_open_citadel_vcpkg_share
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
    foreach(port IN ITEMS angle mpg123 pthreads sdl2 zlib)
        set(port_copyright "${_open_citadel_vcpkg_share}/${port}/copyright")
        if(NOT EXISTS "${port_copyright}")
            message(FATAL_ERROR
                "Missing ${port} copyright notice: ${port_copyright}")
        endif()
        install(FILES "${port_copyright}"
            DESTINATION "licenses/${port}"
        )
    endforeach()

    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    include(InstallRequiredSystemLibraries)

    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_NAME "OpenCitadel-Windows")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME
        "OpenCitadel-Windows-${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Native Windows host for Epic Citadel 1.07")
    set(CPACK_PACKAGE_VENDOR "Open Citadel")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    include(CPack)

    add_executable(open_citadel_audio_test
        audio_test.cpp
        ../../jni/classes/citadel_audio.cpp
        ../../src/trace.cpp
    )
    target_include_directories(open_citadel_audio_test PRIVATE
        ../../jni/classes
        ../../src
    )
    target_compile_features(open_citadel_audio_test PRIVATE cxx_std_20)
    target_compile_definitions(open_citadel_audio_test PRIVATE
        _CRT_SECURE_NO_WARNINGS
        SDL_MAIN_HANDLED
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(open_citadel_audio_test PRIVATE
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
        MPG123::libmpg123
    )
    foreach(runtime_target IN ITEMS SDL2::SDL2 MPG123::libmpg123)
        if(TARGET ${runtime_target})
            add_custom_command(TARGET open_citadel_audio_test POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:${runtime_target}>"
                    "$<TARGET_FILE_DIR:open_citadel_audio_test>"
                VERBATIM
            )
        endif()
    endforeach()
    add_test(NAME audio-mixer COMMAND open_citadel_audio_test)

    add_executable(open_citadel_opensles_test
        opensles_test.cpp
        ../../open_citadel/opensles_compat.cpp
        ../../jni/classes/citadel_audio.cpp
        ../../src/trace.cpp
    )
    target_include_directories(open_citadel_opensles_test PRIVATE
        ../../jni/classes
        ../../loader
        ../../open_citadel
        ../../src
        ../../thunks
    )
    target_compile_features(open_citadel_opensles_test PRIVATE cxx_std_20)
    target_compile_definitions(open_citadel_opensles_test PRIVATE
        _CRT_SECURE_NO_WARNINGS
        SDL_MAIN_HANDLED
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(open_citadel_opensles_test PRIVATE
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
        MPG123::libmpg123
    )
    foreach(runtime_target IN ITEMS SDL2::SDL2 MPG123::libmpg123)
        if(TARGET ${runtime_target})
            add_custom_command(TARGET open_citadel_opensles_test POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:${runtime_target}>"
                    "$<TARGET_FILE_DIR:open_citadel_opensles_test>"
                VERBATIM
            )
        endif()
    endforeach()
    add_test(NAME opensles-pcm-queue COMMAND open_citadel_opensles_test)
endif()
