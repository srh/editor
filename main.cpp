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

#define p(x) { x, #x }

    // c_iflag
    printf("input: %llu=0", (ull)tcattr.c_iflag);
    for (std::pair<int, const char *> flag : { std::pair<int, const char *> p(IGNBRK), p(BRKINT), p(IGNPAR), p(PARMRK), p(INPCK), p(ISTRIP), p(INLCR), p(IGNCR), p(ICRNL), p(IUCLC), p(IXON), p(IXANY), p(IXOFF), p(IMAXBEL), p(IUTF8) }) {
        if (tcattr.c_iflag & flag.first) {
            printf("|%s", flag.second);
        }
    }

    // c_oflag
    printf(", output: %llu=0", (ull)tcattr.c_oflag);
    for (std::pair<int, const char *> flag : { std::pair<int, const char *> p(OPOST), p(OLCUC), p(ONLCR), p(OCRNL), p(ONOCR), p(ONLRET), p(OFILL), p(OFDEL), p(NLDLY), p(CRDLY), p(TABDLY), p(BSDLY), p(VTDLY), p(FFDLY) }) {
        if (tcattr.c_oflag & flag.first) {
            printf("|%s", flag.second);
        }
    }

    // c_cflag
    printf(", control: %llu=baud|size", (ull)tcattr.c_cflag);
    // LOBLK in man pages but not in POSIX or Linux.  CBAUD, CBAUDEX, CSIZE, are masks
    // used below, and CIBAUD seems to be zero on Linux; I _guess_ cfgetispeed is
    // identical to cfgetospeed.
    for (std::pair<int, const char *> flag : { std::pair<int, const char *> /*p(CBAUD), p(CBAUDEX), p(CSIZE), */p(CSTOPB), p(CREAD), p(PARENB), p(PARODD), p(HUPCL), p(CLOCAL) /*, p(LOBLK)*/, p(CIBAUD), p(CMSPAR), p(CRTSCTS) }) {
        if (tcattr.c_cflag & flag.first) {
            printf("|%s", flag.second);
        }
    }
    // input, output
    speed_t speeds[2] = { cfgetispeed(&tcattr), cfgetospeed(&tcattr) };
    int bauds[2] = { -1, -1 };
    for (size_t i = 0; i < 2; ++i) {
        speed_t speed = speeds[i];
        bauds[i] = speed;
#define bp(x) { x, B##x }
        for (std::pair<int, speed_t> pair : { std::pair<int, speed_t> bp(0), bp(50), bp(75), bp(110), bp(134), bp(150), bp(200), bp(300), bp(600), bp(1200), bp(1800), bp(2400), bp(4800), bp(9600), bp(19200), bp(38400), bp(57600), bp(115200), bp(230400) }) {
            if (pair.second == speed) {
                bauds[i] = pair.first;
            }
        }
#undef bp
    };
    int csize;
    switch (tcattr.c_cflag & CSIZE) {
    case CS5: csize = 5; break;
    case CS6: csize = 6; break;
    case CS7: csize = 7; break;
    case CS8: csize = 8; break;
    default:
        runtime_fail("tcattr.c_cflag unrecognized: %llu", (ull)(tcattr.c_cflag & CSIZE));
    }

    printf("(input baud=%d(#%llu), output baud=%d(#%llu), csize=%d)", bauds[0], (ull)speeds[0], bauds[1], (ull)speeds[1], csize);

    // c_lflag
    printf(", local: %llu=0", (ull)tcattr.c_lflag);
    // DEFECHO in manpages but not in POSIX or Linux
    for (std::pair<int, const char *> flag : { std::pair<int, const char *> p(ISIG), p(ICANON), p(XCASE), p(ECHO), p(ECHOE), p(ECHOK), p(ECHONL), p(ECHOCTL), p(ECHOPRT), p(ECHOKE) /*, p(DEFECHO)*/, p(FLUSHO), p(NOFLSH), p(TOSTOP), p(PENDIN), p(IEXTEN) }) {
        if (tcattr.c_lflag & flag.first) {
            printf("|%s", flag.second);
        }
    }

    printf(", c_cc: ");
    for (size_t i = 0; i < NCCS; ++i) {
        printf("%c %llu", i == 0 ? '{' : ',', (ull)tcattr.c_cc[i]);
    }
    printf(" }\n");

    printf("c_cc again: ");
    int count = 0;
    // VDSUSP, VSTATUS, VSWTCH not in POSIX or Linux
    for (std::pair<int, const char *> pair : { std::pair<int, const char *> p(VDISCARD)/*, p(VDSUSP)*/, p(VEOF), p(VEOL), p(VEOL2), p(VERASE), p(VINTR), p(VKILL), p(VLNEXT), p(VMIN), p(VQUIT), p(VREPRINT), p(VSTART)/*, p(VSTATUS)*/, p(VSTOP), p(VSUSP)/*, p(VSWTCH)*/, p(VTIME), p(VWERASE) }) {
        printf("%c [%s (%d)]=%llu", count++ == 0 ? '{' : ',', pair.second, pair.first, (ull)tcattr.c_cc[pair.first]);
    }
    printf("}\n");

#undef p
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
