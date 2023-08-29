#include "io.hpp"

#include <limits.h>
#include <string.h>

#include <fstream>

namespace fs = std::filesystem;

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

qwi::buffer_string read_file(const fs::path& path) {
    if (!fs::is_regular_file(path)) {
        runtime_fail("Tried opening non-regular file %s", path.c_str());
    }

    static_assert(sizeof(qwi::buffer_char) == 1);
    qwi::buffer_string ret;
    // TODO: Use system lib at some point (like, when we care, if ever).
    std::ifstream f{path, std::ios::binary};
    f.seekg(0, std::ios::end);
    runtime_check(!f.fail(), "error seeking to end of file %s", path.c_str());
    int64_t filesize = f.tellg();
    runtime_check(filesize != -1, "error reading file size of %s", path.c_str());
    runtime_check(filesize <= SSIZE_MAX, "Size of file %s is too big", path.c_str());
    // TODO: Use resize_and_overwrite (to avoid having to write memory).
    ret.resize(filesize);
    f.seekg(0);
    runtime_check(!f.fail(), "error seeking back to beginning of file %s", path.c_str());

    f.read(as_chars(ret.data()), ret.size());
    runtime_check(!f.fail(), "error reading file %s", path.c_str());

    return ret;
}
