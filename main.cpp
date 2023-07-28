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

qwi::state initial_state(const command_line_args& args, const terminal_size& window) {
    runtime_check(args.files.size() == 0,
                  "file opening (on command line) not supported (yet!)");  // TODO
    qwi::state state;
    state.buf.set_window(qwi::window_size{.rows = window.rows, .cols = window.cols});
    return state;
}

// Returns true if not '\n'.  Sets *line_col in any case.  Calls emit_drawn_chars(char *,
// size_t) once to pass out chars to be rendered in the terminal (except when a newline is
// encountered).  Always passes a count of 1 or greater to emit_drawn_chars.
template <class C>
bool compute_char_rendering(const uint8_t ch,
                            size_t *line_col, C&& emit_drawn_chars) {
    if (ch == '\n') {
        *line_col = 0;
        return false;
    }
    if (ch == '\t') {
        size_t next_line_col = size_add((*line_col) | 7, 1);
        //             12345678
        char buf[8] = { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
        emit_drawn_chars(buf, next_line_col - *line_col);
        *line_col = next_line_col;
        return true;
    }
    if (ch < 32 || ch == 127) {
        char buf[2] = { '^', char(ch ^ 64) };
        emit_drawn_chars(buf, 2);
        *line_col += 2;
        return true;
    }
    // I guess 128-255 get rendered verbatim.
    // Making this a 1-element array is my own neurosis.
    char buf[1] = { char(ch) };
    emit_drawn_chars(buf, 1);
    ++*line_col;
    return true;
}

size_t current_column(const qwi::buffer& buf) {
    size_t line_col = 0;
    bool saw_newline = false;
    const size_t cursor = buf.cursor();
    for (size_t i = cursor - buf.cursor_distance_to_beginning_of_line(); i < cursor; ++i) {
        uint8_t ch = uint8_t(buf[i]);
        saw_newline |= !compute_char_rendering(ch, &line_col, [](const char *, size_t) { });
    }
    runtime_check(!saw_newline, "encountered impossible newline in current_column");

    return line_col;
}

void redraw_state(int term, const terminal_size& window, const qwi::state& state) {
    terminal_frame frame = init_frame(window);

    if (window.cols < 2 || window.rows == 0) {
        // TODO: This dups code at the bottom of this fn horribly.
        write_frame(term, frame);
        return;
    }

    // first_visible_offset is the first rendered character in the buffer -- this may be a
    // tab character or 2-column-rendered control character, only part of which was
    // rendered.  We render the entire line first_visible_offset was part of, and
    // copy_row_if_visible conditionally copies the line into the `frame` for rendering --
    // taking care to call it before incrementing i for partially rendered characters, and
    // _after_ incrementing i for the completely rendered character.

    // TODO: We actually don't want to re-render a whole line
    size_t i = state.buf.first_visible_offset - distance_to_beginning_of_line(state.buf, state.buf.first_visible_offset);

    std::vector<char> render_row(window.cols, 0);
    size_t render_cursor = render_row.size();  // Means no cursor on this row.
    size_t line_col = 0;
    size_t col = 0;
    size_t row = 0;
    // This gets called after we paste our character into the row and i is the offset
    // after the last completely written character.  Called precisely when col ==
    // window.cols.
    auto copy_row_if_visible = [&]() {
        if (i > state.buf.first_visible_offset) {
            // It simplifies code to throw in this (row < window.rows) check here, instead
            // of carefully calculating where we might need to check it.
            if (row < window.rows) {
                memcpy(&frame.data[row * window.cols], render_row.data(), window.cols);
                if (render_cursor != render_row.size()) {
                    frame.cursor = { .row = uint32_t(row), .col = uint32_t(render_cursor) };
                    render_cursor = render_row.size();
                }
            }
            ++row;
            col = 0;
        }
    };
    while (row < window.rows && i < state.buf.size()) {
        // col < window.cols.
        if (i == state.buf.cursor()) {
            render_cursor = col;
        }

        uint8_t ch = uint8_t(state.buf[i]);

        bool res = compute_char_rendering(ch, &line_col, [&](const char *buf, size_t count) {
            // Always, count > 0.
            for (size_t j = 0; j < count - 1; ++j) {
                render_row[col] = buf[j];
                ++col;
                if (col == window.cols) {
                    copy_row_if_visible();
                }
            }
            render_row[col] = buf[count - 1];
            ++col;
            ++i;
            if (col == window.cols) {
                copy_row_if_visible();
            }
        });
        if (!res) {
            // TODO: We could use '\x1bK'
            // clear to EOL
            do {
                render_row[col] = ' ';
                ++col;
            } while (col < window.cols);
            ++i;
            copy_row_if_visible();
        }
    }

    if (i == state.buf.cursor()) {
        render_cursor = col;
    }

    // If we reached end of buffer, we might still need to copy the current render_row and
    // the remaining screen.
    while (row < window.rows) {
        do {
            render_row[col] = ' ';
            ++col;
        } while (col < window.cols);
        memcpy(&frame.data[row * window.cols], render_row.data(), window.cols);
        if (render_cursor != render_row.size()) {
            frame.cursor = { .row = uint32_t(row), .col = uint32_t(render_cursor) };
            render_cursor = render_row.size();
        }
        ++row;
        col = 0;
    }

    // TODO: Early-bailout at top of function duplicates this.
    write_frame(term, frame);
}

void insert_char(qwi::buffer *buf, char sch) {
    buf->bef.push_back(sch);
    // TODO: Don't recompute virtual_column every time.
    buf->virtual_column = current_column(*buf);
}
// Cheap fn for debugging purposes.
void push_printable_repr(std::string *str, char sch);
void insert_printable_repr(qwi::buffer *buf, char sch) {
    push_printable_repr(&buf->bef, sch);
    buf->virtual_column = current_column(*buf);
}
void backspace_char(qwi::buffer *buf) {
    if (!buf->bef.empty()) {
        buf->bef.pop_back();
    }
    buf->virtual_column = current_column(*buf);
}
void delete_char(qwi::buffer *buf) {
    // erase checks if (!buf->aft.empty()).
    buf->aft.erase(0, 1);
    // TODO: We don't do this for doDeleteRight (or doAppendRight) in jsmacs -- the bug is in jsmacs!
    buf->virtual_column = current_column(*buf);
}

void move_right(qwi::buffer *buf) {
    if (buf->aft.empty()) {
        return;
    }
    buf->bef.push_back(buf->aft.front());
    buf->aft.erase(0, 1);
    buf->virtual_column = current_column(*buf);
}

void move_left(qwi::buffer *buf) {
    if (buf->bef.empty()) {
        return;
    }
    buf->aft.insert(buf->aft.begin(), buf->bef.back());
    buf->bef.pop_back();
    buf->virtual_column = current_column(*buf);
}

void move_up(qwi::buffer *buf) {
    // TODO: This virtual_column logic doesn't work with tab characters.
    // TODO: This is a bit convoluted because it's based on jsmacs code.
    size_t c = buf->virtual_column;
    size_t bolPos = buf->cursor() - qwi::distance_to_beginning_of_line(*buf, buf->cursor());
    size_t leftBolPos = bolPos - (bolPos != 0);
    // end of previous line, or our line if we're on the first line.
    size_t endOfLine = leftBolPos + qwi::distance_to_eol(*buf, leftBolPos);
    size_t d = qwi::distance_to_beginning_of_line(*buf, endOfLine);
    size_t begOfLine = endOfLine - qwi::distance_to_beginning_of_line(*buf, endOfLine);
    size_t nextPos = begOfLine + std::min(c, d);
    buf->set_cursor(nextPos);
}

void move_down(qwi::buffer *buf) {
    // TODO: This virtual_column logic doesn't work with tab characters.
    size_t c = buf->virtual_column;
    // position of next newline (or: end of buffer):
    size_t eolPos = buf->cursor() + qwi::distance_to_eol(*buf, buf->cursor());
    // beginning of next line (or: end of buffer):
    size_t nextLinePos = eolPos + (eolPos != buf->size());
    // size of next line (or: zero)
    size_t d = qwi::distance_to_eol(*buf, nextLinePos);
    size_t nextPos = nextLinePos + std::min(c, d);
    buf->set_cursor(nextPos);
}

void move_home(qwi::buffer *buf) {
    size_t bolPos = buf->cursor() - qwi::distance_to_beginning_of_line(*buf, buf->cursor());
    buf->set_cursor(bolPos);
    buf->virtual_column = current_column(*buf);
}

void move_end(qwi::buffer *buf) {
    size_t eolPos = buf->cursor() + qwi::distance_to_eol(*buf, buf->cursor());
    buf->set_cursor(eolPos);
    buf->virtual_column = current_column(*buf);
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

void read_and_process_tty_input(int term, qwi::state *state, bool *exit_loop) {
    // TODO: When term is non-blocking, we'll need to wait for readiness...?
    char ch;
    check_read_tty_char(term, &ch);
    // TODO: Named constants for these keyboard keys and such.
    // TODO: Implement scrolling to cursor upon all buffer manipulations.
    if (ch == 13) {
        insert_char(&state->buf, '\n');
    } else if (ch == '\t' || (ch >= 32 && ch < 127)) {
        insert_char(&state->buf, ch);
    } else if (ch == 28) {
        // Ctrl+backslash
        *exit_loop = true;
        // TODO: Drop exit var and just break; here?  We have a spurious redraw.  Or just abort?
    } else if (ch == 127) {
        // Backspace.
        backspace_char(&state->buf);
    } else if (ch == 27) {
        std::string chars_read;
        check_read_tty_char(term, &ch);
        chars_read.push_back(ch);
        // TODO: Handle all possible escapes...
        if (ch == '[') {
            check_read_tty_char(term, &ch);
            chars_read.push_back(ch);

            if (ch == 'C') {
                move_right(&state->buf);
                chars_read.clear();
            } else if (ch == 'D') {
                move_left(&state->buf);
                chars_read.clear();
            } else if (ch == 'A') {
                move_up(&state->buf);
                chars_read.clear();
            } else if (ch == 'B') {
                move_down(&state->buf);
                chars_read.clear();
            } else if (ch == 'H') {
                move_home(&state->buf);
                chars_read.clear();
            } else if (ch == 'F') {
                move_end(&state->buf);
                chars_read.clear();
            } else if (isdigit(ch)) {
                // TODO: Generic parsing of numeric/~ escape codes.
                if (ch == '3') {
                    check_read_tty_char(term, &ch);
                    chars_read.push_back(ch);
                    if (ch == '~') {
                        delete_char(&state->buf);
                        chars_read.clear();
                    } else if (ch == ';') {
                        check_read_tty_char(term, &ch);
                        chars_read.push_back(ch);
                        if (ch == '2') {
                            check_read_tty_char(term, &ch);
                            chars_read.push_back(ch);
                            if (ch == '~') {
                                // TODO: Handle Shift+Del key.
                                chars_read.clear();
                            }
                        }
                    }
                } else if (ch == '2') {
                    check_read_tty_char(term, &ch);
                    chars_read.push_back(ch);
                    if (ch == '~') {
                        // TODO: Handle Insert key.
                        chars_read.clear();
                    }
                }
            }
        }
        // Insert for the user (the developer, me) unrecognized escape codes.
        if (!chars_read.empty()) {
            insert_char(&state->buf, '\\');
            insert_char(&state->buf, 'e');
            for (char c : chars_read) {
                insert_char(&state->buf, c);
            }
        }
    } else {
        // TODO: Handle other possible control chars.
        insert_printable_repr(&state->buf, ch);
    }
}

void main_loop(int term, const command_line_args& args) {
    terminal_size window = get_terminal_size(term);
    qwi::state state = initial_state(args, window);

    redraw_state(term, window, state);

    bool exit = false;
    for (; !exit; ) {
        read_and_process_tty_input(term, &state, &exit);

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
