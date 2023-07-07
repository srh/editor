#ifndef QWERTILLION_TERMINAL_HPP_
#define QWERTILLION_TERMINAL_HPP_

struct termios;

void display_tcattr(const struct termios& tcattr);

#endif  // QWERTILLION_TERMINAL_HPP_
