/*
 * Win32 x86 libc imports used by Epic Citadel 1.07.
 *
 * The guest exposes Bionic/POSIX symbols, not the UCRT's underscored names or
 * Winsock's stdcall entry points. Keep those translations in this game profile
 * so shared Linux tables and other ports retain their existing behavior.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <intrin.h>

#include <dirent.h>
#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <share.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cmath>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "fix_path.h"
#include "so_util.h"
#include "thunk_gen.h"
#include "thunks/libc/bionic_file.h"
#include "wchar32_win32.h"

static_assert(sizeof(void *) == 4, "Epic Citadel's x86 ELF must run in Win32");
static_assert(sizeof(SOCKET) == sizeof(int), "WinSock handles must fit guest fd");

so_module *katamari_module(void);

namespace {

std::mutex g_socket_lock;
std::unordered_set<SOCKET> g_socket_handles;
std::once_flag g_winsock_once;
int g_winsock_startup_result = WSASYSNOTREADY;

void start_winsock()
{
    std::call_once(g_winsock_once, [] {
        WSADATA data{};
        g_winsock_startup_result = WSAStartup(MAKEWORD(2, 2), &data);
    });
}

void set_winsock_errno()
{
    switch (WSAGetLastError()) {
    case WSAEINTR:        errno = EINTR; break;
    case WSAEACCES:       errno = EACCES; break;
    case WSAEFAULT:       errno = EFAULT; break;
    case WSAEINVAL:       errno = EINVAL; break;
    case WSAEMFILE:       errno = EMFILE; break;
    case WSAEWOULDBLOCK:  errno = EWOULDBLOCK; break;
    case WSAEINPROGRESS:  errno = EINPROGRESS; break;
    case WSAEALREADY:     errno = EALREADY; break;
    case WSAENOTSOCK:     errno = ENOTSOCK; break;
    case WSAEDESTADDRREQ: errno = EDESTADDRREQ; break;
    case WSAEMSGSIZE:     errno = EMSGSIZE; break;
    case WSAEPROTOTYPE:   errno = EPROTOTYPE; break;
    case WSAENOPROTOOPT:  errno = ENOPROTOOPT; break;
    case WSAEPROTONOSUPPORT: errno = EPROTONOSUPPORT; break;
    case WSAESOCKTNOSUPPORT: errno = EPROTONOSUPPORT; break;
    case WSAEOPNOTSUPP:   errno = EOPNOTSUPP; break;
    case WSAEAFNOSUPPORT: errno = EAFNOSUPPORT; break;
    case WSAEADDRINUSE:   errno = EADDRINUSE; break;
    case WSAEADDRNOTAVAIL: errno = EADDRNOTAVAIL; break;
    case WSAENETDOWN:     errno = ENETDOWN; break;
    case WSAENETUNREACH:  errno = ENETUNREACH; break;
    case WSAENETRESET:    errno = ENETRESET; break;
    case WSAECONNABORTED: errno = ECONNABORTED; break;
    case WSAECONNRESET:   errno = ECONNRESET; break;
    case WSAENOBUFS:      errno = ENOBUFS; break;
    case WSAEISCONN:      errno = EISCONN; break;
    case WSAENOTCONN:     errno = ENOTCONN; break;
    case WSAETIMEDOUT:    errno = ETIMEDOUT; break;
    case WSAECONNREFUSED: errno = ECONNREFUSED; break;
    default:              errno = EIO; break;
    }
}

void set_errno_from_win32(DWORD error)
{
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND: errno = ENOENT; break;
    case ERROR_ACCESS_DENIED: errno = EACCES; break;
    case ERROR_INVALID_HANDLE: errno = EBADF; break;
    case ERROR_INVALID_PARAMETER: errno = EINVAL; break;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY: errno = ENOMEM; break;
    case ERROR_TOO_MANY_OPEN_FILES: errno = EMFILE; break;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS: errno = EEXIST; break;
    case ERROR_DISK_FULL: errno = ENOSPC; break;
    default: errno = EIO; break;
    }
}

int guest_socket(SOCKET socket_handle)
{
    if (socket_handle == INVALID_SOCKET || socket_handle > (SOCKET)INT32_MAX) {
        if (socket_handle != INVALID_SOCKET)
            closesocket(socket_handle);
        errno = EMFILE;
        return -1;
    }
    {
        std::lock_guard<std::mutex> lock(g_socket_lock);
        g_socket_handles.insert(socket_handle);
    }
    return (int)socket_handle;
}

bool is_guest_socket(int fd)
{
    std::lock_guard<std::mutex> lock(g_socket_lock);
    return g_socket_handles.find((SOCKET)(uint32_t)fd) !=
           g_socket_handles.end();
}

bool forget_guest_socket(int fd)
{
    std::lock_guard<std::mutex> lock(g_socket_lock);
    return g_socket_handles.erase((SOCKET)(uint32_t)fd) != 0;
}

int win_socket(int domain, int type, int protocol)
{
    start_winsock();
    if (g_winsock_startup_result != 0) {
        errno = ENETDOWN;
        return -1;
    }
    const SOCKET result = ::socket(domain, type, protocol);
    if (result == INVALID_SOCKET) {
        set_winsock_errno();
        return -1;
    }
    return guest_socket(result);
}

int win_accept(int fd, struct sockaddr *address, int *address_length)
{
    const SOCKET result = ::accept((SOCKET)(uint32_t)fd, address,
                                   address_length);
    if (result == INVALID_SOCKET) {
        set_winsock_errno();
        return -1;
    }
    return guest_socket(result);
}

int win_bind(int fd, const struct sockaddr *address, int address_length)
{
    const int result = ::bind((SOCKET)(uint32_t)fd, address, address_length);
    if (result == SOCKET_ERROR)
        set_winsock_errno();
    return result;
}

int win_connect(int fd, const struct sockaddr *address, int address_length)
{
    const int result = ::connect((SOCKET)(uint32_t)fd, address, address_length);
    if (result == SOCKET_ERROR)
        set_winsock_errno();
    return result;
}

int win_listen(int fd, int backlog)
{
    const int result = ::listen((SOCKET)(uint32_t)fd, backlog);
    if (result == SOCKET_ERROR)
        set_winsock_errno();
    return result;
}

int bounded_socket_count(uint32_t count)
{
    return (int)std::min<uint32_t>(count, (uint32_t)INT32_MAX);
}

int win_recv(int fd, void *buffer, uint32_t count, int flags)
{
    const int result = ::recv((SOCKET)(uint32_t)fd, (char *)buffer,
                              bounded_socket_count(count), flags);
    if (result == SOCKET_ERROR)
        set_winsock_errno();
    return result;
}

int win_send(int fd, const void *buffer, uint32_t count, int flags)
{
    const int result = ::send((SOCKET)(uint32_t)fd, (const char *)buffer,
                              bounded_socket_count(count), flags);
    if (result == SOCKET_ERROR)
        set_winsock_errno();
    return result;
}

int win_recvfrom(int fd, void *buffer, uint32_t count, int flags,
                 struct sockaddr *address, int *address_length)
{
    const int result = ::recvfrom((SOCKET)(uint32_t)fd, (char *)buffer,
                                  bounded_socket_count(count), flags, address,
                                  address_length);
    if (result == SOCKET_ERROR)
        set_winsock_errno();
    return result;
}

int win_sendto(int fd, const void *buffer, uint32_t count, int flags,
               const struct sockaddr *address, int address_length)
{
    const int result = ::sendto((SOCKET)(uint32_t)fd, (const char *)buffer,
                                bounded_socket_count(count), flags, address,
                                address_length);
    if (result == SOCKET_ERROR)
        set_winsock_errno();
    return result;
}

int win_getsockname(int fd, struct sockaddr *address, int *address_length)
{
    const int result = ::getsockname((SOCKET)(uint32_t)fd, address,
                                     address_length);
    if (result == SOCKET_ERROR)
        set_winsock_errno();
    return result;
}

int win_getpeername(int fd, struct sockaddr *address, int *address_length)
{
    const int result = ::getpeername((SOCKET)(uint32_t)fd, address,
                                     address_length);
    if (result == SOCKET_ERROR)
        set_winsock_errno();
    return result;
}

int win_getsockopt(int fd, int level, int option, void *value,
                   int *value_length)
{
    const int result = ::getsockopt((SOCKET)(uint32_t)fd, level, option,
                                    (char *)value, value_length);
    if (result == SOCKET_ERROR)
        set_winsock_errno();
    return result;
}

int win_setsockopt(int fd, int level, int option, const void *value,
                   int value_length)
{
    const int result = ::setsockopt((SOCKET)(uint32_t)fd, level, option,
                                    (const char *)value, value_length);
    if (result == SOCKET_ERROR)
        set_winsock_errno();
    return result;
}

int win_getaddrinfo(const char *node, const char *service,
                    const struct addrinfo *hints, struct addrinfo **result)
{
    start_winsock();
    if (g_winsock_startup_result != 0)
        return EAI_FAIL;
    return ::getaddrinfo(node, service, hints, result);
}

void win_freeaddrinfo(struct addrinfo *result)
{
    ::freeaddrinfo(result);
}

int win_gethostname(char *name, uint32_t length)
{
    start_winsock();
    if (g_winsock_startup_result != 0) {
        errno = ENETDOWN;
        return -1;
    }
    const int result = ::gethostname(name, bounded_socket_count(length));
    if (result == SOCKET_ERROR)
        set_winsock_errno();
    return result;
}

uint32_t win_inet_addr(const char *address)
{
    return (uint32_t)::inet_addr(address);
}

struct BionicFdSet {
    uint32_t bits[32];
};

struct SelectSet {
    BionicFdSet original{};
    fd_set native{};
    bool present = false;
};

void make_native_set(SelectSet *set, const BionicFdSet *guest, int nfds)
{
    if (!guest)
        return;
    set->present = true;
    memcpy(&set->original, guest, sizeof(set->original));
    FD_ZERO(&set->native);
    for (int fd = 0; fd < nfds && fd < 1024; ++fd) {
        if (!(set->original.bits[fd / 32] & (1u << (fd % 32))))
            continue;
        if (is_guest_socket(fd))
            FD_SET((SOCKET)(uint32_t)fd, &set->native);
    }
}

void copy_guest_set(SelectSet *set, BionicFdSet *guest, int nfds)
{
    if (!guest || !set->present)
        return;
    memset(guest, 0, sizeof(*guest));
    for (int fd = 0; fd < nfds && fd < 1024; ++fd) {
        if ((set->original.bits[fd / 32] & (1u << (fd % 32))) &&
            is_guest_socket(fd) &&
            FD_ISSET((SOCKET)(uint32_t)fd, &set->native))
            guest->bits[fd / 32] |= 1u << (fd % 32);
    }
}

int win_select(int nfds, BionicFdSet *read_set, BionicFdSet *write_set,
               BionicFdSet *except_set, struct timeval *timeout)
{
    start_winsock();
    if (g_winsock_startup_result != 0) {
        errno = ENETDOWN;
        return -1;
    }
    SelectSet read_native, write_native, except_native;
    const int count = std::max(nfds, 0);
    make_native_set(&read_native, read_set, count);
    make_native_set(&write_native, write_set, count);
    make_native_set(&except_native, except_set, count);
    const int result = ::select(0,
        read_set ? &read_native.native : nullptr,
        write_set ? &write_native.native : nullptr,
        except_set ? &except_native.native : nullptr, timeout);
    if (result == SOCKET_ERROR) {
        set_winsock_errno();
        return -1;
    }
    copy_guest_set(&read_native, read_set, count);
    copy_guest_set(&write_native, write_set, count);
    copy_guest_set(&except_native, except_set, count);
    return result;
}

int win_close(int fd)
{
    if (forget_guest_socket(fd)) {
        const int result = closesocket((SOCKET)(uint32_t)fd);
        if (result == SOCKET_ERROR)
            set_winsock_errno();
        return result;
    }
    return _close(fd);
}

int win_read(int fd, void *buffer, uint32_t count)
{
    if (is_guest_socket(fd))
        return win_recv(fd, buffer, count, 0);
    return _read(fd, buffer, count);
}

int win_write(int fd, const void *buffer, uint32_t count)
{
    if (is_guest_socket(fd))
        return win_send(fd, buffer, count, 0);
    return _write(fd, buffer, count);
}

int win_ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    va_start(args, request);
    void *argument = va_arg(args, void *);
    va_end(args);

    if (is_guest_socket(fd)) {
        long host_request = (long)request;
        if (request == 0x5421) /* Bionic FIONBIO */
            host_request = FIONBIO;
        else if (request == 0x541B) /* Bionic FIONREAD */
            host_request = FIONREAD;
        else {
            errno = EINVAL;
            return -1;
        }
        const int result = ioctlsocket((SOCKET)(uint32_t)fd, host_request,
                                      (u_long *)argument);
        if (result == SOCKET_ERROR)
            set_winsock_errno();
        return result;
    }

    if (request == 0x541B) { /* FIONREAD on a regular file */
        const __int64 end = _filelengthi64(fd);
        const __int64 current = _telli64(fd);
        if (end < 0 || current < 0)
            return -1;
        *(uint32_t *)argument = (uint32_t)std::min<__int64>(
            end - current, UINT32_MAX);
        return 0;
    }
    errno = ENOTTY;
    return -1;
}

