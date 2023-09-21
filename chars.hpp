#ifndef QWERTILLION_CHARS_HPP_
#define QWERTILLION_CHARS_HPP_

#include <stdint.h>

#include <string>
#include <span>

namespace qwi {

constexpr uint8_t TAB_WIDTH = 8;
constexpr uint8_t TAB_MOD_MASK = TAB_WIDTH - 1;  // 8 is hard-coded tab stop

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

inline std::span<const char> as_char_span(const buffer_string& bs) {
    return std::span<const char>{as_chars(bs.data()), bs.size()};
}

inline std::span<const buffer_char> as_buffer_char_span(const std::string& s) {
    return {as_buffer_chars(s.data()), s.size()};
}


}  // namespace qwi

#endif  // QWERTILLION_CHARS_HPP_
