#include "keyboard.hpp"

#include <inttypes.h>

#include "error.hpp"

// TODO(?): UI logic
const char *special_key_name(keypress::special_key sk) {
    using special_key = keypress::special_key;
    // These are UI values.
    switch (sk) {
    case special_key::F1: return "F1";
    case special_key::F2: return "F2";
    case special_key::F3: return "F3";
    case special_key::F4: return "F4";
    case special_key::F5: return "F5";
    case special_key::F6: return "F6";
    case special_key::F7: return "F7";
    case special_key::F8: return "F8";
    case special_key::F9: return "F9";
    case special_key::F10: return "F10";
    case special_key::F11: return "F11";
    case special_key::F12: return "F12";
    case special_key::Backspace: return "Backspace";
    case special_key::Tab: return "Tab";
    case special_key::CapsLock: return "CapsLock";
    case special_key::Enter: return "Enter";
    case special_key::Insert: return "Insert";
    case special_key::Delete: return "Delete";
    case special_key::Home: return "Home";
    case special_key::End: return "End";
    case special_key::PageUp: return "PageUp";
    case special_key::PageDown: return "PageDown";
    case special_key::Left: return "Left";
    case special_key::Right: return "Right";
    case special_key::Up: return "Up";
    case special_key::Down: return "Down";
    case special_key::PauseBreak: return "PauseBreak";
    case special_key::PrintScreen: return "PrintScreen";
    case special_key::ScrollLock: return "ScrollLock";
    default:
        logic_fail("Invalid special_key: %" PRIi32, static_cast<int32_t>(sk));
    }
}

void append_special_key_render(std::string *onto, keypress::special_key sk) {
    *onto += special_key_name(sk);
}

std::string render_keypress(const keypress& kp) {
    // There's a todo about separating out the {value, modmask} part from keypress.

    std::string mod_prefix;
    mod_prefix.reserve(9);
    if (kp.modmask & keypress::CTRL) {
        mod_prefix += "C-";
    }
    if (kp.modmask & keypress::META) {
        mod_prefix += "M-";
    }
    if (kp.modmask & keypress::SHIFT) {
        mod_prefix += "S-";
    }
    if (kp.modmask & keypress::SUPER) {
        mod_prefix += "s-";
    }

    if (kp.value < 0) {
        keypress::special_key sk = static_cast<keypress::special_key>(-kp.value);
        append_special_key_render(&mod_prefix, sk);
    } else {
        if (kp.value > ' ' && kp.value < 127) {
            mod_prefix += char(kp.value);
        } else if (kp.value == ' ') {
            // Maybe Space should be a special_key.
            mod_prefix += "Space";
        } else {
            logic_fail("Impossible keypress value %" PRIi32, kp.value);
#if 0
            mod_prefix += '?';
            mod_prefix += std::to_string(kp.value);
#endif
        }
    }

    return mod_prefix;
}
