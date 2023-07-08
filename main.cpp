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

struct terminal_frame {
    // Carries the presumed window size that the frame was rendered for.
    terminal_size window;
    // Cursor pos (0..<window.*)
    uint32_t cursor_y = 0;
    uint32_t cursor_x = 0;
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
    ret.cursor_y = step % window.rows;
    ret.cursor_x = (step * 3) % window.cols;

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
    std::string cursor_string = TERMINAL_ESCAPE_SEQUENCE + std::to_string(frame.cursor_y + 1) + ';';
    cursor_string += std::to_string(frame.cursor_x + 1);
    cursor_string += 'H';
    write_data(fd, cursor_string.data(), cursor_string.size());
    write_cstring(fd, TESC(?25h));
}

void draw_frame(int fd, const terminal_size& window, size_t step) {
    terminal_frame frame = render_frame(window, step);

    write_frame(fd, frame);
}

int run_program(const command_line_args& args) {
    if (args.files.size() > 0) {
        fprintf(stderr, "File opening (on command line) not supported yet!\n");  // TODO
        return 1;
    }

    file_descriptor term{open("/dev/tty", O_RDWR)};
    runtime_check(term.fd != -1, "could not open tty: %s", runtime_check_strerror);

    {
        // TODO: We might have other needs to restore the terminal... like if we get Ctrl+Z'd...(?)
        terminal_restore term_restore(&term);

        display_tcattr(*term_restore.tcattr);

        set_raw_mode(term.fd);

        struct terminal_size size = get_terminal_size(term.fd);

        printf("testing\n");
        printf("testing (crlf)\r\n");
        fflush(stdout);

        clear_screen(term.fd);

        for (size_t step = 0; step < 3; ++step) {
            draw_frame(term.fd, size, step);
            usleep(2'000'000);
        }

        // TODO: Clear screen on exception exit too.
        clear_screen(term.fd);
        term_restore.restore();
    }

    int res = term.close();  // TODO: EINTR, EAGAIN
    runtime_check(res != -1, "could not close tty: %s", runtime_check_strerror);

    return 0;
}
