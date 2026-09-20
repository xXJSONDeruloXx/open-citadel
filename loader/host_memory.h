#ifndef OPEN_CITADEL_HOST_MEMORY_H
#define OPEN_CITADEL_HOST_MEMORY_H

#include <stddef.h>

enum host_memory_access {
    HOST_MEMORY_READ = 1u << 0,
    HOST_MEMORY_WRITE = 1u << 1,
    HOST_MEMORY_EXECUTE = 1u << 2,
};

/* Allocate anonymous memory, optionally at an exact address, with the given
 * initial access. A null address lets the host choose a suitable location. */
void *host_memory_allocate(void *address, size_t size, unsigned int access);

/* Return non-zero on success. */
int host_memory_protect(void *address, size_t size, unsigned int access);
int host_memory_release(void *address, size_t size);

/* Make generated or patched instructions visible to the processor. */
void host_memory_flush_instruction_cache(void *address, size_t size);

#endif