int win_strcasecmp(const char *left, const char *right)
{
    return _stricmp(left, right);
}

int win_strncasecmp(const char *left, const char *right, size_t count)
{
    return _strnicmp(left, right, count);
}

void *win_memchr(const void *buffer, int value, size_t count)
{
    return const_cast<void *>(::memchr(buffer, value, count));
}

char *win_strchr(const char *text, int value)
{
    return const_cast<char *>(::strchr(text, value));
}

char *win_strrchr(const char *text, int value)
{
    return const_cast<char *>(::strrchr(text, value));
}

char *win_strstr(const char *text, const char *needle)
{
    return const_cast<char *>(::strstr(text, needle));
}

int win_getpid()
{
    return _getpid();
}

int win_access(const char *path, int mode)
{
    char buffer[PATH_MAX];
    const char *fixed = fix_path(path, buffer, sizeof(buffer));
    return _access(fixed, mode & ~1); /* Windows has no executable bit. */
}

int win_chdir(const char *path)
{
    char buffer[PATH_MAX];
    return _chdir(fix_path(path, buffer, sizeof(buffer)));
}

int win_chmod(const char *path, int mode)
{
    char buffer[PATH_MAX];
    return _chmod(fix_path(path, buffer, sizeof(buffer)), mode);
}

int win_rmdir(const char *path)
{
    char buffer[PATH_MAX];
    return _rmdir(fix_path(path, buffer, sizeof(buffer)));
}

