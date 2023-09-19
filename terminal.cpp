#include "terminal.hpp"

#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>

#include <optional>
#include <utility>

#include "error.hpp"
#include "io.hpp"
#include "util.hpp"

inline void assume_ASCII() { }  // Used wherever we assume the ASCII character set in
                                // terminal handling... hopefully with exhaustive
                                // coverage.

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
    // TODO: Figure out how 'emacs -nw' and 'vi' and make a window with the GUI terminal
    // having no scrollback.

    struct termios tcattr;
    get_and_check_tcattr(fd, &tcattr);
    // TODO: Consider enabling echoing (often) so that the user experiences instant
    // feedback on laggy ssh connections.  (And of course, that means designing the UI
    // code around this...)
    tcattr.c_iflag &= ~(IXON|ICRNL|INLCR);
    tcattr.c_oflag &= ~(OPOST|OCRNL|ONLCR);
    tcattr.c_lflag &= ~(ICANON|ECHO|ISIG);
    // TODO: Consider VMIN set to 0.
    tcattr.c_cc[VMIN] = 1;
    tcattr.c_cc[VTIME] = 0;
    int res = tcsetattr(fd, TCSAFLUSH, &tcattr);
    runtime_check(res != -1, "could not set tcattr (to raw mode) for tty: %s", runtime_check_strerror);
}

void clear_screen(int fd) {
    write_cstring(fd, TESC(2J));
}

terminal_size get_terminal_size(int fd) {
    // TODO: Handle SIGWINCH and update & redraw.

    struct winsize term_size;
    int res = ioctl(fd, TIOCGWINSZ, &term_size);
    runtime_check(res != -1, "could not get window size for tty: %s", runtime_check_strerror);

    runtime_check(term_size.ws_row > 0 && term_size.ws_col > 0, "terminal window size is zero");

    return terminal_size{term_size.ws_row, term_size.ws_col};
}

