/*
 * Win32 x86 replacements for the Open Citadel profile's libm/off_t tables.
 *
 * The guest and host are both i386 cdecl, so these calls do not need an ABI
 * thunk. Keeping this table separate avoids importing Linux-only mmap/ioctl
 * headers into the native Windows build.
 */
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <io.h>

#include "so_util.h"
#include "thunks/thunk_gen.h"

namespace {

double win_acos(double value) { return std::acos(value); }
float win_acosf(float value) { return std::acos(value); }
double win_asin(double value) { return std::asin(value); }
float win_asinf(float value) { return std::asin(value); }
double win_atan(double value) { return std::atan(value); }
float win_atanf(float value) { return std::atan(value); }
double win_atan2(double y, double x) { return std::atan2(y, x); }
float win_atan2f(float y, float x) { return std::atan2(y, x); }
double win_ceil(double value) { return std::ceil(value); }
float win_ceilf(float value) { return std::ceil(value); }
double win_cos(double value) { return std::cos(value); }
float win_cosf(float value) { return std::cos(value); }
double win_exp(double value) { return std::exp(value); }
float win_expf(float value) { return std::exp(value); }
double win_floor(double value) { return std::floor(value); }
float win_floorf(float value) { return std::floor(value); }
double win_fmod(double x, double y) { return std::fmod(x, y); }
float win_fmodf(float x, float y) { return std::fmod(x, y); }
double win_frexp(double value, int *exponent)
{
    return std::frexp(value, exponent);
}
double win_ldexp(double value, int exponent)
{
    return std::ldexp(value, exponent);
}
float win_ldexpf(float value, int exponent)
{
    return std::ldexp(value, exponent);
}
double win_log(double value) { return std::log(value); }
double win_log10(double value) { return std::log10(value); }
float win_log10f(float value) { return std::log10(value); }
float win_logf(float value) { return std::log(value); }
double win_modf(double value, double *integral)
{
    return std::modf(value, integral);
}
double win_pow(double base, double exponent)
{
    return std::pow(base, exponent);
}
float win_powf(float base, float exponent)
{
    return std::pow(base, exponent);
}
float win_roundf(float value) { return std::round(value); }
double win_sin(double value) { return std::sin(value); }
float win_sinf(float value) { return std::sin(value); }
double win_sqrt(double value) { return std::sqrt(value); }
float win_sqrtf(float value) { return std::sqrt(value); }
double win_tan(double value) { return std::tan(value); }
float win_tanf(float value) { return std::tan(value); }

int win_swscanf(const wchar_t *input, const wchar_t *format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = vswscanf(input, format, args);
    va_end(args);
    return result;
}

void win_assert2(const char *file, int line, const char *function,
                 const char *expression)
{
    fprintf(stderr, "assertion failed: %s:%d: %s: %s\n",
            file ? file : "?", line, function ? function : "?",
            expression ? expression : "?");
    fflush(stderr);
    abort();
}

int win_isfinitef(float value)
{
    return _finite((double)value);
}

int win_isinff(float value)
{
    return _fpclass(value) == _FPCLASS_NINF ||
           _fpclass(value) == _FPCLASS_PINF;
}

} // namespace

DynLibFunction symtable_libm[] = {
    NO_THUNK("acos", (uintptr_t)&win_acos),
    NO_THUNK("acosf", (uintptr_t)&win_acosf),
    NO_THUNK("asin", (uintptr_t)&win_asin),
    NO_THUNK("asinf", (uintptr_t)&win_asinf),
    NO_THUNK("atan", (uintptr_t)&win_atan),
    NO_THUNK("atanf", (uintptr_t)&win_atanf),
    NO_THUNK("atan2", (uintptr_t)&win_atan2),
    NO_THUNK("atan2f", (uintptr_t)&win_atan2f),
    NO_THUNK("ceil", (uintptr_t)&win_ceil),
    NO_THUNK("ceilf", (uintptr_t)&win_ceilf),
    NO_THUNK("cos", (uintptr_t)&win_cos),
    NO_THUNK("cosf", (uintptr_t)&win_cosf),
    NO_THUNK("exp", (uintptr_t)&win_exp),
    NO_THUNK("expf", (uintptr_t)&win_expf),
    NO_THUNK("floor", (uintptr_t)&win_floor),
    NO_THUNK("floorf", (uintptr_t)&win_floorf),
    NO_THUNK("fmod", (uintptr_t)&win_fmod),
    NO_THUNK("fmodf", (uintptr_t)&win_fmodf),
    NO_THUNK("frexp", (uintptr_t)&win_frexp),
    NO_THUNK("ldexp", (uintptr_t)&win_ldexp),
    NO_THUNK("ldexpf", (uintptr_t)&win_ldexpf),
    NO_THUNK("log", (uintptr_t)&win_log),
    NO_THUNK("log10", (uintptr_t)&win_log10),
    NO_THUNK("log10f", (uintptr_t)&win_log10f),
    NO_THUNK("logf", (uintptr_t)&win_logf),
    NO_THUNK("modf", (uintptr_t)&win_modf),
    NO_THUNK("pow", (uintptr_t)&win_pow),
    NO_THUNK("powf", (uintptr_t)&win_powf),
    NO_THUNK("roundf", (uintptr_t)&win_roundf),
    NO_THUNK("sin", (uintptr_t)&win_sin),
    NO_THUNK("sinf", (uintptr_t)&win_sinf),
    NO_THUNK("sqrt", (uintptr_t)&win_sqrt),
    NO_THUNK("sqrtf", (uintptr_t)&win_sqrtf),
    NO_THUNK("tan", (uintptr_t)&win_tan),
    NO_THUNK("tanf", (uintptr_t)&win_tanf),
    NO_THUNK("swscanf", (uintptr_t)&win_swscanf),
    NO_THUNK("__assert2", (uintptr_t)&win_assert2),
    NO_THUNK("__isfinitef", (uintptr_t)&win_isfinitef),
    NO_THUNK("__isinff", (uintptr_t)&win_isinff),
    {nullptr, 0},
};

static int32_t win_lseek(int fd, int32_t offset, int whence)
{
    const __int64 result = _lseeki64(fd, (__int64)offset, whence);
    if (result < 0 || result > INT32_MAX) {
        if (result > INT32_MAX)
            errno = EOVERFLOW;
        return -1;
    }
    return (int32_t)result;
}

DynLibFunction symtable_off[] = {
    NO_THUNK("lseek", (uintptr_t)&win_lseek),
    {nullptr, 0},
};