struct BionicSysinfo {
    int32_t uptime;
    uint32_t loads[3];
    uint32_t totalram;
    uint32_t freeram;
    uint32_t sharedram;
    uint32_t bufferram;
    uint32_t totalswap;
    uint32_t freeswap;
    uint16_t procs;
    uint16_t pad;
    uint32_t totalhigh;
    uint32_t freehigh;
    uint32_t mem_unit;
    uint8_t reserved[8];
};
static_assert(sizeof(BionicSysinfo) == 64, "Bionic x86 sysinfo ABI");

int win_sysinfo(BionicSysinfo *result)
{
    if (!result) {
        errno = EFAULT;
        return -1;
    }
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (!GlobalMemoryStatusEx(&memory)) {
        errno = EIO;
        return -1;
    }
    memset(result, 0, sizeof(*result));
    result->mem_unit = 4096;
    result->totalram = (uint32_t)(memory.ullTotalPhys / result->mem_unit);
    result->freeram = (uint32_t)(memory.ullAvailPhys / result->mem_unit);
    return 0;
}

struct tm *win_gmtime_r(const int32_t *seconds, struct tm *result)
{
    if (!seconds || !result) {
        errno = EINVAL;
        return nullptr;
    }
    const __time32_t value = (__time32_t)*seconds;
    return _gmtime32_s(result, &value) == 0 ? result : nullptr;
}

