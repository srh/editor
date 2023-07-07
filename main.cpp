#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
// TODO: Remove unused includes^

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "error.hpp"

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
};

void display_tcattr(const struct termios& tcattr) {
    using ull = unsigned long long;
    printf("input: %llu, output: %llu, control: %llu, local: %llu, c_cc: ",
           (ull)tcattr.c_iflag, (ull)tcattr.c_oflag, (ull)tcattr.c_cflag, (ull)tcattr.c_lflag);

    for (size_t i = 0; i < NCCS; ++i) {
        printf("%c %llu", i == 0 ? '{' : ',', (ull)tcattr.c_cc[i]);
    }
    printf(" }\n");
}

int run_program(const command_line_args& args) {
    if (args.files.size() > 0) {
        fprintf(stderr, "File opening (on command line) not supported yet!\n");  // TODO
        return 1;
    }

    file_descriptor term{open("/dev/tty", O_RDWR)};
    runtime_check(term.fd != -1, "could not open tty: %s", runtime_check_strerror);

    struct termios tcattr;
    int res = tcgetattr(term.fd, &tcattr);
    runtime_check(res != -1, "could not get tcattr for tty: %s", runtime_check_strerror);

    display_tcattr(tcattr);

    printf("testing\n");
    fflush(stdout);
    usleep(500'000);

    res = term.close();  // TODO: EINTR, EAGAIN
    runtime_check(res != -1, "could not close tty: %s", runtime_check_strerror);

    return 0;
}
