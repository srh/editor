#ifndef QWERTILLION_BUFFER_HPP_
#define QWERTILLION_BUFFER_HPP_

#include "state.hpp"

void insert_chars(qwi::buffer *buf, const qwi::buffer_char *chs, size_t count);

inline void insert_char(qwi::buffer *buf, qwi::buffer_char sch) {
    insert_chars(buf, &sch, 1);
}
inline void insert_char(qwi::buffer *buf, char sch) {
    qwi::buffer_char ch = {uint8_t(sch)};
    insert_chars(buf, &ch, 1);
}

void delete_left(qwi::buffer *buf, size_t count);

inline void backspace_char(qwi::buffer *buf) {
    delete_left(buf, 1);
}

void delete_right(qwi::buffer *buf, size_t count);

inline void delete_char(qwi::buffer *buf) {
    delete_right(buf, 1);
}

void kill_line(qwi::buffer *buf);

void move_right_by(qwi::buffer *buf, size_t count);

inline void move_right(qwi::buffer *buf) {
    move_right_by(buf, 1);
}

void move_left_by(qwi::buffer *buf, size_t count);

inline void move_left(qwi::buffer *buf) {
    move_left_by(buf, 1);
}

void move_forward_word(qwi::buffer *buf);
void move_backward_word(qwi::buffer *buf);

void move_up(qwi::buffer *buf);
void move_down(qwi::buffer *buf);
void move_home(qwi::buffer *buf);
void move_end(qwi::buffer *buf);
void set_mark(qwi::buffer *buf);

#endif  // QWERTILLION_BUFFER_HPP_

