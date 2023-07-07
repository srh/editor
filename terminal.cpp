#include "terminal.hpp"

#include <string.h>
#include <stdio.h>
#include <termios.h>

#include <utility>

#include "error.hpp"

void get_and_check_tcattr(int fd, struct termios *out) {
    int res = tcgetattr(fd, out);
    runtime_check(res != -1, "could not get tcattr for tty: %s", runtime_check_strerror);
}

terminal_restore::terminal_restore(file_descriptor *term) : tcattr(new struct termios), fd(term->fd) {
    runtime_check(fd != -1, "expecting an open terminal");
    get_and_check_tcattr(fd, tcattr.get());
}

void terminal_restore::restore() {
    runtime_check(fd != -1, "terminal_restore::restore called without file descriptor");
    int res = tcsetattr(fd, TCSAFLUSH, tcattr.get());
    fd = -1;
    runtime_check(res != -1, "could not set tcattr for tty: %s", runtime_check_strerror);
}

terminal_restore::~terminal_restore() {
    if (fd != -1) {
        try {
            restore();
        } catch (const runtime_check_failure&) {
            // Ignore
        }
    }
}

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

void set_raw_mode(int fd) {
    struct termios tcattr;
    get_and_check_tcattr(fd, &tcattr);
    // TODO: Consider enabling echoing (often) so that the user experiences instant
    // feedback on laggy ssh connections.  (And of course, that means designing the UI
    // code around this...)
    tcattr.c_iflag &= ~IXON;
    tcattr.c_oflag &= ~OPOST;
    tcattr.c_lflag &= ~(ICANON|ECHO|ISIG);
    // TODO: Consider VMIN set to 1.
    tcattr.c_cc[VMIN] = 0;
    tcattr.c_cc[VTIME] = 0;
    int res = tcsetattr(fd, TCSAFLUSH, &tcattr);
    runtime_check(res != -1, "could not set tcattr (to raw mode) for tty: %s", runtime_check_strerror);
}

void clear_screen() {
    printf("%s", TESC(2J));
    fflush(stdout);
}
