#ifndef QWERTILLION_TERMINAL_HPP_
#define QWERTILLION_TERMINAL_HPP_

#include <memory>

#include "error.hpp"

// We _don't_ include <termios.h> in this header, to avoid all the macro pollution.
struct termios;

void display_tcattr(const struct termios& tcattr);

// TODO: Move to own file.
struct file_descriptor {
    int fd = -1;

    ~file_descriptor() {
        if (fd != -1) {
            int discard = ::close(fd);
            (void)discard;
            fd = -1;
        }
    }
    int close() {
        int ret = ::close(fd);
        fd = -1;
        return ret;
    }

    file_descriptor() = default;
    explicit file_descriptor(int _fd) : fd(_fd) { }

    NO_COPY(file_descriptor);
};

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

#endif  // QWERTILLION_TERMINAL_HPP_
