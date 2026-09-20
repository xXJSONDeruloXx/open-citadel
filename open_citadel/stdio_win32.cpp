/* Bionic FILE adapters for the 32-bit Windows C runtime. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "so_util.h"
#include "thunk_gen.h"
#include "thunks/libc/bionic_file.h"

BIONIC_FILE __sF_fake[3]{};
BIONIC_FILE *stdin_impl = &__sF_fake[0];
BIONIC_FILE *stdout_impl = &__sF_fake[1];
BIONIC_FILE *stderr_impl = &__sF_fake[2];

namespace {

struct StandardFileSetup {
    StandardFileSetup()
    {
        __sF_fake[0]._cookie = stdin;
        __sF_fake[1]._cookie = stdout;
        __sF_fake[2]._cookie = stderr;
        __sF_fake[0]._file = 0;
        __sF_fake[1]._file = 1;
        __sF_fake[2]._file = 2;
    }
};

StandardFileSetup g_standard_file_setup;

FILE *host_file(BIONIC_FILE *file)
{
    return file ? static_cast<FILE *>(file->_cookie) : nullptr;
}

bool is_standard_file(BIONIC_FILE *file)
{
    return file >= &__sF_fake[0] && file <= &__sF_fake[2];
}

void update_bionic_flags(BIONIC_FILE *file, FILE *host)
{
    if (!file || !host)
        return;
    file->_flags = (short)((feof(host) << 5) | (ferror(host) << 6));
}

} // namespace

/* Kept C++-linked to match the existing bionic_fopen declaration in
 * src/symtab_io.cpp. */
ABI_ATTR void *fopen_impl(const char *path, const char *mode)
{
    if (!path || !mode)
        return nullptr;
    char host_mode[16]{};
    const size_t mode_length = strlen(mode);
    if (mode_length + 2 > sizeof(host_mode)) {
        errno = EINVAL;
        return nullptr;
    }
    memcpy(host_mode, mode, mode_length + 1);
    if (!strchr(host_mode, 'b'))
        strcat_s(host_mode, sizeof(host_mode), "b");

    FILE *host = fopen(path, host_mode);
    if (!host)
        return nullptr;
    BIONIC_FILE *file = (BIONIC_FILE *)calloc(1, sizeof(*file));
    if (!file) {
        fclose(host);
        return nullptr;
    }
    file->_cookie = host;
    file->_file = -1;
    return (void *)file;
}

ABI_ATTR int bionic_fclose(BIONIC_FILE *file)
{
    FILE *host = host_file(file);
    if (!host)
        return EOF;
    const int result = fclose(host);
    file->_cookie = nullptr;
    if (!is_standard_file(file))
        free(file);
    return result;
}

ABI_ATTR int bionic_fflush(BIONIC_FILE *file)
{
    FILE *host = file ? host_file(file) : nullptr;
    const int result = fflush(host);
    update_bionic_flags(file, host);
    return result;
}

ABI_ATTR size_t bionic_fread(void *buffer, size_t size, size_t count,
                             BIONIC_FILE *file)
{
    FILE *host = host_file(file);
    if (!host)
        return 0;
    const size_t result = fread(buffer, size, count, host);
    update_bionic_flags(file, host);
    return result;
}

ABI_ATTR size_t bionic_fwrite(const void *buffer, size_t size, size_t count,
                              BIONIC_FILE *file)
{
    FILE *host = host_file(file);
    if (!host)
        return 0;
    const size_t result = fwrite(buffer, size, count, host);
    update_bionic_flags(file, host);
    return result;
}

ABI_ATTR char *bionic_fgets(char *buffer, int size, BIONIC_FILE *file)
{
    FILE *host = host_file(file);
    if (!host)
        return nullptr;
    char *result = fgets(buffer, size, host);
    update_bionic_flags(file, host);
    return result;
}

ABI_ATTR int bionic_fseek(BIONIC_FILE *file, long offset, int origin)
{
    FILE *host = host_file(file);
    if (!host)
        return -1;
    const int result = fseek(host, offset, origin);
    update_bionic_flags(file, host);
    return result;
}

ABI_ATTR long bionic_ftell(BIONIC_FILE *file)
{
    FILE *host = host_file(file);
    return host ? ftell(host) : -1L;
}

ABI_ATTR void bionic_rewind(BIONIC_FILE *file)
{
    FILE *host = host_file(file);
    if (!host)
        return;
    rewind(host);
    update_bionic_flags(file, host);
}

ABI_ATTR int bionic_fprintf(BIONIC_FILE *file, const char *format, ...)
{
    FILE *host = host_file(file);
    if (!host)
        return -1;
    va_list args;
    va_start(args, format);
    const int result = vfprintf(host, format, args);
    va_end(args);
    update_bionic_flags(file, host);
    return result;
}

ABI_ATTR int bionic_vfprintf(BIONIC_FILE *file, const char *format,
                             va_list args)
{
    FILE *host = host_file(file);
    if (!host)
        return -1;
    const int result = vfprintf(host, format, args);
    update_bionic_flags(file, host);
    return result;
}

ABI_ATTR int bionic_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = vprintf(format, args);
    va_end(args);
    return result;
}

ABI_ATTR int bionic_vprintf(const char *format, va_list args)
{
    return vprintf(format, args);
}

ABI_ATTR int bionic_sprintf(char *buffer, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = vsprintf(buffer, format, args);
    va_end(args);
    return result;
}

ABI_ATTR int bionic_snprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

ABI_ATTR int bionic_puts(const char *text)
{
    return puts(text);
}

ABI_ATTR int bionic_vsnprintf(char *buffer, size_t size,
                              const char *format, va_list args)
{
    return vsnprintf(buffer, size, format, args);
}

ABI_ATTR int bionic_vsprintf(char *buffer, const char *format, va_list args)
{
    return vsprintf(buffer, format, args);
}

DynLibFunction symtable_stdio_win32[] = {
    NO_THUNK("fclose", (uintptr_t)&bionic_fclose),
    NO_THUNK("fflush", (uintptr_t)&bionic_fflush),
    NO_THUNK("fgets", (uintptr_t)&bionic_fgets),
    NO_THUNK("fread", (uintptr_t)&bionic_fread),
    NO_THUNK("fprintf", (uintptr_t)&bionic_fprintf),
    NO_THUNK("fseek", (uintptr_t)&bionic_fseek),
    NO_THUNK("ftell", (uintptr_t)&bionic_ftell),
    NO_THUNK("rewind", (uintptr_t)&bionic_rewind),
    NO_THUNK("fwrite", (uintptr_t)&bionic_fwrite),
    NO_THUNK("printf", (uintptr_t)&bionic_printf),
    NO_THUNK("puts", (uintptr_t)&bionic_puts),
    NO_THUNK("snprintf", (uintptr_t)&bionic_snprintf),
    NO_THUNK("sprintf", (uintptr_t)&bionic_sprintf),
    NO_THUNK("vfprintf", (uintptr_t)&bionic_vfprintf),
    NO_THUNK("vprintf", (uintptr_t)&bionic_vprintf),
    NO_THUNK("vsnprintf", (uintptr_t)&bionic_vsnprintf),
    NO_THUNK("vsprintf", (uintptr_t)&bionic_vsprintf),
    {nullptr, 0},
};
