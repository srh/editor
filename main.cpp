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
#include "layout.hpp"
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
    *buf += TESC();
    *buf += '0';
    if (new_mask & terminal_style::BOLD_BIT) {
        *buf += ";1";
    }
    if (new_mask & terminal_style::WHITE_ON_RED_BIT) {
        *buf += ";37;41";
    }
    *buf += 'm';
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

state initial_state(const command_line_args& args) {
    const size_t n_files = args.files.size();

    state state;
    // state still needs its sole active window to point at a buf.
    if (n_files == 0) {
        buffer_id id = state.gen_buf_id();
        state.buf_set.emplace(id, std::make_unique<buffer>(scratch_buffer(id)));
        apply_number_to_buf(&state, id);
        state.active_window()->point_at(id, &state);
    } else {
        state.buf_set.reserve(n_files);
        for (size_t i = 0; i < n_files; ++i) {
            auto buf = std::make_unique<buffer>(open_file_into_detached_buffer(&state, args.files.at(i)));
            buffer_id id = buf->id;
            state.buf_set.emplace(id, std::move(buf));
            apply_number_to_buf(&state, id);

            if (i == 0) {
                state.active_window()->point_at(id, &state);
            }
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

void render_string(terminal_frame *frame, const terminal_coord& coord, uint32_t rendering_width, const buffer_string& str, terminal_style style_mask = terminal_style{}) {
    uint32_t col = coord.col;
    const uint32_t end_col = u32_add(col, rendering_width);
    logic_check(end_col <= frame->window.cols, "render_string: coord out of range");
    size_t line_col = 0;
    for (size_t i = 0; i < str.size() && col < end_col; ++i) {
        char_rendering rend = compute_char_rendering(str[i], &line_col);
        if (rend.count == SIZE_MAX) {
            // Newline...(?)
            return;
        }
        size_t to_copy = std::min<size_t>(rend.count, end_col - col);
        size_t offset = coord.row * frame->window.cols + col;
        std::copy(rend.buf, rend.buf + to_copy, &frame->data[offset]);
        std::fill(&frame->style_data[offset], &frame->style_data[offset + to_copy], style_mask);
        col += to_copy;
    }
}

void render_normal_status_area(terminal_frame *frame, const state& state, const buffer *buf, terminal_coord status_area_topleft, uint32_t status_area_width) {
    buffer_string str = buffer_name(&state, buf->id);
    // TODO: Probably, I want the line number info not to be bold.
    str += to_buffer_string(buf->modified_flag() ? " ** (" : "    (");
    size_t line, col;
    buf->line_info(&line, &col);
    str += to_buffer_string(std::to_string(line));  // TODO: Gross
    str += buffer_char{','};
    str += to_buffer_string(std::to_string(col));  // TODO: Gross
    str += buffer_char{')'};

    render_string(frame, status_area_topleft, status_area_width, str, terminal_style::bold());
}

// Returns true if we should render (in red) the cursor for the active window.
bool render_status_area_or_prompt(terminal_frame *frame, const state& state, const buffer *buf,
                                  terminal_coord status_area_topleft, uint32_t status_area_width) {
    bool ret = state.status_prompt.has_value();
    if (!state.live_error_message.empty()) {
        render_string(frame, {.row = status_area_topleft.row, .col = 0},
                      status_area_width,
                      to_buffer_string(state.live_error_message), terminal_style::zero());
        return ret;
    }

    if (state.status_prompt.has_value()) {
        std::string message;
        switch (state.status_prompt->typ) {
        case prompt::type::proc:
            // TODO: Such a gratuitous copy.
            message = state.status_prompt->messageText;
            break;
        }

        render_string(frame, status_area_topleft, status_area_width, to_buffer_string(message), terminal_style::bold());

        std::vector<render_coord> coords = { {state.status_prompt->buf.cursor(), std::nullopt} };
        terminal_coord prompt_topleft = {
            .row = status_area_topleft.row,
            .col = u32_add(status_area_topleft.col, std::min<uint32_t>(status_area_width, uint32_t(message.size())))
        };

        window_size winsize = {.rows = 1, .cols = status_area_topleft.col + status_area_width - prompt_topleft.col};
        // TODO: Should render_into_frame be appending to rendered_window_sizes instead of ALL its callers?
        frame->rendered_window_sizes.emplace_back(&state.status_prompt->win_ctx, winsize);
        render_into_frame(frame, prompt_topleft, winsize, state.status_prompt->win_ctx, state.status_prompt->buf, &coords);

        logic_check(!frame->cursor.has_value(),
                    "attempted rendering status prompt cursor atop another rendered cursor");
        frame->cursor = add(prompt_topleft, coords[0].rendered_pos);
    } else {
        render_normal_status_area(frame, state, buf, status_area_topleft, status_area_width);
    }
    return ret;
}

constexpr terminal_char column_divider_char = { '|' };
constexpr uint32_t column_divider_size = 1;

void render_column_divider(terminal_frame *frame, uint32_t rendering_column) {
    logic_checkg(rendering_column < frame->window.cols);
    for (uint32_t i = rendering_column; i < frame->data.size(); i += frame->window.cols) {
        frame->data[i] = column_divider_char;
        // TODO: Divider style gray?
    }
}

std::vector<std::pair<const ui_window_ctx *, window_size>>
redraw_state(int term, const terminal_size& window, const state& state) {
    terminal_frame frame = init_frame(window);

    if (state.popup_display.has_value()) {
        window_size winsize = {window.rows, window.cols};
        frame.rendered_window_sizes.emplace_back(&state.popup_display->win_ctx, winsize);

        terminal_coord window_topleft = {0, 0};
        std::vector<render_coord> coords;
        render_into_frame(&frame, window_topleft, winsize, state.popup_display->win_ctx,
                          state.popup_display->buf, &coords);
    } else {
        std::vector<uint32_t> columnar_splits
            = true_split_sizes<window_layout::col_data>(
                window.cols, column_divider_size,
                state.layout.column_datas.begin(),
                state.layout.column_datas.end(),
                [](const window_layout::col_data& cd) { return cd.relsize; });

        uint32_t rendering_column = 0;
        size_t col_relsizes_begin = 0;
        for (size_t column_pane = 0; column_pane < columnar_splits.size(); ++column_pane) {
            if (rendering_column == window.cols) {
                logic_check(columnar_splits[column_pane] == 0,
                            "rendering_column overflowed with non-zero columnar_splits value");
                continue;
            }
            if (column_pane != 0) {
                render_column_divider(&frame, rendering_column);
                ++rendering_column;
            }
            const size_t num_rows = state.layout.column_datas.at(column_pane).num_rows;
            const size_t col_relsizes_end = col_relsizes_begin + num_rows;

            const uint32_t row_divider_size = 0;
            std::vector<uint32_t> row_splits
                = true_split_sizes<uint32_t>(
                    window.rows, row_divider_size,
                    state.layout.row_relsizes.begin() + col_relsizes_begin,
                    state.layout.row_relsizes.begin() + col_relsizes_end,
                    [](const uint32_t& elem) { return elem; });

            uint32_t rendering_row = 0;
            for (size_t row_pane = 0; row_pane < row_splits.size(); ++row_pane) {
                if (row_splits[row_pane] == 0) {
                    // Avoid row_splits[row_pane] - 1, special case for status bar rendering, etc.
                    continue;
                }
                const window_size winsize = {
                    .rows = row_splits[row_pane] - 1,
                    .cols = columnar_splits[column_pane],
                };
                window_number winnum = { col_relsizes_begin + row_pane };
                logic_check(winnum.value < state.layout.windows.size(),
                            "row pane window number out of range");
                const ui_window *win = &state.layout.windows[winnum.value];

                const auto& active_tab = win->active_buf();
                const ui_window_ctx *buf_ctx = active_tab.second.get();
                buffer_id buf_id = active_tab.first;

                frame.rendered_window_sizes.emplace_back(buf_ctx, winsize);

                const buffer *buf = state.lookup(buf_id);

                std::vector<render_coord> coords = { {buf->get_mark_offset(buf_ctx->cursor_mark), std::nullopt} };
                terminal_coord window_topleft = {.row = rendering_row, .col = rendering_column};
                render_into_frame(&frame, window_topleft, winsize, *buf_ctx, *buf, &coords);

                {
                    std::optional<terminal_coord> cursor_coord
                        = add(window_topleft, coords[0].rendered_pos);
                    terminal_coord status_area_topleft =
                        {.row = rendering_row + winsize.rows, .col = rendering_column};

                    bool render_red_cursor = true;
                    if (state.layout.active_window.value == winnum.value) {
                        render_red_cursor = render_status_area_or_prompt(
                            &frame, state, buf,
                            status_area_topleft,
                            winsize.cols);
                    } else {
                        render_normal_status_area(
                            &frame, state, buf,
                            status_area_topleft,
                            winsize.cols);
                    }

                    if (render_red_cursor) {
                        if (cursor_coord.has_value()) {
                            frame.style_data[cursor_coord->row * frame.window.cols + cursor_coord->col].mask
                                |= terminal_style::white_on_red().mask;
                        }
                    } else {
                        // (If !cursor_coord.has_value(), assignment is a no-op.)
                        frame.cursor = cursor_coord;
                    }
                }

                rendering_row += row_splits[row_pane];
            }
            rendering_column += columnar_splits[column_pane];
            col_relsizes_begin = col_relsizes_end;
        }
    }

    if (!state.ui_config.ansi_terminal) {
        // Wipe out styling.
        for (terminal_style& style  : frame.style_data) {
            style = terminal_style();
        }
    }

    write_frame(term, frame);

    return std::move(frame.rendered_window_sizes);
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

undo_killring_handled insert_printable_repr(state *state, ui_window_ctx *ui, buffer *buf, char sch) {
    buffer_string str;
    push_printable_repr(&str, sch);
    insert_result res = insert_chars(ui, buf, str.data(), str.size());
    return note_action(state, buf, std::move(res));
}

undo_killring_handled delete_keypress(state *state, ui_window_ctx *ui, buffer *buf) {
    delete_result res = delete_char(ui, buf);
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

undo_killring_handled character_keypress(state *state, ui_window_ctx *ui, buffer *active_buf, uint8_t uch) {
    insert_result res = insert_char(ui, active_buf, uch);
    return note_coalescent_action(state, active_buf, std::move(res));
}

undo_killring_handled tab_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    return character_keypress(state, ui, active_buf, '\t');
}

undo_killring_handled meta_f_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    move_forward_word(ui, active_buf);
    return note_navigation_action(state, active_buf);
}
undo_killring_handled meta_b_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    move_backward_word(ui, active_buf);
    return note_navigation_action(state, active_buf);
}
undo_killring_handled meta_h_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    (void)ui, (void)active_buf;
    return help_menu(state);
}
undo_killring_handled meta_y_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    return alt_yank_from_clipboard(state, ui, active_buf);
}
undo_killring_handled meta_d_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    return delete_forward_word(state, ui, active_buf);
}
undo_killring_handled meta_backspace_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    return delete_backward_word(state, ui, active_buf);
}
undo_killring_handled ctrl_x_ctrl_w_keypress(state *state, buffer *active_buf) {
    return save_as_file_action(state, active_buf);
}
undo_killring_handled meta_1_to_9_keypress(state *state, buffer *active_buf, char value) {
    return switch_to_window_number_action(state, active_buf, int(value - '0'));
}

