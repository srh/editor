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

ui_result read_file(const fs::path& path, qwi::buffer_string *out) {
    static_assert(sizeof(qwi::buffer_char) == 1);
    qwi::buffer_string ret;
    // TODO: Use system lib at some point (like, when we care, if ever).
    std::ifstream f{path, std::ios::binary};
    if (f.fail()) {
        return ui_result::error("error opening file " + path.native());
    }
    f.seekg(0, std::ios::end);
    if (f.fail()) {
        return ui_result::error("error seeking to end of file " + path.native());
    }
    int64_t filesize = f.tellg();
    if (filesize == -1) {
        return ui_result::error("error reading file size of " + path.native());
    }
    if (filesize > SSIZE_MAX) {
        return ui_result::error("size of file " + path.native() + " is too big for this program");
    }

    // TODO: Use resize_and_overwrite (to avoid having to write memory).
    ret.resize(filesize);

    f.seekg(0);
    if (f.fail()) {
        return ui_result::error("could not seek back to beginning of file " + path.native());
    }

    f.read(as_chars(ret.data()), ret.size());
    if (f.fail()) {
        return ui_result::error("error reading file " + path.native());
    }

    *out = std::move(ret);
    return ui_result::success();
}
