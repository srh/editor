#include "io.hpp"

#include <string.h>

void write_data(int fd, const char *s, size_t count) {
    // TODO: Think about separating EINTR from EAGAIN.  I mean, make the fd non-blocking
    // first.  Revisit this.
 top:
    ssize_t res;
    do {
        res = write(fd, s, count);
    } while (res == -1 && (errno == EINTR || errno == EAGAIN));

    runtime_check(res != -1, "write failed, error handling incomplete: %s", runtime_check_strerror);
    runtime_check(res >= 0, "write system call returned invalid result");

    if (size_t(res) < count) {
        s += res;
        count -= res;
        goto top;
    }

}

void write_cstring(int fd, const char *s) {
    write_data(fd, s, strlen(s));
}

void close_fd(int fd) {
    int res = ::close(fd);
    // We don't retry on EINTR in this case because that's platform-dependent and Linux
    // could return EINTR while closing the fd.
    runtime_check(res != -1 || errno == EINTR, "close failed: %s", runtime_check_strerror);
}