undo_killring_handled meta_w_keypress(state *state, buffer *active_buf) {
    return copy_region(state, active_buf);
}
undo_killring_handled right_arrow_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    move_right(ui, active_buf);
    return note_navigation_action(state, active_buf);
}
undo_killring_handled left_arrow_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    move_left(ui, active_buf);
    return note_navigation_action(state, active_buf);
}
undo_killring_handled up_arrow_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    move_up(ui, active_buf);
    return note_navigation_action(state, active_buf);
}
undo_killring_handled down_arrow_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    move_down(ui, active_buf);
    return note_navigation_action(state, active_buf);
}
undo_killring_handled home_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    move_home(ui, active_buf);
    return note_navigation_action(state, active_buf);
}
undo_killring_handled end_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    move_end(ui, active_buf);
    return note_navigation_action(state, active_buf);
}

undo_killring_handled ctrl_backspace_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    return delete_backward_word(state, ui, active_buf);
}

undo_killring_handled ctrl_a_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    return home_keypress(state, ui, active_buf);
}

undo_killring_handled ctrl_b_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    return left_arrow_keypress(state, ui, active_buf);
}

undo_killring_handled ctrl_x_ctrl_c_keypress(state *state, buffer *active_buf, bool *exit_loop) {
    bool exit = false;
    auto ret = exit_cleanly(state, active_buf, &exit);
    if (exit) { *exit_loop = true; }
    return ret;
}

