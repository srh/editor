#ifndef QWERTILLION_BUFFER_HPP_
#define QWERTILLION_BUFFER_HPP_

#include "state.hpp"

namespace qwi {

struct [[nodiscard]] insert_result {
    // Cursor position _after_ insertion
    size_t new_cursor;
    // Returned only to make implementing opposite(const undo_info&) easier.
    buffer_string insertedText;
    Side side;
};

insert_result insert_chars(buffer *buf, const buffer_char *chs, size_t count);

inline insert_result insert_char(buffer *buf, buffer_char sch) {
    return insert_chars(buf, &sch, 1);
}
inline insert_result insert_char(buffer *buf, uint8_t uch) {
    buffer_char ch = {uch};
    return insert_chars(buf, &ch, 1);
}

insert_result insert_chars_right(buffer *buf, const buffer_char *chs, size_t count);

// TODO: Maximal efficiency: don't construct a delete_result on exactly the funcalls that don't use it.
struct [[nodiscard]] delete_result {
    // Cursor position _after_ deletion.
    size_t new_cursor;
    buffer_string deletedText;
    Side side;
    std::string error_message;
};
delete_result delete_left(buffer *buf, size_t count);

inline delete_result backspace_char(buffer *buf) {
    return delete_left(buf, 1);
}

delete_result delete_right(buffer *buf, size_t count);

inline delete_result delete_char(buffer *buf) {
    return delete_right(buf, 1);
}

void move_right_by(buffer *buf, size_t count);

inline void move_right(buffer *buf) {
    move_right_by(buf, 1);
}

void move_left_by(buffer *buf, size_t count);

inline void move_left(buffer *buf) {
    move_left_by(buf, 1);
}

void set_mark(buffer *buf);

// See movement.hpp for more.

}  // namespace qwi

#endif  // QWERTILLION_BUFFER_HPP_

