#include "io.hpp"

#include <string.h>

void write_data(int fd, const char *s, size_t count) {
    ssize_t res = write(fd, s, count);
    // TODO: EAGAIN, EINTR
    runtime_check(res != -1, "write failed, error handling incomplete: %s", runtime_check_strerror);
}

void write_cstring(int fd, const char *s) {
    write_data(fd, s, strlen(s));
}

