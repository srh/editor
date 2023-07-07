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

void draw_frame(int fd, const terminal_size& window) {
    for (size_t i = 0; i < window.rows; ++i) {
        size_t num = i % 52;
        char buf[] = "0\r\n";
        buf[0] = (num < 26 ? 'a' : 'A') + (num % 26);
        write_cstring(fd, buf);
        usleep(30'000);
    }
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
        write_cstring(term.fd, TESC(H));
        usleep(1'000'000);

        draw_frame(term.fd, size);
        usleep(1'000'000);

        term_restore.restore();
    }

    int res = term.close();  // TODO: EINTR, EAGAIN
    runtime_check(res != -1, "could not close tty: %s", runtime_check_strerror);

    return 0;
}
