#pragma once

#include <stddef.h>
#include <stdint.h>

/* Android's i386 ABI uses 32-bit wchar_t; MSVC's wchar_t is 16-bit. */
namespace open_citadel {

using guest_wchar = uint32_t;

inline size_t guest_wcslen(const guest_wchar *text)
{
    const guest_wchar *end = text;
    while (*end)
        ++end;
    return (size_t)(end - text);
}

inline int guest_wcscmp(const guest_wchar *left, const guest_wchar *right)
{
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return *left < *right ? -1 : *left > *right ? 1 : 0;
}

inline int guest_wcsncmp(const guest_wchar *left, const guest_wchar *right,
                         size_t count)
{
    while (count && *left && *left == *right) {
        ++left;
        ++right;
        --count;
    }
    if (!count)
        return 0;
    return *left < *right ? -1 : *left > *right ? 1 : 0;
}

inline int guest_wcscoll(const guest_wchar *left, const guest_wchar *right)
{
    return guest_wcscmp(left, right);
}

inline guest_wchar *guest_wcscpy(guest_wchar *destination,
                                 const guest_wchar *source)
{
    guest_wchar *result = destination;
    do {
        *destination++ = *source;
    } while (*source++);
    return result;
}

inline guest_wchar *guest_wcscat(guest_wchar *destination,
                                 const guest_wchar *source)
{
    guest_wcscpy(destination + guest_wcslen(destination), source);
    return destination;
}

inline guest_wchar *guest_wcsncpy(guest_wchar *destination,
                                  const guest_wchar *source, size_t count)
{
    guest_wchar *result = destination;
    size_t copied = 0;
    while (copied < count && source[copied]) {
        destination[copied] = source[copied];
        ++copied;
    }
    while (copied < count)
        destination[copied++] = 0;
    return result;
}

inline guest_wchar *guest_wcschr(const guest_wchar *text, guest_wchar value)
{
    for (;;) {
        if (*text == value)
            return const_cast<guest_wchar *>(text);
        if (!*text)
            return nullptr;
        ++text;
    }
}

inline guest_wchar *guest_wcsrchr(const guest_wchar *text, guest_wchar value)
{
    const guest_wchar *match = nullptr;
    for (;;) {
        if (*text == value)
            match = text;
        if (!*text)
            return const_cast<guest_wchar *>(match);
        ++text;
    }
}

inline guest_wchar *guest_wcsstr(const guest_wchar *text,
                                 const guest_wchar *needle)
{
    if (!*needle)
        return const_cast<guest_wchar *>(text);
    for (; *text; ++text) {
        const guest_wchar *left = text;
        const guest_wchar *right = needle;
        while (*left && *right && *left == *right) {
            ++left;
            ++right;
        }
        if (!*right)
            return const_cast<guest_wchar *>(text);
    }
    return nullptr;
}

inline int guest_iswdigit(guest_wchar value)
{
    return value >= '0' && value <= '9';
}

inline int guest_iswspace(guest_wchar value)
{
    return value == ' ' || (value >= '\t' && value <= '\r') ||
           value == 0x85 || value == 0xa0 || value == 0x1680 ||
           (value >= 0x2000 && value <= 0x200a) || value == 0x2028 ||
           value == 0x2029 || value == 0x202f || value == 0x205f ||
           value == 0x3000;
}

} // namespace open_citadel
