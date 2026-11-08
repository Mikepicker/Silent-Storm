/*  android/keycodes.h -- macOS stand-in.
 *
 *  platform/input_android.cpp maps Android key codes onto the engine's NInput
 *  layer.  Apart from this header it is portable C++ (std::deque, std::mutex),
 *  so the macOS build reuses it verbatim and feeds it these same codes from
 *  SDL -- see SdlToAndroidKey() in macos_main.cpp.
 *
 *  Values are the stable, documented Android NDK constants.
 */
#ifndef A5_MACOS_ANDROID_KEYCODES_H
#define A5_MACOS_ANDROID_KEYCODES_H

enum {
    AKEYCODE_UNKNOWN            = 0,
    AKEYCODE_HOME               = 3,
    AKEYCODE_BACK               = 4,

    AKEYCODE_0                  = 7,
    AKEYCODE_1                  = 8,
    AKEYCODE_2                  = 9,
    AKEYCODE_3                  = 10,
    AKEYCODE_4                  = 11,
    AKEYCODE_5                  = 12,
    AKEYCODE_6                  = 13,
    AKEYCODE_7                  = 14,
    AKEYCODE_8                  = 15,
    AKEYCODE_9                  = 16,

    AKEYCODE_DPAD_UP            = 19,
    AKEYCODE_DPAD_DOWN          = 20,
    AKEYCODE_DPAD_LEFT          = 21,
    AKEYCODE_DPAD_RIGHT         = 22,
    AKEYCODE_VOLUME_UP          = 24,
    AKEYCODE_VOLUME_DOWN        = 25,

    AKEYCODE_A                  = 29,
    AKEYCODE_B                  = 30,
    AKEYCODE_C                  = 31,
    AKEYCODE_D                  = 32,
    AKEYCODE_E                  = 33,
    AKEYCODE_F                  = 34,
    AKEYCODE_G                  = 35,
    AKEYCODE_H                  = 36,
    AKEYCODE_I                  = 37,
    AKEYCODE_J                  = 38,
    AKEYCODE_K                  = 39,
    AKEYCODE_L                  = 40,
    AKEYCODE_M                  = 41,
    AKEYCODE_N                  = 42,
    AKEYCODE_O                  = 43,
    AKEYCODE_P                  = 44,
    AKEYCODE_Q                  = 45,
    AKEYCODE_R                  = 46,
    AKEYCODE_S                  = 47,
    AKEYCODE_T                  = 48,
    AKEYCODE_U                  = 49,
    AKEYCODE_V                  = 50,
    AKEYCODE_W                  = 51,
    AKEYCODE_X                  = 52,
    AKEYCODE_Y                  = 53,
    AKEYCODE_Z                  = 54,

    AKEYCODE_COMMA              = 55,
    AKEYCODE_PERIOD             = 56,
    AKEYCODE_ALT_LEFT           = 57,
    AKEYCODE_ALT_RIGHT          = 58,
    AKEYCODE_SHIFT_LEFT         = 59,
    AKEYCODE_SHIFT_RIGHT        = 60,
    AKEYCODE_TAB                = 61,
    AKEYCODE_SPACE              = 62,
    AKEYCODE_ENTER              = 66,
    AKEYCODE_DEL                = 67,   /* backspace */
    AKEYCODE_GRAVE              = 68,
    AKEYCODE_MINUS              = 69,
    AKEYCODE_EQUALS             = 70,
    AKEYCODE_LEFT_BRACKET       = 71,
    AKEYCODE_RIGHT_BRACKET      = 72,
    AKEYCODE_BACKSLASH          = 73,
    AKEYCODE_SEMICOLON          = 74,
    AKEYCODE_APOSTROPHE         = 75,
    AKEYCODE_SLASH              = 76,
    AKEYCODE_AT                 = 77,
    AKEYCODE_MENU               = 82,

    AKEYCODE_MEDIA_PLAY_PAUSE   = 85,
    AKEYCODE_MEDIA_STOP         = 86,
    AKEYCODE_MEDIA_NEXT         = 87,
    AKEYCODE_MEDIA_PREVIOUS     = 88,

    AKEYCODE_PAGE_UP            = 92,
    AKEYCODE_PAGE_DOWN          = 93,

    AKEYCODE_ESCAPE             = 111,
    AKEYCODE_FORWARD_DEL        = 112,  /* delete */
    AKEYCODE_CTRL_LEFT          = 113,
    AKEYCODE_CTRL_RIGHT         = 114,
    AKEYCODE_CAPS_LOCK          = 115,
    AKEYCODE_SCROLL_LOCK        = 116,
    AKEYCODE_META_LEFT          = 117,
    AKEYCODE_META_RIGHT         = 118,
    AKEYCODE_SYSRQ              = 120,
    AKEYCODE_BREAK              = 121,
    AKEYCODE_MOVE_HOME          = 122,
    AKEYCODE_MOVE_END           = 123,
    AKEYCODE_INSERT             = 124,

    AKEYCODE_F1                 = 131,
    AKEYCODE_F2                 = 132,
    AKEYCODE_F3                 = 133,
    AKEYCODE_F4                 = 134,
    AKEYCODE_F5                 = 135,
    AKEYCODE_F6                 = 136,
    AKEYCODE_F7                 = 137,
    AKEYCODE_F8                 = 138,
    AKEYCODE_F9                 = 139,
    AKEYCODE_F10                = 140,
    AKEYCODE_F11                = 141,
    AKEYCODE_F12                = 142,

    AKEYCODE_NUM_LOCK           = 143,
    AKEYCODE_NUMPAD_0           = 144,
    AKEYCODE_NUMPAD_1           = 145,
    AKEYCODE_NUMPAD_2           = 146,
    AKEYCODE_NUMPAD_3           = 147,
    AKEYCODE_NUMPAD_4           = 148,
    AKEYCODE_NUMPAD_5           = 149,
    AKEYCODE_NUMPAD_6           = 150,
    AKEYCODE_NUMPAD_7           = 151,
    AKEYCODE_NUMPAD_8           = 152,
    AKEYCODE_NUMPAD_9           = 153,
    AKEYCODE_NUMPAD_DIVIDE      = 154,
    AKEYCODE_NUMPAD_MULTIPLY    = 155,
    AKEYCODE_NUMPAD_SUBTRACT    = 156,
    AKEYCODE_NUMPAD_ADD         = 157,
    AKEYCODE_NUMPAD_DOT         = 158,
    AKEYCODE_NUMPAD_COMMA       = 159,
    AKEYCODE_NUMPAD_ENTER       = 160,
    AKEYCODE_NUMPAD_EQUALS      = 161,

    AKEYCODE_VOLUME_MUTE        = 164
};

#endif
