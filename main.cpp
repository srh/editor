#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
// TODO: Remove unused includes^

#include <string>
#include <vector>

#include "error.hpp"
#include "io.hpp"
#include "state.hpp"
#include "terminal.hpp"

struct command_line_args {
    bool version = false;
    bool help = false;

    std::vector<std::string> files;
};

bool parse_command_line(FILE *err_fp, int argc, const char **argv, command_line_args *out) {
    // TODO: We could check for duplicate or conflicting args (like --help and --version
    // used together with other args).
    int i = 1;
    *out = command_line_args{};
    while (i < argc) {
        const char *arg = argv[i];
        if (0 == strcmp(arg, "--version")) {
            out->version = true;
            ++i;
        } else if (0 == strcmp(arg, "--help")) {
            out->help = true;
            ++i;
        } else if (0 == strcmp(arg, "--")) {
            ++i;
            while (i < argc) {
                out->files.emplace_back(argv[i]);
                ++i;
            }
        } else if (arg[0] == '-') {
            fprintf(err_fp, "Invalid argument '%s'.  See --help for usage.\n", arg);
            return false;
        } else {
            out->files.emplace_back(arg);
            ++i;
        }
    }

    return true;
}

void print_version(FILE *fp) {
    const char *PRODUCT_NAME = "Qwertillion";
    const char *PRODUCT_VERSION = "0.0.0.epsilon";
    fprintf(fp, "%s %s\n", PRODUCT_NAME, PRODUCT_VERSION);
}

void print_help(FILE *fp) {
    print_version(fp);
    fprintf(fp, "Usage: --help | --version | [files...] [-- files..]\n");
}

int run_program(const command_line_args& args);

int main(int argc, const char **argv) {
    command_line_args args;
    if (!parse_command_line(stderr, argc, argv, &args)) {
        return 2;
    }

    FILE *help_fp = stdout;
    if (args.help) {
        print_help(help_fp);
        return 0;
    }

    if (args.version) {
        print_version(help_fp);
        return 0;
    }

    try {
        return run_program(args);
    } catch (const runtime_check_failure& exc) {
        (void)exc;  // No info in exc.
        return 1;
    }
}

uint32_t u32_mul(uint32_t x, uint32_t y) {
    // TODO: Throw upon overflow.
    return x * y;
}

size_t size_mul(size_t x, size_t y) {
    // TODO: Throw upon overflow.
    return x * y;
}
size_t size_add(size_t x, size_t y) {
    // TODO: Throw upon overflow.
    return x + y;
}

struct terminal_frame {
    // Carries the presumed window size that the frame was rendered for.
    terminal_size window;
    // Cursor pos (0..<window.*)

    // nullopt means it's invisible.
    struct cursor_pos { uint32_t row = 0, col = 0; };
    std::optional<cursor_pos> cursor;

    // data.size() = u32_mul(window.rows, window.cols).
    std::vector<char> data;
};

terminal_frame init_frame(const terminal_size& window) {
    terminal_frame ret;
    ret.data.resize(u32_mul(window.rows, window.cols));
    ret.window = window;
    return ret;
}

terminal_frame render_frame(const terminal_size& window, size_t step) {
    terminal_frame ret = init_frame(window);
    ret.cursor = terminal_frame::cursor_pos{
        .row = uint32_t(step % window.rows),
        .col = uint32_t((step * 3) % window.cols),
    };

    for (size_t i = 0; i < ret.data.size(); ++i) {
        char num = size_mul(i, step) % 61;
        ret.data[i] = (num < 10 ? '0' : num < 36 ? 'a' - 10 : 'A' - 36) + num;
    }

    return ret;
}

void write_frame(int fd, const terminal_frame& frame) {
    // TODO: Either single buffered write or some minimal diff write.
    write_cstring(fd, TESC(?25l));
    write_cstring(fd, TESC(H));
    for (size_t i = 0; i < frame.window.rows; ++i) {
        write_data(fd, &frame.data[i * frame.window.cols], frame.window.cols);
        if (i < frame.window.rows - 1) {
            write_cstring(fd, "\r\n");
        }
    }
    if (frame.cursor.has_value()) {
        std::string cursor_string = TERMINAL_ESCAPE_SEQUENCE + std::to_string(frame.cursor->row + 1) + ';';
        cursor_string += std::to_string(frame.cursor->col + 1);
        cursor_string += 'H';
        write_data(fd, cursor_string.data(), cursor_string.size());
        // TODO: Make cursor visible when exiting program.
        write_cstring(fd, TESC(?25h));
    }
}

