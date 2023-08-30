#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

#include "arith.hpp"
#include "editing.hpp"
#include "error.hpp"
#include "io.hpp"
#include "movement.hpp"
#include "state.hpp"
#include "term_ui.hpp"
#include "terminal.hpp"

namespace fs = std::filesystem;

struct command_line_args {
    bool version = false;
    bool help = false;

    std::vector<std::string> files;
};

namespace qwi {

void append_mask_difference(std::string *buf, uint8_t old_mask, uint8_t new_mask) {
    static_assert(std::is_same<decltype(old_mask), decltype(terminal_style::mask)>::value);

    // Right now this code is non-general -- it assumes there is _only_ a bold bit.
    switch (int(new_mask & terminal_style::BOLD_BIT) - int(old_mask & terminal_style::BOLD_BIT)) {
    case 0: break;
    case terminal_style::BOLD_BIT: {
        *buf += TESC(1m);

    } break;
    case -terminal_style::BOLD_BIT: {
        *buf += TESC(0m);
    } break;
    }

}

// Notably, this function does not write any ansi color or style escape sequences if the
// style is uninital
void write_frame(int fd, const terminal_frame& frame) {
    uint8_t mask = 0;
    static_assert(std::is_same<decltype(mask), decltype(terminal_style::mask)>::value);

    std::string buf;
    buf += TESC(?25l);
    buf += TESC(H);
    for (size_t i = 0; i < frame.window.rows; ++i) {
        for (size_t j = 0; j < frame.window.cols; ++j) {
            size_t offset = i * frame.window.cols + j;
            if (mask != frame.style_data[offset].mask) {
                append_mask_difference(&buf, mask, frame.style_data[offset].mask);
                mask = frame.style_data[offset].mask;
            }
            buf += frame.data[offset].as_char();
        }
        if (mask != 0) {
            append_mask_difference(&buf, mask, 0);
            mask = 0;
        }
        if (i < frame.window.rows - 1) {
            buf += "\r\n";
        }
    }
    if (frame.cursor.has_value()) {
        buf += TERMINAL_ESCAPE_SEQUENCE;
        buf += std::to_string(frame.cursor->row + 1);
        buf += ';';
        buf += std::to_string(frame.cursor->col + 1);
        buf += 'H';
        // TODO: Make cursor visible when exiting program.
        buf += TESC(?25h);
    }
    write_data(fd, buf.data(), buf.size());
}

void draw_empty_frame_for_exit(int fd, const terminal_size& window) {
    terminal_frame frame = init_frame(window);
    if (!INIT_FRAME_INITIALIZES_WITH_SPACES) {
        for (size_t i = 0; i < frame.data.size(); ++i) {
            frame.data[i] = terminal_char{' '};
        }
    }
    // TODO: Ensure cursor is restored on non-happy-paths.
    frame.cursor = {0, 0};

    write_frame(fd, frame);
}

state initial_state(const command_line_args& args, const terminal_size& window) {
    const size_t n_files = args.files.size();

    window_size buf_window = main_buf_window_from_terminal_window(window);

    state state;
    if (n_files == 0) {
        state.buf_ptr.value = 0;
        state.buflist.push_back(scratch_buffer(buf_window));
    } else {
        state.buf_ptr.value = 0;
        state.buflist.reserve(n_files);
        state.buflist.clear();  // a no-op
        for (size_t i = 0; i < n_files; ++i) {
            // TODO: Maybe combine these ops and define the fn in editing.cpp
            state.buflist.push_back(open_file_into_detached_buffer(args.files.at(i)));
            state.buflist.back().set_window(buf_window);
            apply_number_to_buf(&state, buffer_number{i});

            // TODO: How do we handle duplicate file names?  Just allow identical buffer
            // names, but make selecting them in the UI different?  Only allow identical
            // buffer names when there are married files?  Disallow the concept of a
            // "buffer name" when there's a married file?
        }
    }
    return state;
}

terminal_coord add(const terminal_coord& window_topleft, window_coord wc) {
    return terminal_coord{u32_add(window_topleft.row, wc.row),
        u32_add(window_topleft.col, wc.col)};
}

std::optional<terminal_coord> add(const terminal_coord& window_topleft, const std::optional<window_coord>& wc) {
    if (wc.has_value()) {
        return add(window_topleft, *wc);
    } else {
        return std::nullopt;
    }
}

void render_string(terminal_frame *frame, const terminal_coord& coord, const buffer_string& str, terminal_style style_mask = terminal_style{}) {
    uint32_t col = coord.col;  // <= frame->window.cols
    runtime_check(col <= frame->window.cols, "render_string: coord out of range");
    size_t line_col = 0;
    for (size_t i = 0; i < str.size() && col < frame->window.cols; ++i) {
        char_rendering rend = compute_char_rendering(str[i], &line_col);
        if (rend.count == SIZE_MAX) {
            // Newline...(?)
            return;
        }
        size_t to_copy = std::min<size_t>(rend.count, frame->window.cols - col);
        size_t offset = coord.row * frame->window.cols + col;
        std::copy(rend.buf, rend.buf + to_copy, &frame->data[offset]);
        std::fill(&frame->style_data[offset], &frame->style_data[offset + to_copy], style_mask);
        col += to_copy;
    }
}

// TODO: Non-const reference for state param -- we set its status_prompt's buf's window.
void render_status_area(terminal_frame *frame, state& state) {
    uint32_t last_row = u32_sub(frame->window.rows, 1);

    if (!state.live_error_message.empty()) {
        render_string(frame, {.row = last_row, .col = 0},
                      to_buffer_string(state.live_error_message), terminal_style::zero());
        return;
    }

    if (state.status_prompt.has_value()) {
        std::string message;
        switch (state.status_prompt->typ) {
        case prompt::type::file_open: message = "file to open: "; break;
        case prompt::type::file_save: message = "file to save: "; break;
        case prompt::type::buffer_switch: message = "switch to buffer: "; break;
        case prompt::type::buffer_close: message = "close without saving? (yes/no): "; break;
        case prompt::type::exit_without_save:
            message = "exit without saving? (" + state.status_prompt->messageText + ") (yes/no): ";
            break;
        }

        render_string(frame, {.row = last_row, .col = 0}, to_buffer_string(message), terminal_style::bold());

        std::vector<render_coord> coords = { {state.status_prompt->buf.cursor(), std::nullopt} };
        terminal_coord prompt_topleft = {.row = last_row, .col = uint32_t(message.size())};
        // TODO: Use resize_buf_window here, generally.
        state.status_prompt->buf.set_window({.rows = 1, .cols = frame->window.cols - prompt_topleft.col});
        render_into_frame(frame, prompt_topleft, state.status_prompt->buf, &coords);

        // TODO: This is super-hacky -- we overwrite the main buffer's cursor.
        frame->cursor = add(prompt_topleft, coords[0].rendered_pos);
    } else {
        buffer_string str = buffer_name(&state, state.buf_ptr);
        str += to_buffer_string(state.topbuf().modified_flag() ? " **" : "   ");
        render_string(frame, {.row = last_row, .col = 0}, str, terminal_style::bold());
    }
}

// TODO: non-const reference for state, passed into render_status_area
void redraw_state(int term, const terminal_size& window, state& state) {
    terminal_frame frame = init_frame(window);

    if (!too_small_to_render(state.topbuf().window)) {
        // TODO: Support resizing.
        runtime_check(window.cols == state.topbuf().window.cols, "window cols changed");
        runtime_check(window.rows == state.topbuf().window.rows + STATUS_AREA_HEIGHT, "window rows changed");

        std::vector<render_coord> coords = { {state.topbuf().cursor(), std::nullopt} };
        terminal_coord window_topleft = {0, 0};
        render_into_frame(&frame, window_topleft, state.topbuf(), &coords);

        // TODO: This is super-hacky -- this gets overwritten if the status area has a
        // prompt.  With multiple buffers, we need some concept of an active buffer, with
        // an active cursor.
        // TODO: Also, we don't render our inactive cursor, and we should.
        frame.cursor = add(window_topleft, coords[0].rendered_pos);

        render_status_area(&frame, state);
    }

    if (!state.ui_config.ansi_terminal) {
        // Wipe out styling.
        for (terminal_style& style  : frame.style_data) {
            style = terminal_style();
        }
    }

    write_frame(term, frame);
}

// Cheap fn for debugging purposes.
void push_printable_repr(buffer_string *str, char sch) {
    uint8_t ch = uint8_t(sch);
    if (ch == '\n' || ch == '\t') {
        str->push_back(buffer_char{ch});
    } else if (ch < 32 || ch > 126) {
        str->push_back(buffer_char{'\\'});
        str->push_back(buffer_char{'x'});
        const char *hex = "0123456789abcdef";
        str->push_back(buffer_char{uint8_t(hex[ch / 16])});
        str->push_back(buffer_char{uint8_t(hex[ch % 16])});
    } else {
        str->push_back(buffer_char{ch});
    }
}

undo_killring_handled insert_printable_repr(state *state, buffer *buf, char sch) {
    buffer_string str;
    push_printable_repr(&str, sch);
    insert_result res = insert_chars(buf, str.data(), str.size());
    return note_action(state, buf, std::move(res));
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

// TODO: All keypresses should be implemented.
undo_killring_handled unimplemented_keypress() {
    return undo_killring_handled{};
}

undo_killring_handled nop_keypress() {
    return undo_killring_handled{};
}

// *exit_loop can be assigned true; there is no need to assign it false.
undo_killring_handled enter_keypress(int term, state *state, bool *exit_loop) {
    if (!state->status_prompt.has_value()) {
        insert_result res = insert_char(&state->topbuf(), '\n');
        return note_coalescent_action(state, &state->topbuf(), std::move(res));
    }

    return enter_handle_status_prompt(term, state, exit_loop);
}

undo_killring_handled delete_keypress(state *state, buffer *buf) {
    delete_result res = delete_char(buf);
    // TODO: Here, and perhaps in general, handle cases where no characters were actually deleted.
    return note_coalescent_action(state, buf, std::move(res));
}

undo_killring_handled insert_keypress(state *state, buffer *buf) {
    (void)state, (void)buf;
    return unimplemented_keypress();
}

undo_killring_handled f1_keypress(state *, buffer *) { return nop_keypress(); }
undo_killring_handled f2_keypress(state *, buffer *) { return nop_keypress(); }
undo_killring_handled f3_keypress(state *, buffer *) { return nop_keypress(); }
undo_killring_handled f4_keypress(state *, buffer *) { return nop_keypress(); }
undo_killring_handled f5_keypress(state *state, buffer *active_buf) { return rotate_buf_right(state, active_buf); }
undo_killring_handled f6_keypress(state *state, buffer *active_buf) { return rotate_buf_left(state, active_buf); }
undo_killring_handled f7_keypress(state *state, buffer *active_buf) { return buffer_switch_action(state, active_buf); }
undo_killring_handled f8_keypress(state *, buffer *) { return nop_keypress(); }
undo_killring_handled f9_keypress(state *, buffer *) { return nop_keypress(); }
undo_killring_handled f10_keypress(state *, buffer *) { return nop_keypress(); }
undo_killring_handled f11_keypress(state *, buffer *) { return nop_keypress(); }
undo_killring_handled f12_keypress(state *, buffer *) { return nop_keypress(); }

undo_killring_handled shift_delete_keypress(state *, buffer *) { return unimplemented_keypress(); }

undo_killring_handled character_keypress(state *state, buffer *active_buf, uint8_t uch) {
    insert_result res = insert_char(active_buf, uch);
    return note_coalescent_action(state, active_buf, std::move(res));
}

undo_killring_handled tab_keypress(state *state, buffer *active_buf) {
    return character_keypress(state, active_buf, '\t');
}

undo_killring_handled meta_f_keypress(state *state, buffer *active_buf) {
    move_forward_word(active_buf);
    return note_navigation_action(state, active_buf);
}
undo_killring_handled meta_b_keypress(state *state, buffer *active_buf) {
    move_backward_word(active_buf);
    return note_navigation_action(state, active_buf);
}
undo_killring_handled meta_q_keypress(state *state, buffer *active_buf) {
    return buffer_close_action(state, active_buf);
}
undo_killring_handled meta_y_keypress(state *state, buffer *active_buf) {
    return alt_yank_from_clipboard(state, active_buf);
}
undo_killring_handled meta_d_keypress(state *state, buffer *active_buf) {
    return delete_forward_word(state, active_buf);
}
undo_killring_handled meta_backspace_keypress(state *state, buffer *active_buf) {
    return delete_backward_word(state, active_buf);
}
undo_killring_handled meta_w_keypress(state *state, buffer *active_buf) {
    return copy_region(state, active_buf);
}
undo_killring_handled right_arrow_keypress(state *state, buffer *active_buf) {
    move_right(active_buf);
    return note_navigation_action(state, active_buf);
}
undo_killring_handled left_arrow_keypress(state *state, buffer *active_buf) {
    move_left(active_buf);
    return note_navigation_action(state, active_buf);
}
undo_killring_handled up_arrow_keypress(state *state, buffer *active_buf) {
    move_up(active_buf);
    return note_navigation_action(state, active_buf);
}
undo_killring_handled down_arrow_keypress(state *state, buffer *active_buf) {
    move_down(active_buf);
    return note_navigation_action(state, active_buf);
}
undo_killring_handled home_keypress(state *state, buffer *active_buf) {
    move_home(active_buf);
    return note_navigation_action(state, active_buf);
}
undo_killring_handled end_keypress(state *state, buffer *active_buf) {
    move_end(active_buf);
    return note_navigation_action(state, active_buf);
}

undo_killring_handled ctrl_backspace_keypress(state *state, buffer *active_buf) {
    return delete_backward_word(state, active_buf);
}

undo_killring_handled ctrl_a_keypress(state *state, buffer *active_buf) {
    return home_keypress(state, active_buf);
}

undo_killring_handled ctrl_b_keypress(state *state, buffer *active_buf) {
    return left_arrow_keypress(state, active_buf);
}

undo_killring_handled ctrl_c_keypress(state *state, buffer *active_buf, bool *exit_loop) {
    bool exit = false;
    auto ret = exit_cleanly(state, active_buf, &exit);
    if (exit) { *exit_loop = true; }
    return ret;
}

undo_killring_handled ctrl_d_keypress(state *state, buffer *active_buf) {
    return delete_keypress(state, active_buf);
}

undo_killring_handled ctrl_e_keypress(state *state, buffer *active_buf) {
    return end_keypress(state, active_buf);
}

undo_killring_handled ctrl_f_keypress(state *state, buffer *active_buf) {
    return right_arrow_keypress(state, active_buf);
}

undo_killring_handled ctrl_g_keypress(state *state, buffer *active_buf) {
    return cancel_action(state, active_buf);
}

undo_killring_handled ctrl_n_keypress(state *state, buffer *active_buf) {
    return down_arrow_keypress(state, active_buf);
}

undo_killring_handled ctrl_o_keypress(state *state, buffer *active_buf) {
    return open_file_action(state, active_buf);
}

undo_killring_handled ctrl_p_keypress(state *state, buffer *active_buf) {
    return up_arrow_keypress(state, active_buf);
}

undo_killring_handled ctrl_s_keypress(state *state, buffer *active_buf) {
    // May prompt if the buf isn't married to a file.
    return save_file_action(state, active_buf);
}

undo_killring_handled backspace_keypress(state *state, buffer *active_buf) {
    // TODO: Here, and perhaps elsewhere, handle undo where no characters were actually deleted.
    delete_result res = backspace_char(active_buf);
    return note_coalescent_action(state, active_buf, std::move(res));
}

undo_killring_handled ctrl_k_keypress(state *state, buffer *active_buf) {
    return kill_line(state, active_buf);
}

undo_killring_handled ctrl_w_keypress(state *state, buffer *active_buf) {
    return kill_region(state, active_buf);
}

undo_killring_handled ctrl_y_keypress(state *state, buffer *active_buf) {
    return yank_from_clipboard(state, active_buf);
}

undo_killring_handled ctrl_space_keypress(state *state, buffer *active_buf) {
    set_mark(active_buf);
    return note_backout_action(state, active_buf);
}

undo_killring_handled ctrl_underscore_keypress(state *state, buffer *active_buf) {
    no_yank(&state->clipboard);
    perform_undo(state, active_buf);
    return handled_undo_killring(state, active_buf);
}

// Reads remainder of "\e[\d+(;\d+)?~" character escapes after the first digit was read.
bool read_tty_numeric_escape(int term, std::string *chars_read, char firstDigit, std::pair<uint8_t, std::optional<uint8_t>> *out) {
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
        } else if (ch == '~') {
            if (first_number.has_value()) {
                out->first = *first_number;
                out->second = number;
            } else {
                out->first = number;
                out->second = std::nullopt;
            }
            return true;
        } else if (ch == ';') {
            if (first_number.has_value()) {
                // TODO: We want to consume the whole keyboard escape code and ignore it together.
                return false;
            }
            // TODO: Should we enforce a digit after the first semicolon, or allow "\e[\d+;~" as the code does now?
            first_number = number;
            number = 0;
        }
    }
}