struct tm *win_gmtime(const int32_t *seconds)
{
    static thread_local struct tm result;
    return win_gmtime_r(seconds, &result);
}

struct tm *win_localtime_r(const int32_t *seconds, struct tm *result)
{
    if (!seconds || !result) {
        errno = EINVAL;
        return nullptr;
    }
    const __time32_t value = (__time32_t)*seconds;
    return _localtime32_s(result, &value) == 0 ? result : nullptr;
}

int win_utimes(const char *path, const struct timeval times[2])
{
    if (!path) {
        errno = EFAULT;
        return -1;
    }
    char path_buffer[PATH_MAX];
    const char *fixed = fix_path(path, path_buffer, sizeof(path_buffer));
    HANDLE file = CreateFileA(fixed, FILE_WRITE_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        set_errno_from_win32(GetLastError());
        return -1;
    }

    FILETIME access_time{}, write_time{};
    if (times) {
        auto to_file_time = [](const timeval &value) {
            const int64_t ticks = 116444736000000000LL +
                (int64_t)value.tv_sec * 10000000LL +
                (int64_t)value.tv_usec * 10LL;
            ULARGE_INTEGER integer{};
            integer.QuadPart = ticks < 0 ? 0 : (uint64_t)ticks;
            FILETIME converted{};
            converted.dwLowDateTime = integer.LowPart;
            converted.dwHighDateTime = integer.HighPart;
            return converted;
        };
        access_time = to_file_time(times[0]);
        write_time = to_file_time(times[1]);
    } else {
        GetSystemTimeAsFileTime(&access_time);
        write_time = access_time;
    }
    const BOOL ok = SetFileTime(file, nullptr, &access_time, &write_time);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok) {
        set_errno_from_win32(error);
        return -1;
    }
    return 0;
}

