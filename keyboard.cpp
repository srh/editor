#include "keyboard.hpp"

#include <inttypes.h>

#include "error.hpp"

void append_special_key_render(std::string *onto, keypress::special_key sk) {
    using special_key = keypress::special_key;
    switch (sk) {
    case special_key::F1: *onto += "F1"; break;
    case special_key::F2: *onto += "F2"; break;
    case special_key::F3: *onto += "F3"; break;
    case special_key::F4: *onto += "F4"; break;
    case special_key::F5: *onto += "F5"; break;
    case special_key::F6: *onto += "F6"; break;
    case special_key::F7: *onto += "F7"; break;
    case special_key::F8: *onto += "F8"; break;
    case special_key::F9: *onto += "F9"; break;
    case special_key::F10: *onto += "F10"; break;
    case special_key::F11: *onto += "F11"; break;
    case special_key::F12: *onto += "F12"; break;
    case special_key::Backspace: *onto += "Backspace"; break;
    case special_key::Tab: *onto += "Tab"; break;
    case special_key::CapsLock: *onto += "CapsLock"; break;
    case special_key::Enter: *onto += "Enter"; break;
    case special_key::Insert: *onto += "Insert"; break;
    case special_key::Delete: *onto += "Delete"; break;
    case special_key::Home: *onto += "Home"; break;
    case special_key::End: *onto += "End"; break;
    case special_key::PageUp: *onto += "PageUp"; break;
    case special_key::PageDown: *onto += "PageDown"; break;
    case special_key::Left: *onto += "Left"; break;
    case special_key::Right: *onto += "Right"; break;
    case special_key::Up: *onto += "Up"; break;
    case special_key::Down: *onto += "Down"; break;
    case special_key::PauseBreak: *onto += "PauseBreak"; break;
    case special_key::PrintScreen: *onto += "PrintScreen"; break;
    case special_key::ScrollLock: *onto += "ScrollLock"; break;
    }
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
