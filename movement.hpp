#ifndef QWERTILLION_MOVEMENT_HPP_
#define QWERTILLION_MOVEMENT_HPP_

#include "buffer.hpp"

namespace qwi {

size_t forward_word_distance(const buffer *buf);
size_t backward_word_distance(const buffer *buf);

void move_forward_word(ui_window_ctx *ui, buffer *buf);
void move_backward_word(ui_window_ctx *ui, buffer *buf);

void move_up(ui_window_ctx *ui, buffer *buf);
void move_down(ui_window_ctx *ui, buffer *buf);
// TODO: Do these respect \n or visible line?
void move_home(ui_window_ctx *ui, buffer *buf);
void move_end(ui_window_ctx *ui, buffer *buf);

}  // namespace qwi

#endif  // QWERTILLION_MOVEMENT_HPP_