undo_killring_handled ctrl_d_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    return delete_keypress(state, ui, active_buf);
}

undo_killring_handled ctrl_e_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    return end_keypress(state, ui, active_buf);
}

undo_killring_handled ctrl_f_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    return right_arrow_keypress(state, ui, active_buf);
}

undo_killring_handled ctrl_g_keypress(state *state, buffer *active_buf) {
    return cancel_action(state, active_buf);
}

undo_killring_handled ctrl_n_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    return down_arrow_keypress(state, ui, active_buf);
}

undo_killring_handled ctrl_o_keypress(state *state, buffer *active_buf) {
    return switch_to_next_window_action(state, active_buf);
}

undo_killring_handled ctrl_p_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    return up_arrow_keypress(state, ui, active_buf);
}

undo_killring_handled ctrl_x_ctrl_s_keypress(state *state, buffer *active_buf) {
    // May prompt if the buf isn't married to a file.
    return save_file_action(state, active_buf);
}

undo_killring_handled backspace_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    // TODO: Here, and perhaps elsewhere, handle undo where no characters were actually deleted.
    delete_result res = backspace_char(ui, active_buf);
    return note_coalescent_action(state, active_buf, std::move(res));
}

undo_killring_handled ctrl_k_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    return kill_line(state, ui, active_buf);
}

