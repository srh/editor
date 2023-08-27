#ifndef QWERTILLION_MOVEMENT_HPP_
#define QWERTILLION_MOVEMENT_HPP_

#include "buffer.hpp"

namespace qwi {

size_t forward_word_distance(const qwi::buffer *buf);
size_t backward_word_distance(const qwi::buffer *buf);

void move_forward_word(qwi::buffer *buf);
void move_backward_word(qwi::buffer *buf);

void move_up(qwi::buffer *buf);
void move_down(qwi::buffer *buf);
void move_home(qwi::buffer *buf);
void move_end(qwi::buffer *buf);

}  // namespace qwi

#endif  // QWERTILLION_MOVEMENT_HPP_
