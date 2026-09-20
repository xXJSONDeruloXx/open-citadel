#ifndef OPEN_CITADEL_ANDROID_INPUT_CODES_H
#define OPEN_CITADEL_ANDROID_INPUT_CODES_H

/* Numeric input values used by the Android UE3 guest ABI. */
namespace android_input {

constexpr int kActionDown = 0;
constexpr int kActionUp = 1;
constexpr int kActionMove = 2;
constexpr int kActionCancel = 3;
constexpr int kActionOutside = 4;
constexpr int kActionPointerDown = 5;
constexpr int kActionPointerUp = 6;

constexpr int kAxisX = 0;
constexpr int kAxisY = 1;
constexpr int kAxisVscroll = 9;
constexpr int kAxisHscroll = 10;
constexpr int kAxisZ = 11;
constexpr int kAxisRz = 14;
constexpr int kAxisLtrigger = 17;
constexpr int kAxisRtrigger = 18;

constexpr int kKeyHome = 3;
constexpr int kKeyBack = 4;
constexpr int kKey0 = 7;
constexpr int kKeyStar = 17;
constexpr int kKeyDpadUp = 19;
constexpr int kKeyDpadDown = 20;
constexpr int kKeyDpadLeft = 21;
constexpr int kKeyDpadRight = 22;
constexpr int kKeyA = 29;
constexpr int kKeyComma = 55;
constexpr int kKeyPeriod = 56;
constexpr int kKeyAltLeft = 57;
constexpr int kKeyAltRight = 58;
constexpr int kKeyShiftLeft = 59;
constexpr int kKeyShiftRight = 60;
constexpr int kKeyTab = 61;
constexpr int kKeySpace = 62;
constexpr int kKeyEnter = 66;
constexpr int kKeyDel = 67;
constexpr int kKeyGrave = 68;
constexpr int kKeyMinus = 69;
constexpr int kKeyEquals = 70;
constexpr int kKeyLeftBracket = 71;
constexpr int kKeyRightBracket = 72;
constexpr int kKeyBackslash = 73;
constexpr int kKeySemicolon = 74;
constexpr int kKeyApostrophe = 75;
constexpr int kKeySlash = 76;
constexpr int kKeyNum = 78;
constexpr int kKeyPlus = 81;
constexpr int kKeyMenu = 82;
constexpr int kKeySearch = 84;
constexpr int kKeyPageUp = 92;
constexpr int kKeyPageDown = 93;
constexpr int kKeyButtonA = 96;
constexpr int kKeyButtonB = 97;
constexpr int kKeyButtonC = 98;
constexpr int kKeyButtonX = 99;
constexpr int kKeyButtonY = 100;
constexpr int kKeyButtonZ = 101;
constexpr int kKeyButtonL1 = 102;
constexpr int kKeyButtonR1 = 103;
constexpr int kKeyButtonL2 = 104;
constexpr int kKeyButtonR2 = 105;
constexpr int kKeyButtonThumbL = 106;
constexpr int kKeyButtonThumbR = 107;
constexpr int kKeyButtonStart = 108;
constexpr int kKeyButtonSelect = 109;
constexpr int kKeyButtonMode = 110;
constexpr int kKeyEscape = 111;
constexpr int kKeyCtrlLeft = 113;
constexpr int kKeyCtrlRight = 114;
constexpr int kKeyCapsLock = 115;
constexpr int kKeyScrollLock = 116;
constexpr int kKeyMetaLeft = 117;
constexpr int kKeyMetaRight = 118;
constexpr int kKeySysRq = 120;
constexpr int kKeyBreak = 121;
constexpr int kKeyMoveEnd = 123;
constexpr int kKeyInsert = 124;
constexpr int kKeyF1 = 131;
constexpr int kKeyNumLock = 143;
constexpr int kKeyNumpad0 = 144;
constexpr int kKeyNumpadDivide = 154;
constexpr int kKeyNumpadMultiply = 155;
constexpr int kKeyNumpadSubtract = 156;
constexpr int kKeyNumpadAdd = 157;
constexpr int kKeyNumpadDot = 158;
constexpr int kKeyNumpadComma = 159;
constexpr int kKeyNumpadEnter = 160;
constexpr int kKeyNumpadEquals = 161;
constexpr int kKeyButton1 = 188;

} // namespace android_input

#endif
