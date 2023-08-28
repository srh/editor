#ifndef QWERTILLION_CHARS_HPP_
#define QWERTILLION_CHARS_HPP_

#include <stdint.h>

#include <string>

namespace qwi {

// TODO: buffer_string and buffer_char don't belong in this header.
struct buffer_char {
    uint8_t value;

    static buffer_char from_char(char ch) { return buffer_char{uint8_t(ch)}; }
    friend auto operator<=>(buffer_char, buffer_char) = default;
};

using buffer_string = std::basic_string<buffer_char>;

buffer_string to_buffer_string(const std::string& s);

// Does "Side" belong here?  The side of some insertion or deletion relative to the
// cursor?  Not really, but where should it go?
enum class Side { left, right, };


inline char *as_chars(buffer_char *chs) {
    static_assert(sizeof(*chs) == sizeof(char));
    return reinterpret_cast<char *>(chs);
}

inline const char *as_chars(const buffer_char *chs) {
    static_assert(sizeof(*chs) == sizeof(char));
    return reinterpret_cast<const char *>(chs);
}

inline buffer_char *as_buffer_chars(char *chs) {
    static_assert(sizeof(*chs) == sizeof(buffer_char));
    return reinterpret_cast<buffer_char *>(chs);
}

inline const buffer_char *as_buffer_chars(const char *chs) {
    static_assert(sizeof(*chs) == sizeof(buffer_char));
    return reinterpret_cast<const buffer_char *>(chs);
}


}  // namespace qwi

#endif  // QWERTILLION_CHARS_HPP_