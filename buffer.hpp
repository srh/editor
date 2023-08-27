#ifndef QWERTILLION_BUFFER_HPP_
#define QWERTILLION_BUFFER_HPP_

#include "state.hpp"

namespace qwi {

struct [[nodiscard]] insert_result {
    // Cursor position _after_ insertion
    size_t new_cursor;
    // Returned only to make implementing opposite(const undo_info&) easier.
    qwi::buffer_string insertedText;
    qwi::Side side;
};

insert_result insert_chars(qwi::buffer *buf, const qwi::buffer_char *chs, size_t count);

inline insert_result insert_char(qwi::buffer *buf, qwi::buffer_char sch) {
    return insert_chars(buf, &sch, 1);
}
inline insert_result insert_char(qwi::buffer *buf, char sch) {
    qwi::buffer_char ch = {uint8_t(sch)};
    return insert_chars(buf, &ch, 1);
}

insert_result insert_chars_right(qwi::buffer *buf, const qwi::buffer_char *chs, size_t count);

// TODO: Maximal efficiency: don't construct a delete_result on exactly the funcalls that don't use it.
struct [[nodiscard]] delete_result {
    // Cursor position _after_ deletion.
    size_t new_cursor;
    qwi::buffer_string deletedText;
    qwi::Side side;
};
delete_result delete_left(qwi::buffer *buf, size_t count);

inline delete_result backspace_char(qwi::buffer *buf) {
    return delete_left(buf, 1);
}

delete_result delete_right(qwi::buffer *buf, size_t count);

inline delete_result delete_char(qwi::buffer *buf) {
    return delete_right(buf, 1);
}

void move_right_by(qwi::buffer *buf, size_t count);

inline void move_right(qwi::buffer *buf) {
    move_right_by(buf, 1);
}

void move_left_by(qwi::buffer *buf, size_t count);

inline void move_left(qwi::buffer *buf) {
    move_left_by(buf, 1);
}

void set_mark(qwi::buffer *buf);

// See movement.hpp for more.

}  // namespace qwi

#endif  // QWERTILLION_BUFFER_HPP_