undo_killring_handled ctrl_w_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    return kill_region(state, ui, active_buf);
}

undo_killring_handled ctrl_y_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    return yank_from_clipboard(state, ui, active_buf);
}

undo_killring_handled ctrl_space_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    set_mark(ui, active_buf);
    return note_backout_action(state, active_buf);
}

undo_killring_handled ctrl_underscore_keypress(state *state, ui_window_ctx *ui, buffer *active_buf) {
    no_yank(&state->clipboard);
    perform_undo(state, ui, active_buf);
    return handled_undo_killring(state, active_buf);
}

undo_killring_handled ctrl_x_2_keypress(state *state, buffer *active_buf) {
    return split_horizontally(state, active_buf);
}

undo_killring_handled ctrl_x_3_keypress(state *state, buffer *active_buf) {
    return split_vertically(state, active_buf);
}

undo_killring_handled ctrl_x_k_keypress(state *state, buffer *active_buf) {
    return buffer_close_action(state, active_buf);
}

undo_killring_handled ctrl_x_ctrl_f_keypress(state *state, buffer *active_buf) {
    return open_file_action(state, active_buf);
}

undo_killring_handled ctrl_x_arrow_keypress(state *state, buffer *active_buf, ortho_direction direction) {
    return grow_window_size(state, active_buf, direction);
}