undo_killring_handled read_and_process_tty_input(int term, state *state, bool *exit_loop) {
    // TODO: When term is non-blocking, we'll need to wait for readiness...?
    char ch;
    check_read_tty_char(term, &ch);

    buffer *active_buf = state->status_prompt.has_value() ? &state->status_prompt->buf : &state->topbuf();

    if (ch >= 32 && ch < 127) {
        return character_keypress(state, active_buf, uint8_t(ch));
    }
    if (ch == '\t') {
        return tab_keypress(state, active_buf);
    }
    if (ch == '\r') {
        return enter_keypress(term, state, exit_loop);
    }
    if (ch == 28) {
        // Ctrl+backslash
        *exit_loop = true;
        return undo_killring_handled{};
    }
    if (ch == 27) {
        std::string chars_read;
        check_read_tty_char(term, &ch);
        chars_read.push_back(ch);
        // TODO: Handle all possible escapes...
        if (ch == '[') {
            check_read_tty_char(term, &ch);
            chars_read.push_back(ch);

            if (isdigit(ch)) {
                std::pair<uint8_t, std::optional<uint8_t>> numbers;
                if (read_tty_numeric_escape(term, &chars_read, ch, &numbers)) {
                    if (!numbers.second.has_value()) {
                        switch (numbers.first) {
                        case 3:
                            return delete_keypress(state, active_buf);
                        case 2:
                            return insert_keypress(state, active_buf);

                        // (Yes, the escape codes aren't as contiguous as you'd expect.)
                        case 15: return f5_keypress(state, active_buf);  // F5
                        case 17: return f6_keypress(state, active_buf);  // F6
                        case 18: return f7_keypress(state, active_buf);  // F7
                        case 19: return f8_keypress(state, active_buf);  // F8
                        case 20: return f9_keypress(state, active_buf);  // F9
                        case 21: return f10_keypress(state, active_buf);  // F10
                            // TODO: F11
                        case 24: return f12_keypress(state, active_buf);  // F12
                        default:
                            break;
                        }
                    } else {
                        uint8_t numbers_second = *numbers.second;
                        if (numbers.first == 3 && numbers_second == 2) {
                            return shift_delete_keypress(state, active_buf);
                        }
                    }
                }
            } else {
                switch (ch) {
                case 'C':
                    return right_arrow_keypress(state, active_buf);
                case 'D':
                    return left_arrow_keypress(state, active_buf);
                case 'A':
                    return up_arrow_keypress(state, active_buf);
                case 'B':
                    return down_arrow_keypress(state, active_buf);
                case 'H':
                    return home_keypress(state, active_buf);
                case 'F':
                    return end_keypress(state, active_buf);
                default:
                    break;
                }
            }
        } else {
            switch (ch) {
            case 'f': return meta_f_keypress(state, active_buf);
            case 'b': return meta_b_keypress(state, active_buf);
            case 'q': return meta_q_keypress(state, active_buf);
            case 'y': return meta_y_keypress(state, active_buf);
            case 'd': return meta_d_keypress(state, active_buf);
            case ('?' ^ CTRL_XOR_MASK): return meta_backspace_keypress(state, active_buf);
            case 'w': return meta_w_keypress(state, active_buf);
            case 'O': {
                check_read_tty_char(term, &ch);
                chars_read.push_back(ch);
                switch (ch) {
                case 'P': return f1_keypress(state, active_buf);  // F1
                case 'Q': return f2_keypress(state, active_buf);  // F2
                case 'R': return f3_keypress(state, active_buf);  // F3
                case 'S': return f4_keypress(state, active_buf);  // F4
                default:
                    break;
                }
            } break;
            default:
                break;
            }
        }

        // Insert for the user (the developer, me) unrecognized escape codes.
        buffer_string str;
        str.push_back(buffer_char::from_char('\\'));
        str.push_back(buffer_char::from_char('e'));
        for (char c : chars_read) {
            str.push_back(buffer_char::from_char(c));
        }

        insert_result res = insert_chars(active_buf, str.data(), str.size());
        return note_action(state, active_buf, std::move(res));
    }

    if (ch == 8) {
        return ctrl_backspace_keypress(state, active_buf);
    }

    if (uint8_t(ch) <= 127) {
        switch (ch ^ CTRL_XOR_MASK) {
        case '?': return backspace_keypress(state, active_buf);
        case '@': return ctrl_space_keypress(state, active_buf); // Ctrl+Space same as C-@
        case 'A': return ctrl_a_keypress(state, active_buf);
        case 'B': return ctrl_b_keypress(state, active_buf);
        case 'C': return ctrl_c_keypress(state, active_buf, exit_loop);
        case 'D': return ctrl_d_keypress(state, active_buf);
        case 'E': return ctrl_e_keypress(state, active_buf);
        case 'F': return ctrl_f_keypress(state, active_buf);
        case 'G': return ctrl_g_keypress(state, active_buf);
        case 'K': return ctrl_k_keypress(state, active_buf);
        case 'N': return ctrl_n_keypress(state, active_buf);
        case 'O': return ctrl_o_keypress(state, active_buf);
        case 'P': return ctrl_p_keypress(state, active_buf);
        case 'S': return ctrl_s_keypress(state, active_buf);
        case 'W': return ctrl_w_keypress(state, active_buf);
        case 'Y': return ctrl_y_keypress(state, active_buf);
        case '_': return ctrl_underscore_keypress(state, active_buf);
        default:
            // For now we do push the printable repr for any unhandled chars, for debugging purposes.
            // TODO: Handle other possible control chars.
            return insert_printable_repr(state, active_buf, ch);
        }
    } else {
        // TODO: Handle high characters -- do we just insert them, or do we validate
        // UTF-8, or what?
        return insert_printable_repr(state, active_buf, ch);
    }
}

void main_loop(int term, const command_line_args& args) {
    terminal_size window = get_terminal_size(term);
    state state = initial_state(args, window);

    redraw_state(term, window, state);

    bool exit = false;
    for (; !exit; ) {
        undo_killring_handled handled = read_and_process_tty_input(term, &state, &exit);
        {
            // Undo and killring behavior has been handled exhaustively in all branches of
            // read_and_process_tty_input -- here's where we consume that fact.
            (void)handled;
        }

        // TODO: Use SIGWINCH.  Procrastinating this for as long as possible.
        terminal_size new_window = get_terminal_size(term);
        if (new_window != window) {
            resize_window(&state, new_window);
            window = new_window;
        }
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

}  // namespace qwi

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
        return qwi::run_program(args);
    } catch (const runtime_check_failure& exc) {
        (void)exc;  // No info in exc.
        return 1;
    }
}
