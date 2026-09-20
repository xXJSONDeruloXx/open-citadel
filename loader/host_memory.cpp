#include "host_memory.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static DWORD native_protection(unsigned int access)
{
    const bool read = (access & HOST_MEMORY_READ) != 0;
    const bool write = (access & HOST_MEMORY_WRITE) != 0;
    const bool execute = (access & HOST_MEMORY_EXECUTE) != 0;

    if (execute) {
        if (write)
            return PAGE_EXECUTE_READWRITE;
        return read ? PAGE_EXECUTE_READ : PAGE_EXECUTE;
    }
    if (write)
        return PAGE_READWRITE;
    return read ? PAGE_READONLY : PAGE_NOACCESS;
}

void *host_memory_allocate(void *address, size_t size, unsigned int access)
{
    if (!size)
        return nullptr;
    void *memory = VirtualAlloc(address, size, MEM_RESERVE | MEM_COMMIT,
                                native_protection(access));
    if (address && memory != address) {
        if (memory)
            VirtualFree(memory, 0, MEM_RELEASE);
        return nullptr;
    }
    return memory;
}

int host_memory_protect(void *address, size_t size, unsigned int access)
{
    DWORD previous = 0;
    return address && size &&
           VirtualProtect(address, size, native_protection(access), &previous)
        ? 1 : 0;
}

int host_memory_release(void *address, size_t size)
{
    (void)size;
    return address && VirtualFree(address, 0, MEM_RELEASE) ? 1 : 0;
}

void host_memory_flush_instruction_cache(void *address, size_t size)
{
    if (address && size)
        FlushInstructionCache(GetCurrentProcess(), address, size);
}

#else

#include <sys/mman.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

static int native_protection(unsigned int access)
{
    int protection = PROT_NONE;
    if (access & HOST_MEMORY_READ)
        protection |= PROT_READ;
    if (access & HOST_MEMORY_WRITE)
        protection |= PROT_WRITE;
    if (access & HOST_MEMORY_EXECUTE)
        protection |= PROT_EXEC;
    return protection;
}

void *host_memory_allocate(void *address, size_t size, unsigned int access)
{
    if (!size)
        return nullptr;

    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_POPULATE
    flags |= MAP_POPULATE;
#endif
    if (address)
        flags |= MAP_FIXED;

    void *memory = mmap(address, size, native_protection(access), flags, -1, 0);
    return memory == MAP_FAILED ? nullptr : memory;
}

int host_memory_protect(void *address, size_t size, unsigned int access)
{
    return address && size &&
           mprotect(address, size, native_protection(access)) == 0;
}

int host_memory_release(void *address, size_t size)
{
    return address && size && munmap(address, size) == 0;
}

void host_memory_flush_instruction_cache(void *address, size_t size)
{
    if (address && size)
        __builtin___clear_cache((char *)address, (char *)address + size);
}

#endif