undo_killring_handled process_keyprefix_in_buf(
    state *state, ui_window_ctx *win, buffer *active_buf, bool *exit_loop);

bool process_keyprefix_in_status_prompt(state *state, bool *exit_loop) {
    keypress kp = state->keyprefix.at(0);

    logic_checkg(state->status_prompt.has_value());
    if (kp.equals(keypress::special_key::Enter)) {
        // TODO: Do we want enter_handle_status_prompt to return an undo_killring_handled value?
        undo_killring_handled discard = enter_handle_status_prompt(state, exit_loop);
        (void)discard;
        return true;
    }
    if (kp.equals('g', keypress::CTRL)) {
        undo_killring_handled discard = note_bufless_backout_action(state);
        (void)discard;

        // At some point we should probably add a switch statement to handle all cases, or
        // a, uh, cancel handler on the `prompt` type, but for now this is correct for
        // buffer-based proc prompts.  (At some point we'll want message reporting like
        // "C-x C-g is undefined".)
        // TODO: Message reporting that we closed the prompt.
        close_status_prompt(state);
        return true;
    }
    return false;
}

undo_killring_handled read_and_process_tty_input(int term, state *state, bool *exit_loop) {
    // TODO: When term is non-blocking, we'll need to wait for readiness...?
    const keypress_result kpr = read_tty_keypress(term);
    const keypress& kp = kpr.kp;

    state->popup_display = std::nullopt;

    if (kpr.isMisparsed) {
        state->note_error_message("Unparsed escape sequence: \\e" + kpr.chars_read);

        state->keyprefix.clear();
        // Do nothing for undo or killring.
        return handled_undo_killring_no_buf(state);
    }
    if (!kpr.chars_read.empty()) {
        // TODO: Get rid of this.
        state->add_message("Successfully parsed escape sequence: \\e" + kpr.chars_read);
    }

    // Append to keyprefix and process it later.
    state->keyprefix.push_back(kp);

    if (!state->status_prompt.has_value()) {
        const auto& active_tab = state->active_window()->active_buf();
        buffer *active_buf = state->lookup(active_tab.first);
        ui_window_ctx *ui = active_tab.second.get();
        return process_keyprefix_in_buf(state, ui, active_buf, exit_loop);
    }

    // Status prompt may intercept some keypresses, then pass on to its buf.

    // TODO: There's a very good chance we're going to overwrite the status_prompt in some
    // cases, then pass active_buf to some generic undo/killring function.  Right now the
    // memory safety is protected by dynamic logic.

    buffer *active_buf = &state->status_prompt->buf;
    ui_window_ctx *win = &state->status_prompt->win_ctx;

    if (process_keyprefix_in_status_prompt(state, exit_loop)) {
        state->keyprefix.clear();
        // Undo/killring supposedly handled (and return value swallowed) by
        // process_keyprefix_in_status_prompt.
        return undo_killring_handled{};
    }

    return process_keyprefix_in_buf(state, win, active_buf, exit_loop);
}

undo_killring_handled process_keyprefix_in_buf(
    state *state, ui_window_ctx *ui, buffer *active_buf, bool *exit_loop, bool *clear_keyprefix);

undo_killring_handled process_keyprefix_in_buf(
    state *state, ui_window_ctx *ui, buffer *active_buf, bool *exit_loop) {
    // TODO: Hacky and gross... but what to do?
    bool clear_keyprefix = true;
    auto ret = process_keyprefix_in_buf(state, ui, active_buf, exit_loop, &clear_keyprefix);
    if (clear_keyprefix) {
        state->keyprefix.clear();
    }
    return ret;
}

// The purpose of this function is mainly to construct an undo_killring_handled{} value.
undo_killring_handled continue_keyprefix(bool *clear_keyprefix) {
    *clear_keyprefix = false;
    return undo_killring_handled{};
}

