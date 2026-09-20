#include "host_memory.h"

#include <stdio.h>

int main()
{
    constexpr size_t size = 4096;
    auto *memory = static_cast<volatile unsigned char *>(host_memory_allocate(
        nullptr, size, HOST_MEMORY_READ | HOST_MEMORY_WRITE));
    if (!memory) {
        fprintf(stderr, "host_memory_allocate failed\n");
        return 1;
    }

    memory[0] = 0x5a;
    memory[size - 1] = 0xa5;
    if (memory[0] != 0x5a || memory[size - 1] != 0xa5) {
        fprintf(stderr, "allocated memory did not preserve writes\n");
        host_memory_release(const_cast<unsigned char *>(memory), size);
        return 1;
    }

    if (!host_memory_protect(const_cast<unsigned char *>(memory), size,
                            HOST_MEMORY_READ | HOST_MEMORY_EXECUTE)) {
        fprintf(stderr, "host_memory_protect(RX) failed\n");
        host_memory_release(const_cast<unsigned char *>(memory), size);
        return 1;
    }
    host_memory_flush_instruction_cache(const_cast<unsigned char *>(memory),
                                        size);

    if (!host_memory_protect(const_cast<unsigned char *>(memory), size,
                            HOST_MEMORY_READ | HOST_MEMORY_WRITE)) {
        fprintf(stderr, "host_memory_protect(RW) failed\n");
        host_memory_release(const_cast<unsigned char *>(memory), size);
        return 1;
    }
    memory[0] = 0xc3;
    if (memory[0] != 0xc3) {
        fprintf(stderr, "memory was not writable after protection change\n");
        host_memory_release(const_cast<unsigned char *>(memory), size);
        return 1;
    }

    if (!host_memory_release(const_cast<unsigned char *>(memory), size)) {
        fprintf(stderr, "host_memory_release failed\n");
        return 1;
    }

    puts("host memory allocation/protection/cache/release passed");
    return 0;
}
