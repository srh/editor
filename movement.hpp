#ifndef QWERTILLION_MOVEMENT_HPP_
#define QWERTILLION_MOVEMENT_HPP_

#include "buffer.hpp"

namespace qwi {

size_t forward_word_distance(const buffer *buf, const size_t cursor);
size_t backward_word_distance(const buffer *buf, const size_t cursor);

void move_forward_word(scratch_frame *scratch, ui_window_ctx *ui, buffer *buf);
void move_backward_word(scratch_frame *scratch, ui_window_ctx *ui, buffer *buf);

void move_up(scratch_frame *scratch, ui_window_ctx *ui, buffer *buf);
void move_down(scratch_frame *scratch, ui_window_ctx *ui, buffer *buf);
// TODO: Do these respect \n or visible line?
void move_home(scratch_frame *scratch, ui_window_ctx *ui, buffer *buf);
void move_end(scratch_frame *scratch, ui_window_ctx *ui, buffer *buf);

}  // namespace qwi

#endif  // QWERTILLION_MOVEMENT_HPP_
