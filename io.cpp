#include "io.hpp"

#include <string.h>

void write_cstring(int fd, const char *s) {
    size_t count = strlen(s);
    ssize_t res = write(fd, s, count);
    // TODO: EAGAIN, EINTR
    runtime_check(res != -1, "write failed, error handling incomplete: %s", runtime_check_strerror);
}

