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
    std::string error_message;  // "" or "Buffer is read only"
};

insert_result insert_chars(ui_window_ctx *ui, buffer *buf, const buffer_char *chs, size_t count);

inline insert_result insert_char(ui_window_ctx *ui, buffer *buf, buffer_char sch) {
    return insert_chars(ui, buf, &sch, 1);
}
inline insert_result insert_char(ui_window_ctx *ui, buffer *buf, uint8_t uch) {
    buffer_char ch = {uch};
    return insert_chars(ui, buf, &ch, 1);
}

insert_result insert_chars_right(ui_window_ctx *ui, buffer *buf, const buffer_char *chs, size_t count);

void force_insert_chars_end_before_cursor(ui_window_ctx *ui, buffer *buf,
                                          const buffer_char *chs, size_t count);

// TODO: Maximal efficiency: don't construct a delete_result on exactly the funcalls that don't use it.
struct [[nodiscard]] delete_result {
    // Cursor position _after_ deletion.
    size_t new_cursor;
    buffer_string deletedText;
    Side side;
    std::string error_message;
};
delete_result delete_left(ui_window_ctx *ui, buffer *buf, size_t count);

inline delete_result backspace_char(ui_window_ctx *ui, buffer *buf) {
    return delete_left(ui, buf, 1);
}

delete_result delete_right(ui_window_ctx *ui, buffer *buf, size_t count);

inline delete_result delete_char(ui_window_ctx *ui, buffer *buf) {
    return delete_right(ui, buf, 1);
}

void move_right_by(ui_window_ctx *ui, buffer *buf, size_t count);

inline void move_right(ui_window_ctx *ui, buffer *buf) {
    move_right_by(ui, buf, 1);
}

void move_left_by(ui_window_ctx *ui, buffer *buf, size_t count);

inline void move_left(ui_window_ctx *ui, buffer *buf) {
    move_left_by(ui, buf, 1);
}

void set_mark(ui_window_ctx *ui, buffer *buf);

// See movement.hpp for more.

}  // namespace qwi

#endif  // QWERTILLION_BUFFER_HPP_

