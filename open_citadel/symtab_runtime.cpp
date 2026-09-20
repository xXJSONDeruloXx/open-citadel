#include <new>
#include <stddef.h>
#include <stdint.h>

#include "so_util.h"
#include "thunk_gen.h"

/*
 * Small GCC/libstdc++ ABI surface imported by Epic Citadel's old NDK build.
 * The allocations are ABI-identical in a same-word-size host process.
 */
static void *guest_new_array(size_t size)
{
    return ::operator new[](size);
}

static void guest_delete(void *ptr)
{
    ::operator delete(ptr);
}

static void guest_delete_array(void *ptr)
{
    ::operator delete[](ptr);
}

extern "C" void open_citadel_Jv_RegisterClasses(void *) {}

#if !defined(_WIN32)
extern "C" void __register_frame_info(const void *, void *)
    __attribute__((weak));
extern "C" void *__deregister_frame_info(const void *)
    __attribute__((weak));
extern "C" void __register_frame_info_bases(const void *, void *, void *, void *)
    __attribute__((weak));
extern "C" void *__deregister_frame_info_bases(const void *)
    __attribute__((weak));
#endif

static void frame_register_arm(const void *begin, void *object)
{
#if !defined(_WIN32)
    if (__register_frame_info)
        __register_frame_info(begin, object);
#else
    (void)begin;
    (void)object;
#endif
}

static void *frame_deregister_arm(const void *begin)
{
#if !defined(_WIN32)
    return __deregister_frame_info ? __deregister_frame_info(begin) : nullptr;
#else
    (void)begin;
    return nullptr;
#endif
}

static void frame_register_x86(const void *begin, void *object, void *tbase,
                               void *dbase)
{
#if !defined(_WIN32)
    if (__register_frame_info_bases)
        __register_frame_info_bases(begin, object, tbase, dbase);
#else
    (void)begin;
    (void)object;
    (void)tbase;
    (void)dbase;
#endif
}

static void *frame_deregister_x86(const void *begin)
{
#if !defined(_WIN32)
    return __deregister_frame_info_bases
        ? __deregister_frame_info_bases(begin) : nullptr;
#else
    (void)begin;
    return nullptr;
#endif
}

DynLibFunction symtable_open_citadel_runtime[] = {
    NO_THUNK("_Znaj", (uintptr_t)&guest_new_array),
    NO_THUNK("_ZdlPv", (uintptr_t)&guest_delete),
    NO_THUNK("_ZdaPv", (uintptr_t)&guest_delete_array),
    NO_THUNK("_Jv_RegisterClasses", (uintptr_t)&open_citadel_Jv_RegisterClasses),
#if defined(__i386__) || defined(_M_IX86)
    NO_THUNK("__register_frame_info_bases", (uintptr_t)&frame_register_x86),
    NO_THUNK("__deregister_frame_info_bases", (uintptr_t)&frame_deregister_x86),
#elif defined(__arm__)
    NO_THUNK("__register_frame_info", (uintptr_t)&frame_register_arm),
    NO_THUNK("__deregister_frame_info", (uintptr_t)&frame_deregister_arm),
#endif
    {nullptr, 0},
};
