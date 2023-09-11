#ifndef QWERTILLION_KEYBOARD_HPP_
#define QWERTILLION_KEYBOARD_HPP_

#include <stdint.h>

#include <string>

struct keypress {
    // modmask only has 'SHIFT' for special keys -- ordinary characters like 'A' are
    // represented as {'A', 0}, Ctrl+A is {'a', CTRL}, Ctrl+Shift+A is {'A', CTRL} if we
    // can even detect that.

    using key_type = int32_t;  /* Negative values used for special keys, positive might be Unicode */
    using modmask_type = uint8_t;

    enum class special_key : int32_t {
        F1 = 1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        Backspace, Tab, CapsLock, Enter,
        Insert, Delete, Home, End, PageUp, PageDown,
        Left, Right, Up, Down,
        PauseBreak, PrintScreen, ScrollLock,
    };

    static constexpr key_type special_to_key_type(special_key sk) {
        return -static_cast<key_type>(sk);
    }

    key_type value = 0;
    modmask_type modmask = 0;
    bool isMisparsed = false;
    std::string chars_read = "";  // Supplied for escape sequences irrespective of whether
                                  // isMisparsed is true or not

    // TODO: Actually separate out the {value, modmask} part.
    // We really ought to separate the {value, modmask} part of the keypress to a separate
    // type from isMisparsed and chars_read, which is a completely ancillary communication
    // from the keypress parsing function.
    bool equals(key_type _value, modmask_type _mm = 0) const {
        return value == _value && modmask == _mm;
    }

    bool equals(special_key sk, modmask_type _mm = 0) const {
        return value == special_to_key_type(sk) && modmask == _mm;
    }

    static constexpr modmask_type
        META = 1,
        SHIFT = 2,
        CTRL = 4,
        SUPER = 8;

    static special_key invalid_special() { return static_cast<special_key>(0); }

    static keypress ascii(char ch, modmask_type mm = 0) { return { .value = uint8_t(ch), .modmask = mm }; }
    static keypress special(special_key k, modmask_type mm = 0) {
        return { .value = special_to_key_type(k), .modmask = mm };
    }
    static keypress incomplete_parse(const std::string& chars) {
        return { .isMisparsed = true, .chars_read = chars };
    }
};

std::string render_keypress(const keypress& kp);

#endif  // QWERTILLION_KEYBOARD_HPP_

