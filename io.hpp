#ifndef QWERTILLION_IO_HPP_
#define QWERTILLION_IO_HPP_

#include <filesystem>

#include <unistd.h>

#include "chars.hpp"
#include "error.hpp"

void write_data(int fd, const char *s, size_t count);
void write_cstring(int fd, const char *s);
void close_fd(int fd);
// TODO: Violates our "only one use of qwi:: prefix" rule.
// TODO: Also, UI logic (with ui_result)
ui_result read_file(const std::filesystem::path& path, qwi::buffer_string *out);

struct file_descriptor {
    int fd = -1;

    ~file_descriptor() {
        if (fd != -1) {
            int discard = ::close(fd);
            (void)discard;
            fd = -1;
        }
    }
    void close() noexcept(false) {
        close_fd(fd);
        fd = -1;
    }

    file_descriptor() = default;
    explicit file_descriptor(int _fd) : fd(_fd) { }

    NO_COPY(file_descriptor);
};

#endif  // QWERTILLION_IO_HPP_