int win_sched_yield()
{
    if (!SwitchToThread())
        Sleep(0);
    return 0;
}

int win_pause()
{
    Sleep(1);
    return -1;
}

unsigned int win_sleep(unsigned int seconds)
{
    const uint64_t milliseconds = (uint64_t)seconds * 1000;
    Sleep((DWORD)std::min<uint64_t>(milliseconds, MAXDWORD));
    return 0;
}

int win_usleep(uint32_t microseconds)
{
    const uint64_t milliseconds = ((uint64_t)microseconds + 999) / 1000;
    Sleep((DWORD)std::min<uint64_t>(milliseconds, MAXDWORD));
    return 0;
}

std::atomic<uint64_t> g_rand48_state{0x1234abcd330eULL};

void win_srand48(long seed)
{
    g_rand48_state.store((((uint64_t)(uint32_t)seed) << 16) | 0x330eULL,
                         std::memory_order_relaxed);
}

long win_lrand48()
{
    constexpr uint64_t mask = (1ULL << 48) - 1;
    uint64_t previous = g_rand48_state.load(std::memory_order_relaxed);
    uint64_t next;
    do {
        next = (previous * 0x5deece66dULL + 0xbULL) & mask;
    } while (!g_rand48_state.compare_exchange_weak(
        previous, next, std::memory_order_relaxed, std::memory_order_relaxed));
    return (long)(next >> 17);
}

struct CxaDestructor {
    void (*function)(void *);
    void *object;
    void *dso;
    bool called;
};

std::mutex g_cxa_lock;
std::vector<CxaDestructor> g_cxa_destructors;
bool g_cxa_atexit_registered = false;

void win_cxa_finalize(void *dso)
{
    for (;;) {
        CxaDestructor destructor{};
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(g_cxa_lock);
            for (auto it = g_cxa_destructors.rbegin();
                 it != g_cxa_destructors.rend(); ++it) {
                if (!it->called && (!dso || it->dso == dso)) {
                    it->called = true;
                    destructor = *it;
                    found = true;
                    break;
                }
            }
        }
        if (!found)
            return;
        destructor.function(destructor.object);
    }
}

void win_cxa_finalize_all()
{
    win_cxa_finalize(nullptr);
}

int win_cxa_atexit(void (*function)(void *), void *object, void *dso)
{
    if (!function)
        return -1;
    std::lock_guard<std::mutex> lock(g_cxa_lock);
    if (!g_cxa_atexit_registered) {
        if (atexit(win_cxa_finalize_all) != 0)
            return -1;
        g_cxa_atexit_registered = true;
    }
    try {
        g_cxa_destructors.push_back({function, object, dso, false});
    } catch (...) {
        return -1;
    }
    return 0;
}

int *win_errno()
{
    return _errno();
}

void report_guest_failure(const char *kind, uintptr_t caller)
{
    so_module *module = katamari_module();
    if (module && caller >= module->text_base &&
        caller < module->text_base + module->text_size)
        fprintf(stderr, "OpenCitadel: guest %s at +0x%08x\n", kind,
                (unsigned)(caller - module->text_base));
    else
        fprintf(stderr, "OpenCitadel: guest %s caller=%p\n", kind,
                (void *)caller);
    fflush(stderr);
}

__declspec(noinline) void win_abort()
{
    report_guest_failure("abort", (uintptr_t)_ReturnAddress());
    abort();
}

void win_stack_chk_fail()
{
    report_guest_failure("stack check failed", (uintptr_t)_ReturnAddress());
    abort();
}

uintptr_t g_bionic_stack_chk_guard = (uintptr_t)0x6f70656e;

