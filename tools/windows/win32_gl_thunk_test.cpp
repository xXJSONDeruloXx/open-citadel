#include "thunk_gen_dyn.h"

#include <stdint.h>
#include <stdio.h>

#if !defined(_WIN32) || !defined(_M_IX86)
#error This test must use the 32-bit Windows calling conventions.
#endif

static int g_calls;

static int __stdcall host_sum(int a, int b, int c, int d)
{
    ++g_calls;
    return a + b + c + d;
}

static float __stdcall host_scale(float value, int scale)
{
    ++g_calls;
    return value * (float)scale;
}

using SumGuest = int (*)(int, int, int, int);
using SumHost = int (__stdcall *)(int, int, int, int);
using ScaleGuest = float (*)(float, int);
using ScaleHost = float (__stdcall *)(float, int);

static SumHost g_host_sum = &host_sum;
static ScaleHost g_host_scale = &host_scale;

bool gl_diag_enabled(void) { return false; }
void gl_diag_before(const char *) {}
void gl_diag_after(const char *) {}

int main()
{
    const uintptr_t sum_address = select_either_ptr<&g_host_sum>(
        reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(g_host_sum)),
        "host_sum");
    if (sum_address == reinterpret_cast<uintptr_t>(g_host_sum)) {
        fprintf(stderr, "stdcall host function bypassed the cdecl bridge\n");
        return 1;
    }
    const SumGuest sum_guest = reinterpret_cast<SumGuest>(sum_address);
    for (int i = 0; i < 10000; ++i) {
        if (sum_guest(1, 2, 3, 4) != 10)
            return 2;
    }

    const uintptr_t scale_address = select_either_ptr<&g_host_scale>(
        reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(g_host_scale)),
        "host_scale");
    if (scale_address == reinterpret_cast<uintptr_t>(g_host_scale))
        return 3;
    const ScaleGuest scale_guest = reinterpret_cast<ScaleGuest>(scale_address);
    if (scale_guest(1.25f, 4) != 5.0f || g_calls != 10001)
        return 4;

    printf("Win32 cdecl guest -> stdcall GLES bridge passed (%d calls)\n",
           g_calls);
    return 0;
}
