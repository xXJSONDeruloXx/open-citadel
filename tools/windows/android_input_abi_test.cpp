#include "android_input_codes.h"

static_assert(android_input::kActionPointerUp == 6);
static_assert(android_input::kAxisX == 0);
static_assert(android_input::kAxisY == 1);
static_assert(android_input::kAxisZ == 11);
static_assert(android_input::kAxisRz == 14);
static_assert(android_input::kAxisLtrigger == 17);
static_assert(android_input::kAxisRtrigger == 18);
static_assert(android_input::kKeyButtonA == 96);
static_assert(android_input::kKeyButtonMode == 110);
static_assert(android_input::kKeyEscape == 111);
static_assert(android_input::kKeyNumpad0 == 144);
static_assert(android_input::kKeyNumpadEnter == 160);
static_assert(android_input::kKeyButton1 == 188);

int main()
{
    return 0;
}
