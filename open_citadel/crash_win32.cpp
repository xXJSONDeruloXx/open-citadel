/* Best-effort fault reporting for the x86 ELF mapped into the Win32 process. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "crash.h"

namespace {

so_module *g_guest_module;
const char *g_guest_soname = "libUnrealEngine3.so";
PVOID g_first_chance_handler;

LONG CALLBACK report_first_chance_exception(EXCEPTION_POINTERS *exception)
{
    if (!exception || !exception->ExceptionRecord || !exception->ContextRecord)
        return EXCEPTION_CONTINUE_SEARCH;

    const DWORD code = exception->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;

    const uintptr_t pc = exception->ContextRecord->Eip;
    const bool in_guest = g_guest_module &&
        pc >= g_guest_module->text_base &&
        pc < g_guest_module->text_base + g_guest_module->text_size;
    if (!in_guest && !getenv("OPEN_CITADEL_TRACE_CRASH"))
        return EXCEPTION_CONTINUE_SEARCH;

    if (in_guest)
        fprintf(stderr,
                "OpenCitadel: first-chance Win32 exception 0x%08lx at %s+0x%08lx\n",
                (unsigned long)code, g_guest_soname,
                (unsigned long)(pc - g_guest_module->text_base));
    else
        fprintf(stderr,
                "OpenCitadel: first-chance Win32 exception 0x%08lx at 0x%08lx\n",
                (unsigned long)code, (unsigned long)pc);

    if (code == EXCEPTION_ACCESS_VIOLATION &&
        exception->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR operation =
            exception->ExceptionRecord->ExceptionInformation[0];
        const ULONG_PTR address =
            exception->ExceptionRecord->ExceptionInformation[1];
        fprintf(stderr, "  access=%s address=%p\n",
                operation == 0 ? "read" : operation == 1 ? "write" : "execute",
                (void *)address);
    }
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI report_windows_exception(EXCEPTION_POINTERS *exception)
{
    if (!exception || !exception->ExceptionRecord || !exception->ContextRecord)
        return EXCEPTION_CONTINUE_SEARCH;

    const DWORD code = exception->ExceptionRecord->ExceptionCode;
    const uintptr_t pc = exception->ContextRecord->Eip;
    const uintptr_t sp = exception->ContextRecord->Esp;
    const bool in_guest = g_guest_module &&
        pc >= g_guest_module->text_base &&
        pc < g_guest_module->text_base + g_guest_module->text_size;
    if (in_guest) {
        fprintf(stderr,
                "OpenCitadel: unhandled Win32 exception 0x%08lx at %s+0x%08lx\n",
                (unsigned long)code, g_guest_soname,
                (unsigned long)(pc - g_guest_module->text_base));
    } else {
        fprintf(stderr,
                "OpenCitadel: unhandled Win32 exception 0x%08lx at 0x%08lx\n",
                (unsigned long)code, (unsigned long)pc);
    }

    const CONTEXT *context = exception->ContextRecord;
    fprintf(stderr,
            "  EAX=%08lx EBX=%08lx ECX=%08lx EDX=%08lx ESI=%08lx EDI=%08lx\n"
            "  EBP=%08lx ESP=%08lx EIP=%08lx EFLAGS=%08lx\n",
            (unsigned long)context->Eax, (unsigned long)context->Ebx,
            (unsigned long)context->Ecx, (unsigned long)context->Edx,
            (unsigned long)context->Esi, (unsigned long)context->Edi,
            (unsigned long)context->Ebp, (unsigned long)sp,
            (unsigned long)pc, (unsigned long)context->EFlags);
    if (code == EXCEPTION_ACCESS_VIOLATION &&
        exception->ExceptionRecord->NumberParameters >= 2)
        fprintf(stderr, "  access=%llu address=%p\n",
                (unsigned long long)exception->ExceptionRecord->ExceptionInformation[0],
                (void *)exception->ExceptionRecord->ExceptionInformation[1]);
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

void report_abort_signal(int signal_number)
{
    fprintf(stderr, "OpenCitadel: guest raised signal %d (abort)\n",
            signal_number);
    fflush(stderr);
    ExitProcess(128u + (unsigned int)signal_number);
}

} // namespace

extern "C" void crash_report_init(so_module *module, const char *soname)
{
    g_guest_module = module;
    g_guest_soname = soname ? soname : "libUnrealEngine3.so";
    SetUnhandledExceptionFilter(report_windows_exception);
    if (!g_first_chance_handler)
        g_first_chance_handler = AddVectoredExceptionHandler(
            1, report_first_chance_exception);
    if (getenv("OPEN_CITADEL_TRACE_CRASH"))
        fprintf(stderr,
                "OpenCitadel: crash reporter first-chance=%p guest=%p text=%p+0x%08lx\n",
                g_first_chance_handler, (void *)g_guest_module,
                g_guest_module ? (void *)g_guest_module->text_base : nullptr,
                (unsigned long)(g_guest_module ? g_guest_module->text_size : 0));
    signal(SIGABRT, report_abort_signal);
}
