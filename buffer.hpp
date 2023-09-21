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

insert_result insert_chars(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, const buffer_char *chs, size_t count);

inline insert_result insert_char(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, buffer_char sch) {
    return insert_chars(scratch_frame, ui, buf, &sch, 1);
}
inline insert_result insert_char(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, uint8_t uch) {
    buffer_char ch = {uch};
    return insert_chars(scratch_frame, ui, buf, &ch, 1);
}

insert_result insert_chars_right(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, const buffer_char *chs, size_t count);

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
delete_result delete_left(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, size_t count);

inline delete_result backspace_char(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf) {
    return delete_left(scratch_frame, ui, buf, 1);
}

delete_result delete_right(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, size_t count);

inline delete_result delete_char(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf) {
    return delete_right(scratch_frame, ui, buf, 1);
}

void move_right_by(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, size_t count);

inline void move_right(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf) {
    move_right_by(scratch_frame, ui, buf, 1);
}

void move_left_by(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, size_t count);

inline void move_left(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf) {
    move_left_by(scratch_frame, ui, buf, 1);
}

void set_mark(ui_window_ctx *ui, buffer *buf);

// See movement.hpp for more.

}  // namespace qwi

#endif  // QWERTILLION_BUFFER_HPP_

