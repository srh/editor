#ifndef QWERTILLION_IO_HPP_
#define QWERTILLION_IO_HPP_

#include <unistd.h>

#include "error.hpp"

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

void write_cstring(int fd, const char *s);

#endif  // QWERTILLION_IO_HPP_
