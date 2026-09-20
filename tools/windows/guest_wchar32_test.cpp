#include <cassert>
#include <cstdint>

#include "wchar32_win32.h"

int main()
{
    using namespace open_citadel;

    const guest_wchar source[] = {'g', 'a', 'm', 'e', 0};
    const guest_wchar suffix[] = {'d', 'a', 't', 'a', 0};
    const guest_wchar full[] = {'g', 'a', 'm', 'e', 'd', 'a', 't', 'a', 0};
    const guest_wchar tail[] = {'d', 'a', 't', 'a', 0};
    guest_wchar buffer[16]{};

    assert(guest_wcslen(source) == 4);
    assert(guest_wcscpy(buffer, source) == buffer);
    assert(guest_wcscmp(buffer, source) == 0);
    assert(guest_wcsncmp(buffer, source, 3) == 0);
    assert(guest_wcscoll(buffer, source) == 0);
    assert(guest_wcscat(buffer, suffix) == buffer);
    assert(guest_wcslen(buffer) == 8);
    assert(guest_wcscmp(buffer, full) == 0);
    assert(guest_wcschr(buffer, 'd') == buffer + 4);
    assert(guest_wcsrchr(buffer, 'a') == buffer + 6);
    assert(guest_wcsstr(buffer, tail) == buffer + 4);

    guest_wchar padded[6] = {};
    guest_wcsncpy(padded, source, 6);
    assert(guest_wcslen(padded) == 4);
    assert(padded[5] == 0);
    assert(guest_iswdigit('7'));
    assert(!guest_iswdigit('g'));
    assert(guest_iswspace(0x3000));
    assert(!guest_iswspace('g'));
    return 0;
}