bool read_tty_char(int term_fd, char *out) {
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

void check_read_tty_char(int term_fd, char *out) {
    bool success = read_tty_char(term_fd, out);
    runtime_check(success, "zero-length read from tty configured with VMIN=1");
}

struct parsed_numeric_escape {
    uint8_t first;
    std::optional<uint8_t> second;
    char terminator;  // ~, A, B, C, D, for now
};

// Reads remainder of "\e[\d+(;\d+)?~" character escapes after the first digit was read.
bool read_tty_numeric_escape(int term, std::string *chars_read, char firstDigit, parsed_numeric_escape *out) {
    logic_checkg(isdigit(firstDigit));
    uint32_t number = firstDigit - '0';
    std::optional<uint8_t> first_number;

    for (;;) {
        char ch;
        check_read_tty_char(term, &ch);
        chars_read->push_back(ch);
        if (isdigit(ch)) {
            uint32_t new_number = number * 10 + (ch - '0');
            if (new_number > UINT8_MAX) {
                // TODO: We'd probably want to report this to the user somehow, or still
                // consume the entire escape code (for now we just render its characters.
                return false;
            }
            number = new_number;
        } else if (ch == '~' || ch == 'A' || ch == 'B' || ch == 'C' || ch == 'D') {
            if (first_number.has_value()) {
                out->first = *first_number;
                out->second = number;
            } else {
                out->first = number;
                out->second = std::nullopt;
            }
            out->terminator = ch;
            return true;
        } else if (ch == ';') {
            if (first_number.has_value()) {
                // TODO: We want to consume the whole keyboard escape code and ignore it together.
                return false;
            }
            // TODO: Should we enforce a digit after the first semicolon, or allow "\e[\d+;~" as the code does now?
            first_number = number;
            number = 0;
        } else {
            return false;
        }
    }
}

keypress_result read_tty_keypress(int term, std::string *chars_read_out) {
    // TODO: When term is non-blocking, we'll need to wait for readiness...?
    char ch;
    check_read_tty_char(term, &ch);

    using special_key = keypress::special_key;
    if (ch >= 32 && ch < 127) {
        return keypress::ascii(ch);
    }
    if (ch == '\t') {
        return keypress::special(special_key::Tab);
    }
    if (ch == '\r') {
        return keypress::special(special_key::Enter);
    }
    if (ch == 27) {
        chars_read_out->clear();
        std::string& chars_read = *chars_read_out;
        check_read_tty_char(term, &ch);
        chars_read.push_back(ch);
        // TODO: Handle all possible escapes...
        if (ch == '[') {
            check_read_tty_char(term, &ch);
            chars_read.push_back(ch);

            if (isdigit(ch)) {
                parsed_numeric_escape numbers;
                if (read_tty_numeric_escape(term, &chars_read, ch, &numbers)) {
                    special_key special = keypress::invalid_special();
                    switch (numbers.first) {
                    case 1: {
                        switch (numbers.terminator) {
                        case 'A': special = special_key::Up; break;
                        case 'B': special = special_key::Down; break;
                        case 'C': special = special_key::Right; break;
                        case 'D': special = special_key::Left; break;
                        default: break;
                        }
                    } break;
                    case 2: special = special_key::Insert; break;
                    case 3: special = special_key::Delete; break;
                    case 5: special = special_key::PageUp; break;
                    case 6: special = special_key::PageDown; break;
                    case 15: special = special_key::F5; break;
                    case 17: special = special_key::F6; break;
                    case 18: special = special_key::F7; break;
                    case 19: special = special_key::F8; break;
                    case 20: special = special_key::F9; break;
                    case 21: special = special_key::F10; break;
                        // TODO: F11
                    case 24: special = special_key::F12; break;
                    default:
                        break;
                    }

                    if (special != keypress::invalid_special()) {
                        if (!numbers.second.has_value()) {
                            return keypress::special(special, 0);
                        } else {
                            switch (*numbers.second) {
                            case 2:
                                return keypress::special(special, keypress::SHIFT);
                            case 3:
                                return keypress::special(special, keypress::META);
                            case 4:
                                return keypress::special(special, keypress::META | keypress::SHIFT);
                            case 5:
                                return keypress::special(special, keypress::CTRL);
                            case 6:
                                return keypress::special(special, keypress::CTRL | keypress::SHIFT);
                            case 7:
                                return keypress::special(special, keypress::CTRL | keypress::META);
                            default:
                                break;
                            }
                        }
                    }
                    // If special == 0, we fall through to incomplete_parse.
                }
            } else {
                switch (ch) {
                case 'C':
                    return keypress::special(special_key::Right);
                case 'D':
                    return keypress::special(special_key::Left);
                case 'A':
                    return keypress::special(special_key::Up);
                case 'B':
                    return keypress::special(special_key::Down);
                case 'H':
                    return keypress::special(special_key::Home);
                case 'F':
                    return keypress::special(special_key::End);
                case 'Z':
                    return keypress::special(special_key::Tab, keypress::SHIFT);
                case '[': {
                    // The Linux console uses these instead of \eOA-\eOD for F1-F4.
                    check_read_tty_char(term, &ch);
                    chars_read.push_back(ch);
                    switch (ch) {
                    case 'A': return keypress::special(special_key::F1); break;
                    case 'B': return keypress::special(special_key::F2); break;
                    case 'C': return keypress::special(special_key::F3); break;
                    case 'D': return keypress::special(special_key::F4); break;
                    case 'E': return keypress::special(special_key::F5); break;
                        // We stop here?
                    default:
                        break;
                    }
                } break;
                default:
                    break;
                }
            }
        } else {
            if (ch == 'O') {
                check_read_tty_char(term, &ch);
                chars_read.push_back(ch);
                switch (ch) {
                case 'P': return keypress::special(special_key::F1);
                case 'Q': return keypress::special(special_key::F2);
                case 'R': return keypress::special(special_key::F3);
                case 'S': return keypress::special(special_key::F4);
                default:
                    break;
                }
            } else if (ch == ('?' ^ CTRL_XOR_MASK)) {
                assume_ASCII();
                return keypress::special(special_key::Backspace, keypress::META);
            } else {
                if (32 <= ch && ch < 127) {
                    return keypress::ascii(ch, keypress::META);
                }
                if (uint8_t(ch) <= 127) {
                    // TODO: Meta+Ctrl characters.
                }
            }
        }

        // TODO: It's kind of silly that we set chars_read and then overwrite the value
        // again in the caller.
        return keypress_result::incomplete_parse(chars_read);
    }

    if (ch == 8) {
        return keypress::special(special_key::Backspace, keypress::CTRL);
    }

    if (uint8_t(ch) <= 127) {
        uint8_t maskch = uint8_t(ch) ^ CTRL_XOR_MASK;
        // Special case for backspace key (when ch == 127).
        if (maskch == '?') {
            assume_ASCII();
            return keypress::special(special_key::Backspace);
        }
        if (maskch == '@') {
            assume_ASCII();
            return keypress::ascii(' ', keypress::CTRL);  // Ctrl+Space same as C-@
        }
        if (maskch >= 'A' && maskch <= 'Z') {
            assume_ASCII();
            // TODO: Generally, here and elsewhere in terminal parsing, we have a strong
            // assumption of the ASCII character set.  What if there's an EBCDIC terminal?
            // How does that work?
            const uint8_t ALPHA_SHIFT_MASK = 32;
            return keypress::ascii(maskch ^ ALPHA_SHIFT_MASK, keypress::CTRL);
        }

        return keypress_result({ .value = maskch, .modmask = keypress::CTRL });
    } else {
        // TODO: Handle high characters -- do we just insert them, or do we validate
        // UTF-8, or what?
        return keypress_result({ .value = uint8_t(ch), .modmask = 0 });
    }
}

keypress_result read_tty_keypress(int term) {
    std::string chars_read;
    keypress_result ret = read_tty_keypress(term, &chars_read);
    ret.chars_read = std::move(chars_read);
    return ret;
}