void draw_frame(int fd, const terminal_size& window, size_t step) {
    terminal_frame frame = render_frame(window, step);

    write_frame(fd, frame);
}

void draw_empty_frame_for_exit(int fd, const terminal_size& window) {
    terminal_frame frame = init_frame(window);
    for (size_t i = 0; i < frame.data.size(); ++i) {
        frame.data[i] = ' ';
    }
    // TODO: Ensure cursor is restored on non-happy-paths.
    frame.cursor = {0, 0};

    write_frame(fd, frame);
}

qwi::state initial_state(const command_line_args& args) {
    runtime_check(args.files.size() == 0,
                  "file opening (on command line) not supported (yet!)");  // TODO
    qwi::state state;
    state.bufs.push_back(qwi::buffer{});
    state.bufs.back().name = "*scratch*";
    return state;
}

void redraw_state(int term, const terminal_size& window, const qwi::state& state) {
    terminal_frame frame = init_frame(window);

    // First find the front of the row of our first_visible_offset.  (Which usually is
    // equal, unless the window was resized.)

    // TODO: Couldn't we traverse backwards?  Yeah, but maybe we'll change the data
    // structure anyway.
    size_t column = 0;
    size_t front_of_row = 0;
    for (size_t i = 0; i < state.buf.first_visible_offset; ++i) {
        uint8_t ch = uint8_t(state.buf[i]);
        // TODO: We dup this logic below, which is gross.
        if (ch == '\n') {
            column = 0;
            front_of_row = i + 1;
        } else if (ch == '\t') {
            column = size_add(column | 7, 1);
            column = (column >= window.cols ? 0 : column);
        } else if (ch < 32 || ch == 127) {
            if (column > window.cols - 2) {
                column = 2;
            } else {
                column += 2;
                column = (column == window.cols ? 0 : column);
            }
        } else {
            ++column;
            column = (column == window.cols ? 0 : column);
        }
    }

    // Because we don't support window resize (yet).
    const bool window_was_resized = false;
    // Check if first_visible_offset is invalid.
    if (!window_was_resized) {
        runtime_check(column == 0, "first_visible_offset should be rendered at the first column");
    }

    size_t i = front_of_row;
    // TODO: Update state.buf.first_visible_offset at some point... only when we
    // manually scroll or type text.

    // Now we render.
    size_t row = 0;
    size_t col = 0;
    while (row < window.rows && i < state.buf.size()) {
        if (i == state.buf.cursor()) {
            frame.cursor = { .row = uint32_t(row), .col = uint32_t(col) };
        }
        uint8_t ch = uint8_t(state.buf[i]);
        if (ch == '\n') {
            // TODO: We could use '\x1bK'
            // clear to EOL
            do {
                frame.data[row * window.cols + col] = ' ';
                ++col;
            } while (col < window.cols);
            ++row;
            col = 0;
        } else if (ch == '\t') {
            // clear to EOL
            do {
                frame.data[row * window.cols + col] = ' ';
                ++col;
            } while (col < window.cols && (col & 7) != 0);
            if (col == window.cols) {
                col = 0;
                ++row;
            }
        } else if (ch < 32 || ch == 127) {
            if (col > window.cols - 2) {
                // Just one cell to clear to EOL.
                frame.data[row * window.cols + col] = ' ';
                ++row;
                col = 0;
                if (row == window.rows) {
                    break;
                }
            }
            frame.data[row * window.cols + col] = '^';
            ++col;
            frame.data[row * window.cols + col] = (ch ^ 64);
            ++col;
        } else {
            // I guess 128-255 get rendered verbatim.
            frame.data[row * window.cols + col] = ch;
            ++col;
            if (col == window.cols) {
                col = 0;
                ++row;
            }
        }
        ++i;
    }

    // Case where cursor is at the end of the buf.
    if (row < window.rows && i == state.buf.cursor()) {
        frame.cursor = { .row = uint32_t(row), .col = uint32_t(col) };
    }

    // We have to fill the rest of the screen.
    for (size_t j = row * window.cols + col, e = window.rows * window.cols; j < e; ++j) {
        frame.data[j] = ' ';
    }

    write_frame(term, frame);
}