void *win_dlopen(const char *name, int)
{
    so_module *module = katamari_module();
    if (!module)
        return nullptr;
    if (!name)
        return module;
    for (const char **library = so_builtin_libs; *library; ++library) {
        if (strcmp(name, *library) == 0)
            return module;
    }
    return nullptr;
}

void *win_dlsym(void *handle, const char *name)
{
    if (!name)
        return nullptr;
    so_module *module = handle ? (so_module *)handle : katamari_module();
    const uintptr_t result = so_resolve_link(module, name);
    return (void *)result;
}

} // namespace

extern BIONIC_FILE __sF_fake[3];
extern BIONIC_FILE *stdin_impl;
extern BIONIC_FILE *stdout_impl;
extern BIONIC_FILE *stderr_impl;
extern const char *_ctype_impl;
extern const short *_toupper_tab_impl;

DynLibFunction symtable_libc[] = {
    NO_THUNK("__cxa_atexit", (uintptr_t)&win_cxa_atexit),
    NO_THUNK("__cxa_finalize", (uintptr_t)&win_cxa_finalize),
    NO_THUNK("__errno", (uintptr_t)&win_errno),
    NO_THUNK("__stack_chk_fail", (uintptr_t)&win_stack_chk_fail),
    NO_THUNK("__stack_chk_guard", (uintptr_t)&g_bionic_stack_chk_guard),
    NO_THUNK("__sF", (uintptr_t)&__sF_fake),
    NO_THUNK("stdin", (uintptr_t)&stdin_impl),
    NO_THUNK("stdout", (uintptr_t)&stdout_impl),
    NO_THUNK("stderr", (uintptr_t)&stderr_impl),
    NO_THUNK("_stdin", (uintptr_t)&stdin_impl),
    NO_THUNK("_stdout", (uintptr_t)&stdout_impl),
    NO_THUNK("_stderr", (uintptr_t)&stderr_impl),
    NO_THUNK("_ctype_", (uintptr_t)&_ctype_impl),
    NO_THUNK("_toupper_tab_", (uintptr_t)&_toupper_tab_impl),

    NO_THUNK("abort", (uintptr_t)&win_abort),
    NO_THUNK("atoi", (uintptr_t)&atoi),
    NO_THUNK("access", (uintptr_t)&win_access),
    NO_THUNK("chdir", (uintptr_t)&win_chdir),
    NO_THUNK("chmod", (uintptr_t)&win_chmod),
    NO_THUNK("close", (uintptr_t)&win_close),
    NO_THUNK("closedir", (uintptr_t)&closedir),
    NO_THUNK("dlopen", (uintptr_t)&win_dlopen),
    NO_THUNK("dlsym", (uintptr_t)&win_dlsym),
    NO_THUNK("free", (uintptr_t)&free),
    NO_THUNK("freeaddrinfo", (uintptr_t)&win_freeaddrinfo),
    NO_THUNK("getaddrinfo", (uintptr_t)&win_getaddrinfo),
    NO_THUNK("gethostname", (uintptr_t)&win_gethostname),
    NO_THUNK("getpid", (uintptr_t)&win_getpid),
    NO_THUNK("gmtime", (uintptr_t)&win_gmtime),
    NO_THUNK("gmtime_r", (uintptr_t)&win_gmtime_r),
    NO_THUNK("localtime_r", (uintptr_t)&win_localtime_r),
    NO_THUNK("inet_addr", (uintptr_t)&win_inet_addr),
    NO_THUNK("isalpha", (uintptr_t)&isalpha),
    NO_THUNK("isspace", (uintptr_t)&isspace),
    NO_THUNK("iswdigit", (uintptr_t)&open_citadel::guest_iswdigit),
    NO_THUNK("iswspace", (uintptr_t)&open_citadel::guest_iswspace),
    NO_THUNK("isxdigit", (uintptr_t)&isxdigit),
    NO_THUNK("lrand48", (uintptr_t)&win_lrand48),
    NO_THUNK("malloc", (uintptr_t)&malloc),
    NO_THUNK("memchr", (uintptr_t)&win_memchr),
    NO_THUNK("memcmp", (uintptr_t)&memcmp),
    NO_THUNK("memcpy", (uintptr_t)&memcpy),
    NO_THUNK("memmove", (uintptr_t)&memmove),
    NO_THUNK("memset", (uintptr_t)&memset),
    NO_THUNK("pause", (uintptr_t)&win_pause),
    NO_THUNK("qsort", (uintptr_t)&qsort),
    NO_THUNK("read", (uintptr_t)&win_read),
    NO_THUNK("realloc", (uintptr_t)&realloc),
    NO_THUNK("rmdir", (uintptr_t)&win_rmdir),
    NO_THUNK("sched_yield", (uintptr_t)&win_sched_yield),
    NO_THUNK("sleep", (uintptr_t)&win_sleep),
    NO_THUNK("srand48", (uintptr_t)&win_srand48),
    NO_THUNK("strcasecmp", (uintptr_t)&win_strcasecmp),
    NO_THUNK("strcat", (uintptr_t)&strcat),
    NO_THUNK("strchr", (uintptr_t)&win_strchr),
    NO_THUNK("strcmp", (uintptr_t)&strcmp),
    NO_THUNK("strcpy", (uintptr_t)&strcpy),
    NO_THUNK("strcspn", (uintptr_t)&strcspn),
    NO_THUNK("strlen", (uintptr_t)&strlen),
    NO_THUNK("strncasecmp", (uintptr_t)&win_strncasecmp),
    NO_THUNK("strncmp", (uintptr_t)&strncmp),
    NO_THUNK("strncpy", (uintptr_t)&strncpy),
    NO_THUNK("strrchr", (uintptr_t)&win_strrchr),
    NO_THUNK("strspn", (uintptr_t)&strspn),
    NO_THUNK("strstr", (uintptr_t)&win_strstr),
    NO_THUNK("strtod", (uintptr_t)&strtod),
    NO_THUNK("strtok", (uintptr_t)&strtok),
    NO_THUNK("strtol", (uintptr_t)&strtol),
    NO_THUNK("strtoul", (uintptr_t)&strtoul),
    NO_THUNK("strtoull", (uintptr_t)&strtoull),
    NO_THUNK("sysinfo", (uintptr_t)&win_sysinfo),
    NO_THUNK("utimes", (uintptr_t)&win_utimes),
    NO_THUNK("usleep", (uintptr_t)&win_usleep),
    NO_THUNK("vsnprintf", (uintptr_t)&vsnprintf),
    NO_THUNK("vsprintf", (uintptr_t)&vsprintf),
    NO_THUNK("wcscat", (uintptr_t)&open_citadel::guest_wcscat),
    NO_THUNK("wcschr", (uintptr_t)&open_citadel::guest_wcschr),
    NO_THUNK("wcscmp", (uintptr_t)&open_citadel::guest_wcscmp),
    NO_THUNK("wcscoll", (uintptr_t)&open_citadel::guest_wcscoll),
    NO_THUNK("wcscpy", (uintptr_t)&open_citadel::guest_wcscpy),
    NO_THUNK("wcslen", (uintptr_t)&open_citadel::guest_wcslen),
    NO_THUNK("wcsncmp", (uintptr_t)&open_citadel::guest_wcsncmp),
    NO_THUNK("wcsncpy", (uintptr_t)&open_citadel::guest_wcsncpy),
    NO_THUNK("wcsrchr", (uintptr_t)&open_citadel::guest_wcsrchr),
    NO_THUNK("wcsstr", (uintptr_t)&open_citadel::guest_wcsstr),
    NO_THUNK("write", (uintptr_t)&win_write),

    NO_THUNK("socket", (uintptr_t)&win_socket),
    NO_THUNK("accept", (uintptr_t)&win_accept),
    NO_THUNK("bind", (uintptr_t)&win_bind),
    NO_THUNK("connect", (uintptr_t)&win_connect),
    NO_THUNK("getpeername", (uintptr_t)&win_getpeername),
    NO_THUNK("getsockname", (uintptr_t)&win_getsockname),
    NO_THUNK("getsockopt", (uintptr_t)&win_getsockopt),
    NO_THUNK("ioctl", (uintptr_t)&win_ioctl),
    NO_THUNK("listen", (uintptr_t)&win_listen),
    NO_THUNK("recv", (uintptr_t)&win_recv),
    NO_THUNK("recvfrom", (uintptr_t)&win_recvfrom),
    NO_THUNK("select", (uintptr_t)&win_select),
    NO_THUNK("send", (uintptr_t)&win_send),
    NO_THUNK("sendto", (uintptr_t)&win_sendto),
    NO_THUNK("setsockopt", (uintptr_t)&win_setsockopt),

    {nullptr, 0},
};