// Processes a keypress when we're focused in a buffer.
undo_killring_handled process_keyprefix_in_buf(
    state *state, ui_window_ctx *ui, buffer *active_buf, bool *exit_loop, bool *clear_keyprefix) {
    logic_checkg(state->keyprefix.size() > 0);

    keypress kp = state->keyprefix.at(0);

    if (kp.value >= 0 && kp.modmask == 0) {
        // TODO: What if kp.value >= 256?
        return character_keypress(state, ui, active_buf, uint8_t(kp.value));
    }
    using special_key = keypress::special_key;
    if (kp.modmask != 0) {
        if (kp.equals(special_key::Delete, keypress::SHIFT)) {
            return shift_delete_keypress(state, active_buf);
        }

        if (kp.modmask == keypress::META) {
            if (kp.value >= '1' && kp.value <= '9') {
                return meta_1_to_9_keypress(state, active_buf, static_cast<char>(kp.value));
            }

            switch (kp.value) {
            case 'f': return meta_f_keypress(state, ui, active_buf);
            case 'b': return meta_b_keypress(state, ui, active_buf);
            case 'h': return meta_h_keypress(state, ui, active_buf);
            case 'y': return meta_y_keypress(state, ui, active_buf);
            case 'd': return meta_d_keypress(state, ui, active_buf);
            case keypress::special_to_key_type(special_key::Backspace):
                return meta_backspace_keypress(state, ui, active_buf);
            default:
                break;
            }
        } else if (kp.modmask == keypress::CTRL) {
            switch (kp.value) {
            case ' ': return ctrl_space_keypress(state, ui, active_buf);
            case 'a': return ctrl_a_keypress(state, ui, active_buf);
            case 'b': return ctrl_b_keypress(state, ui, active_buf);
            case 'd': return ctrl_d_keypress(state, ui, active_buf);
            case 'e': return ctrl_e_keypress(state, ui, active_buf);
            case 'f': return ctrl_f_keypress(state, ui, active_buf);
            case 'g': return ctrl_g_keypress(state, active_buf);
            case 'k': return ctrl_k_keypress(state, ui, active_buf);
            case 'n': return ctrl_n_keypress(state, ui, active_buf);
            case 'o': return ctrl_o_keypress(state, active_buf);
            case 'p': return ctrl_p_keypress(state, ui, active_buf);
            case 'w': return ctrl_w_keypress(state, ui, active_buf);
            case 'y': return ctrl_y_keypress(state, ui, active_buf);
            case 'x': {
                if (state->keyprefix.size() == 1) {
                    return continue_keyprefix(clear_keyprefix);
                }
                keypress kp1 = state->keyprefix.at(1);
                if (kp1.modmask == 0) {
                    switch (kp1.value) {
                    case '2':
                        return ctrl_x_2_keypress(state, active_buf);
                    case '3':
                        return ctrl_x_3_keypress(state, active_buf);
                    case 'k':
                        return ctrl_x_k_keypress(state, active_buf);
                    case keypress::special_to_key_type(special_key::Left):
                        return ctrl_x_arrow_keypress(state, active_buf, ortho_direction::Left);
                    case keypress::special_to_key_type(special_key::Right):
                        return ctrl_x_arrow_keypress(state, active_buf, ortho_direction::Right);
                    case keypress::special_to_key_type(special_key::Up):
                        return ctrl_x_arrow_keypress(state, active_buf, ortho_direction::Up);
                    case keypress::special_to_key_type(special_key::Down):
                        return ctrl_x_arrow_keypress(state, active_buf, ortho_direction::Down);
                    default:
                        // More C-x-prefixed modmask == 0 keypresses might go here (or below).
                        break;
                    }
                } else if (kp1.modmask == keypress::CTRL) {
                    switch (kp1.value) {
                    case 'f':
                        return ctrl_x_ctrl_f_keypress(state, active_buf);
                    case 'c':
                        return ctrl_x_ctrl_c_keypress(state, active_buf, exit_loop);
                    case 's':
                        return ctrl_x_ctrl_s_keypress(state, active_buf);
                    case 'w':
                        return ctrl_x_ctrl_w_keypress(state, active_buf);
                    default:
                        break;
                    }
                }

                // More C-x-prefixed keypresses would go here.
            } break;
            case '\\':
                *exit_loop = true;
                return undo_killring_handled{};
            case '_': return ctrl_underscore_keypress(state, ui, active_buf);
            case keypress::special_to_key_type(special_key::Backspace):
                return ctrl_backspace_keypress(state, ui, active_buf);
            default:
                break;
            }
        }

    } else if (kp.modmask == 0) {
        switch (keypress::key_type_to_special(kp.value)) {
        case special_key::Tab: return tab_keypress(state, ui, active_buf);
        case special_key::Enter: return character_keypress(state, ui, active_buf, uint8_t('\n'));
        case special_key::Delete: return delete_keypress(state, ui, active_buf);
        case special_key::Insert: return insert_keypress(state, active_buf);
        case special_key::F1: return f1_keypress(state, active_buf);
        case special_key::F2: return f2_keypress(state, active_buf);
        case special_key::F3: return f3_keypress(state, active_buf);
        case special_key::F4: return f4_keypress(state, active_buf);
        case special_key::F5: return f5_keypress(state, active_buf);
        case special_key::F6: return f6_keypress(state, active_buf);
        case special_key::F7: return f7_keypress(state, active_buf);
        case special_key::F8: return f8_keypress(state, active_buf);
        case special_key::F9: return f9_keypress(state, active_buf);
        case special_key::F10: return f10_keypress(state, active_buf);
        case special_key::F11: return f11_keypress(state, active_buf);
        case special_key::F12: return f12_keypress(state, active_buf);
        case special_key::Backspace: return backspace_keypress(state, ui, active_buf);
        case special_key::Right: return right_arrow_keypress(state, ui, active_buf);
        case special_key::Left: return left_arrow_keypress(state, ui, active_buf);
        case special_key::Up: return up_arrow_keypress(state, ui, active_buf);
        case special_key::Down: return down_arrow_keypress(state, ui, active_buf);
        case special_key::Home: return home_keypress(state, ui, active_buf);
        case special_key::End: return end_keypress(state, ui, active_buf);
        default:
            break;
        }
    }

    if (state->keyprefix.size() == 1) {
        state->note_error_message("Unprocessed keypress: " + render_keypress(kp));
    } else {
        logic_checkg(state->keyprefix.size() > 1);  // Non-empty.
        std::string build = "Unprocessed key sequence:";
        for (const auto& k : state->keyprefix) {
            build += ' ';
            build += render_keypress(k);
        }
        state->note_error_message(std::move(build));
    }

    // Do nothing for undo or killring.
    return handled_undo_killring(state, active_buf);
}

void main_loop(int term, const command_line_args& args) {
    state state = initial_state(args);

    terminal_size window = get_terminal_size(term);

    auto redraw = [&] {
        std::vector<std::pair<const ui_window_ctx *, window_size>> window_sizes
            = redraw_state(term, window, state);
        for (auto& elem : window_sizes) {
            // const-ness is inherited from state being passed as a const param -- we now
            // un-const and set_last_rendered_window.
            const_cast<ui_window_ctx *>(elem.first)->set_last_rendered_window(elem.second);
            state.layout.last_rendered_terminal_size = window;
        }
    };
    redraw();

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
            // resize_window(&state, new_window);
            window = new_window;
        }

        redraw();
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
    fprintf(fp,
            "Usage: --help | --version | [files...] [-- files..]\n"
            "  Press M-h (meta-h or alt-h) in-app for keyboard shortcuts.\n");
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