void insert_char(qwi::buffer *buf, char sch) {
    buf->bef.push_back(sch);
}
void backspace_char(qwi::buffer *buf) {
    if (!buf->bef.empty()) {
        buf->bef.pop_back();
    }
}
void delete_char(qwi::buffer *buf) {
    // erase checks if (!buf->aft.empty()).
    buf->aft.erase(0, 1);
}

void move_right(qwi::buffer *buf) {
    if (buf->aft.empty()) {
        return;
    }
    buf->bef.push_back(buf->aft.front());
    buf->aft.erase(0, 1);
}

void move_left(qwi::buffer *buf) {
    if (buf->bef.empty()) {
        return;
    }
    buf->aft.insert(buf->aft.begin(), buf->bef.back());
    buf->bef.pop_back();
}

void push_printable_repr(std::string *str, char sch) {
    uint8_t ch = uint8_t(sch);
    if (ch == '\n' || ch == '\t') {
        str->push_back(ch);
    } else if (ch < 32 || ch > 126) {
        str->push_back('\\');
        str->push_back('x');
        const char *hex = "0123456789abcdef";
        str->push_back(hex[ch / 16]);
        str->push_back(hex[ch % 16]);
    } else {
        str->push_back(ch);
    }
}

bool readTtyChar(int term_fd, char *out) {
    char readbuf[1];
    ssize_t res;
    do {
        res = read(term_fd, readbuf, 1);
    } while (res == -1 && errno == EINTR);

    // TODO: Of course, we'd want to auto-save the file upon this and all sorts of exceptions.
    runtime_check(res != -1 || errno == EAGAIN, "unexpected error on terminal read: %s", runtime_check_strerror);

    if (res != 0) {
        *out = readbuf[0];
        return true;
    }
    return false;
}

void main_loop(int term, const command_line_args& args) {
    qwi::state state = initial_state(args);

    terminal_size window = get_terminal_size(term);
    redraw_state(term, window, state);

    bool exit = false;
    for (; !exit; ) {
        // TODO: When term is non-blocking, we'll need to wait for readiness...?
        char ch;
        bool success = readTtyChar(term, &ch);
        runtime_check(success, "zero-length read from tty configured with VMIN=1");
        // TODO: Named constants for these keyboard keys and such.
        // TODO: Implement scrolling to cursor upon all buffer manipulations.
        if (ch == 13) {
            insert_char(&state.buf, '\n');
        } else if (ch == '\t' || (ch >= 32 && ch < 127)) {
            insert_char(&state.buf, ch);
        } else if (ch == 28) {
            // Ctrl+backslash
            exit = true;
            // TODO: Drop exit var and just break; here?  We have a spurious redraw.  Or just abort?
        } else if (ch == 127) {
            // Backspace.
            backspace_char(&state.buf);
        } else if (ch == 8) {
            // Delete.
            delete_char(&state.buf);
        } else if (ch == 27) {
            success = readTtyChar(term, &ch);
            runtime_check(success, "zero-length read from tty configured with VMIN=1");  // TODO: helper method
            // TODO: Handle all possible escapes...
            if (ch == '[') {
                success = readTtyChar(term, &ch);
                runtime_check(success, "zero-length read from tty configured with VMIN=1");  // TODO: helper method
                if (ch == 'C') {
                    move_right(&state.buf);
                } else if (ch == 'D') {
                    move_left(&state.buf);
                } else {
                    // TODO: Handle all possible escapes...
                }
            }
        } else {
            // TODO: Handle other possible control chars.
        }

        terminal_size window = get_terminal_size(term);
        redraw_state(term, window, state);
    }
}

int run_program(const command_line_args& args) {
    file_descriptor term{open("/dev/tty", O_RDWR)};
    runtime_check(term.fd != -1, "could not open tty: %s", runtime_check_strerror);

    {
        // TODO: We might have other needs to restore the terminal... like if we get Ctrl+Z'd...(?)
        terminal_restore term_restore(&term);

        // TODO: Log this in some debug log.
        display_tcattr(*term_restore.tcattr);

        set_raw_mode(term.fd);

        clear_screen(term.fd);

        main_loop(term.fd, args);

        // TODO: Clear screen on exception exit too.
        struct terminal_size window = get_terminal_size(term.fd);
        draw_empty_frame_for_exit(term.fd, window);
        clear_screen(term.fd);
        write_cstring(term.fd, TESC(H));
        term_restore.restore();
    }

    term.close();

    return 0;
}
