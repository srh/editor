#ifndef QWERTILLION_TERMINAL_HPP_
#define QWERTILLION_TERMINAL_HPP_

#include <memory>

#include <stdint.h>

#include "error.hpp"
#include "keyboard.hpp"
#include "terminal_size.hpp"

// We _don't_ include <termios.h> in this header, to avoid all the macro pollution.
struct termios;

struct file_descriptor;

#define TERMINAL_ESCAPE_SEQUENCE "\x1b["
#define TESC(x) TERMINAL_ESCAPE_SEQUENCE #x

void display_tcattr(const struct termios& tcattr);

void set_raw_mode(int fd);
void clear_screen(int fd);

terminal_size get_terminal_size(int fd);

struct terminal_restore {
    // Unchanged -- original value of termios.
    std::unique_ptr<struct termios> tcattr;
    // -1 after restore() called.
    int fd;

    explicit terminal_restore(file_descriptor *term_descriptor);
    ~terminal_restore();

    void restore();

    NO_COPY(terminal_restore);
};

void check_read_tty_char(int term_fd, char *out);
keypress_result read_tty_keypress(int term);

#endif  // QWERTILLION_TERMINAL_HPP_
