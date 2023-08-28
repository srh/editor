#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <fstream>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include "arith.hpp"
#include "buffer.hpp"
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

// Generated and returned to indicate that the code exhaustively handles undo and killring behavior.
struct [[nodiscard]] undo_killring_handled { };

// TODO: All keypresses should be implemented.
undo_killring_handled unimplemented_keypress() {
    return undo_killring_handled{};
}

undo_killring_handled nop_keypress() {
    return undo_killring_handled{};
}

// Callers will need to handle undo.
#if 0
undo_killring_handled undo_will_need_handling() {
    return undo_killring_handled{};
}
#endif

undo_killring_handled handled_undo_killring(state *state, buffer *buf) {
    (void)state, (void)buf;
    return undo_killring_handled{};
}

atomic_undo_item make_reverse_action(insert_result&& i_res) {
    int8_t rmd = - i_res.modificationFlagDelta.value;
    atomic_undo_item item = {
        .beg = i_res.new_cursor,
        .text_inserted = buffer_string{},
        .text_deleted = std::move(i_res.insertedText),
        .mod_delta = modification_delta{ rmd },
        .side = i_res.side,  // We inserted on left (right), hence we delete on left (right)
    };

    return item;
}

void note_undo(buffer *buf, insert_result&& i_res) {
    // Make and add the _reverse_ action in the undo items.
    // (Why the reverse action?  Because jsmacs did it that way.)

    // TODO: Of course, in some cases we have reverseAddEdit -- but that's only when
    // actually undoing, so there are none yet.
    add_edit(&buf->undo_info, make_reverse_action(std::move(i_res)));
}

void note_nop_undo(buffer *buf) {
    add_nop_edit(&buf->undo_info);
}

undo_killring_handled note_action(state *state, buffer *buf, insert_result&& i_res) {
    no_yank(&state->clipboard);

    note_undo(buf, std::move(i_res));
    return undo_killring_handled{};
}

undo_killring_handled note_coalescent_action(state *state, buffer *buf, insert_result&& i_res) {
    no_yank(&state->clipboard);

    add_coalescent_edit(&buf->undo_info, make_reverse_action(std::move(i_res)), undo_history::char_coalescence::insert_char);
    return undo_killring_handled{};
}

atomic_undo_item make_reverse_action(delete_result&& d_res) {
    int8_t rmd = - d_res.modificationFlagDelta.value;
    atomic_undo_item item = {
        .beg = d_res.new_cursor,
        .text_inserted = std::move(d_res.deletedText),
        .text_deleted = buffer_string{},
        .mod_delta = modification_delta{ rmd },
        .side = d_res.side,  // We deleted on left (right), hence we insert on left (right)
    };

    return item;
}

void note_undo(buffer *buf, delete_result&& d_res) {
    add_edit(&buf->undo_info, make_reverse_action(std::move(d_res)));
}

undo_killring_handled note_action(state *state, buffer *buf, delete_result&& d_res) {
    no_yank(&state->clipboard);

    note_undo(buf, std::move(d_res));
    return undo_killring_handled{};
}

undo_killring_handled note_coalescent_action(state *state, buffer *buf, delete_result&& d_res) {
    no_yank(&state->clipboard);

    Side side = d_res.side;
    using char_coalescence = undo_history::char_coalescence;
    add_coalescent_edit(&buf->undo_info, make_reverse_action(std::move(d_res)),
                        side == Side::left ? char_coalescence::delete_left : char_coalescence::delete_right);
    return undo_killring_handled{};
}

// Callers want to back out of any killring stuff, but don't want to break undo history
// for some reason.
undo_killring_handled note_noundo_killring_action(state *state, buffer *buf) {
    no_yank(&state->clipboard);
    add_coalescence_break(&buf->undo_info);
    return undo_killring_handled{};
}

// An action that backs out of the yank sequence or some undo sequence.  C-g typed into a
// buffer, for example.
undo_killring_handled note_backout_action(state *state, buffer *buf) {
    no_yank(&state->clipboard);
    add_nop_edit(&buf->undo_info);
    return undo_killring_handled{};
}

// Possibly a useless categorization -- maybe useful for refactoring later.
inline undo_killring_handled note_navigation_action(state *state, buffer *buf) {
    return note_backout_action(state, buf);
}

// Currently a nop, we might need a generic action or code adjustments in the future.
// (Current callers also invoke note_navigation_action.)
void note_navigate_away_from_buf(buffer *buf) {
    (void)buf;
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

buffer_string read_file(const fs::path& path) {
    if (!fs::is_regular_file(path)) {
        runtime_fail("Tried opening non-regular file %s", path.c_str());
    }

    static_assert(sizeof(buffer_char) == 1);
    buffer_string ret;
    // TODO: Use system lib at some point (like, when we care, if ever).
    std::ifstream f{path, std::ios::binary};
    f.seekg(0, std::ios::end);
    runtime_check(!f.fail(), "error seeking to end of file %s", path.c_str());
    int64_t filesize = f.tellg();
    runtime_check(filesize != -1, "error reading file size of %s", path.c_str());
    runtime_check(filesize <= SSIZE_MAX, "Size of file %s is too big", path.c_str());
    // TODO: Use resize_and_overwrite (to avoid having to write memory).
    ret.resize(filesize);
    f.seekg(0);
    runtime_check(!f.fail(), "error seeking back to beginning of file %s", path.c_str());

    f.read(as_chars(ret.data()), ret.size());
    runtime_check(!f.fail(), "error reading file %s", path.c_str());

    return ret;
}

std::string buf_name_from_file_path(const fs::path& path) {
    return path.filename().string();
}

// Caller needs to call set_window on the buf, generally, or other ui-specific stuff.
buffer open_file_into_detached_buffer(const std::string& dirty_path) {
    fs::path path = dirty_path;
    buffer_string data = read_file(path);
    std::string name = buf_name_from_file_path(path);

    buffer ret;
    ret.name_str = std::move(name);
    ret.married_file = path.string();
    ret.aft = std::move(data);
    return ret;
}

void apply_number_to_buf(state *state, size_t buf_index) {
    buffer& the_buf = state->buflist.at(buf_index);
    const std::string& name = the_buf.name_str;
    std::unordered_set<uint64_t> numbers;
    for (size_t i = 0, e = state->buflist.size(); i < e; ++i) {
        buffer& existing = state->buflist[i];
        if (i != buf_index && existing.name_str == name) {
            auto res = numbers.insert(existing.name_number);
            logic_check(res.second,
                        "insert_with_name_number_into_buflist seeing bufs with duplicate numbers, name = %s",
                        the_buf.name_str.c_str());
        }
    }

    // TODO: Consider making the numbers start at 1.  So name_number=0 means a number has
    // not been applied.  (Aside from UI differences, the purpose is, by making zero a
    // special value, to make the buf never have an invalid, conflicting number upon
    // mutations of the state.)
    uint64_t n = 0;
    while (numbers.count(n) == 1) {
        ++n;
    }
    the_buf.name_number = n;
}

buffer scratch_buffer(const window_size& buf_window) {
    buffer ret;
    ret.set_window(buf_window);
    ret.name_str = "*scratch*";
    ret.name_number = 0;
    return ret;
}

state initial_state(const command_line_args& args, const terminal_size& window) {
    const size_t n_files = args.files.size();

    window_size buf_window = main_buf_window_from_terminal_window(window);

    state state;
    if (n_files == 0) {
        state.buflist.push_back(scratch_buffer(buf_window));
    } else {
        state.buflist.reserve(n_files);
        state.buflist.clear();  // a no-op
        for (size_t i = 0; i < n_files; ++i) {
            state.buflist.push_back(open_file_into_detached_buffer(args.files.at(i)));
            state.buflist.back().set_window(buf_window);
            apply_number_to_buf(&state, i);

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
    if (state.status_prompt.has_value()) {
        std::string message;
        switch (state.status_prompt->typ) {
        case prompt::type::file_open: message = "file to open: "; break;
        case prompt::type::file_save: message = "file to save: "; break;
        case prompt::type::buffer_switch: message = "switch to buffer: "; break;
        case prompt::type::buffer_close: message = "close without saving? (yes/no): "; break;
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
        buffer_string str = buffer_name(&state, buffer_number{state::topbuf_index_is_0});
        str += to_buffer_string(state.topbuf().modified_flag ? " **" : "   ");
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

void save_buf_to_married_file(const buffer& buf) {
    // TODO: Display that save succeeded, somehow.
    logic_check(buf.married_file.has_value(), "save_buf_to_married_file with unmarried buf");
    std::ofstream fstream(*buf.married_file, std::ios::binary | std::ios::trunc);
    // TODO: Write a temporary file and rename it.
    fstream.write(as_chars(buf.bef.data()), buf.bef.size());
    fstream.write(as_chars(buf.aft.data()), buf.aft.size());
    fstream.close();
    // TODO: Better error handling
    runtime_check(!fstream.fail(), "error writing to file %s", buf.married_file->c_str());
}

void set_save_prompt(state *state) {
    logic_check(!state->status_prompt.has_value(), "set_save_prompt with existing prompt");
    state->status_prompt = {prompt::type::file_save, buffer()};
    // TODO: How/where should we set the prompt's buf's window?
}

void set_buffer_switch_prompt(state *state) {
    logic_check(!state->status_prompt.has_value(), "set_buffer_switch_prompt with existing prompt");
    buffer_string data = buffer_name(state, buffer_number{state::topbuf_index_is_0});
    state->status_prompt = {prompt::type::buffer_switch, buffer::from_data(std::move(data))};
}

undo_killring_handled open_file_action(state *state, buffer *activeBuf) {
    undo_killring_handled ret = note_navigation_action(state, activeBuf);
    if (state->status_prompt.has_value()) {
        return ret;
    }

    state->status_prompt = {prompt::type::file_open, buffer{}};
    return ret;
}

undo_killring_handled save_file_action(state *state, buffer *activeBuf) {
    // Specifically, I don't want to break the undo chain here.
    undo_killring_handled ret = note_noundo_killring_action(state, activeBuf);
    if (state->status_prompt.has_value()) {
        // TODO: We'll have to handle M-x C-s or C-x C-s somehow -- probably by generic
        // logic at the keypress level.

        // Ignore keypress.
        return ret;
    }

    if (state->topbuf().married_file.has_value()) {
        save_buf_to_married_file(state->topbuf());
    } else {
        set_save_prompt(state);
    }
    return ret;
}

undo_killring_handled buffer_switch_action(state *state, buffer *activeBuf) {
    undo_killring_handled ret = note_navigation_action(state, activeBuf);
    if (state->status_prompt.has_value()) {
        // TODO: We'll have to handle M-x C-s or C-x C-s somehow -- probably by generic
        // logic at the keypress level.

        // Ignore keypress.
        return ret;
    }

    buffer_string data = buffer_name(state, buffer_number{state::topbuf_index_is_0});
    state->status_prompt = {prompt::type::buffer_switch, buffer::from_data(std::move(data))};
    return ret;
}

undo_killring_handled buffer_close_action(state *state, buffer *activeBuf) {
    undo_killring_handled ret = note_backout_action(state, activeBuf);
    if (state->status_prompt.has_value()) {
        // TODO: Ignore keypress?  Or should we treat this like C-g for the status prompt?
        return ret;
    }

    // TODO: Only complain if the buffer has been modified.  (Add a modified flag.)
    state->status_prompt = {prompt::type::buffer_close, buffer{}};
    return ret;
}

bool find_buffer_by_name(const state *state, const std::string& text, buffer_number *out) {
    for (size_t i = 0, e = state->buflist.size(); i < e; ++i) {
        if (buffer_name_str(state, buffer_number{i}) == text) {
            *out = buffer_number{i};
            return true;
        }
    }
    return false;
}

void rotate_to_buffer(state *state, buffer_number buf_number);

undo_killring_handled enter_key(int term, state *state) {
    if (!state->status_prompt.has_value()) {
        insert_result res = insert_char(&state->topbuf(), '\n');
        return note_coalescent_action(state, &state->topbuf(), std::move(res));
    }
    switch (state->status_prompt->typ) {
    case prompt::type::file_save: {
        // killring important, undo not because we're destructing the status_prompt buf.
        undo_killring_handled ret = note_backout_action(state, &state->status_prompt->buf);
        // TODO: Of course, handle errors, such as if directory doesn't exist, permissions.
        std::string text = state->status_prompt->buf.copy_to_string();
        if (text != "") {
            state->topbuf().married_file = text;
            save_buf_to_married_file(state->topbuf());
            state->topbuf().name_str = buf_name_from_file_path(fs::path(text));
            state->topbuf().name_number = 0;
            apply_number_to_buf(state, state::topbuf_index_is_0);
        } else {
            // TODO: Implement displaying errors to the user.
        }
        close_status_prompt(state);
        return ret;
    } break;
    case prompt::type::file_open: {
        // killring important, undo not because we're destructing the status_prompt buf.
        undo_killring_handled ret = note_backout_action(state, &state->status_prompt->buf);
        std::string text = state->status_prompt->buf.copy_to_string();
        // TODO: Implement displaying errors to the user.

        if (text != "") {
            // TODO: Handle error!
            buffer buf = open_file_into_detached_buffer(text);

            // TODO: Gross!  So gross.
            terminal_size window = get_terminal_size(term);
            window_size buf_window = main_buf_window_from_terminal_window(window);
            buf.set_window(buf_window);

            static_assert(state::topbuf_index_is_0 == 0);
            state->buflist.insert(state->buflist.begin(), std::move(buf));
            apply_number_to_buf(state, state::topbuf_index_is_0);

        } else {
            // TODO: Display error.
        }

        close_status_prompt(state);
        return ret;
    } break;
    case prompt::type::buffer_switch: {
        // killring important, undo not because we're destructing the status_prompt buf.
        undo_killring_handled ret = note_backout_action(state, &state->status_prompt->buf);
        std::string text = state->status_prompt->buf.copy_to_string();
        // TODO: Implement displaying errors to the user.
        if (text != "") {
            buffer_number buf_number;
            if (find_buffer_by_name(state, text, &buf_number)) {
                rotate_to_buffer(state, buf_number);
            } else {
                // TODO: Display error.
            }
        } else {
            // TODO: Display error.
        }

        close_status_prompt(state);
        return ret;
    } break;
    case prompt::type::buffer_close: {
        // killring important, undo not because we're destructing the status_prompt buf.
        undo_killring_handled ret = note_backout_action(state, &state->status_prompt->buf);
        std::string text = state->status_prompt->buf.copy_to_string();
        // TODO: Implement displaying errors to the user.
        if (text == "yes") {
            // Yes, close without saving.
            logic_checkg(!state->buflist.empty());
            static_assert(state::topbuf_index_is_0 == 0);
            state->buflist.erase(state->buflist.begin());

            // buflist must never be empty
            if (state->buflist.empty()) {
                // TODO: Gross!  So gross.
                terminal_size window = get_terminal_size(term);
                window_size buf_window = main_buf_window_from_terminal_window(window);
                state->buflist.push_back(scratch_buffer(buf_window));
            }
            close_status_prompt(state);
            return ret;
        } else if (text == "no") {
            // No, don't close without saving.
            close_status_prompt(state);
            return ret;
        } else {
            // TODO: Report error.
            return ret;
        }
    } break;
    default:
        logic_fail("status prompt unreachable default case");
        break;
    }
}

undo_killring_handled cancel_key(state *state, buffer *buf) {
    // We break the yank and undo sequence in `buf` -- of course, when creating the status
    // prompt, we already broke the yank and undo sequence in the _original_ buf.
    undo_killring_handled ret = note_backout_action(state, buf);

    if (state->status_prompt.has_value()) {
        // At some point we should probably add a switch statement to handle all cases --
        // but for now this is correct for file_save, file_open, buffer_switch, and
        // buffer_close.  (At some point we'll want message reporting like "C-x C-g is
        // undefined".)
        close_status_prompt(state);
    }

    return ret;
}

undo_killring_handled delete_backward_word(state *state, buffer *buf) {
    size_t d = backward_word_distance(buf);
    delete_result delres = delete_left(buf, d);
    record_yank(&state->clipboard, delres.deletedText, yank_side::left);
    note_undo(buf, std::move(delres));
    return handled_undo_killring(state, buf);
}

undo_killring_handled delete_forward_word(state *state, buffer *buf) {
    size_t d = forward_word_distance(buf);
    delete_result delres = delete_right(buf, d);
    record_yank(&state->clipboard, delres.deletedText, yank_side::right);
    note_undo(buf, std::move(delres));
    return handled_undo_killring(state, buf);
}

undo_killring_handled kill_line(state *state, buffer *buf) {
    size_t eolDistance = distance_to_eol(*buf, buf->cursor());

    delete_result delres;
    if (eolDistance == 0 && buf->cursor() < buf->size()) {
        delres = delete_right(buf, 1);
    } else {
        delres = delete_right(buf, eolDistance);
    }
    record_yank(&state->clipboard, delres.deletedText, yank_side::right);
    note_undo(buf, std::move(delres));
    return handled_undo_killring(state, buf);
}

undo_killring_handled kill_region(state *state, buffer *buf) {
    if (!buf->mark.has_value()) {
        // TODO: Display error
        // (We do NOT want no_yank here.)  We do want to disrupt the undo action chain (if only because Emacs does that).
        note_nop_undo(buf);
        return handled_undo_killring(state, buf);
    }
    size_t mark = *buf->mark;
    size_t cursor = buf->cursor();
    if (mark > cursor) {
        delete_result delres = delete_right(buf, mark - cursor);
        record_yank(&state->clipboard, delres.deletedText, yank_side::right);
        note_undo(buf, std::move(delres));
        return handled_undo_killring(state, buf);
    } else if (mark < cursor) {
        delete_result delres = delete_left(buf, cursor - mark);
        record_yank(&state->clipboard, delres.deletedText, yank_side::left);
        note_undo(buf, std::move(delres));
        return handled_undo_killring(state, buf);
    } else {
        // We actually do want to yank, and combine yanks with successive yanks.  Right or
        // left yank side doesn't matter except for string concatenation efficiency.  Note
        // that we can't "do nothing" -- if state->clipboard.justRecorded is false, we
        // need to create an empty string clipboard entry.  That's what this record_yank
        // call does.
        record_yank(&state->clipboard, buffer_string{}, yank_side::right);
        note_nop_undo(buf);
        return handled_undo_killring(state, buf);
    }
}

undo_killring_handled copy_region(state *state, buffer *buf) {
    if (!buf->mark.has_value()) {
        // TODO: Display error
        // (We do NOT want no_yank here.)  We do want to disrupt the undo action chain (if only because Emacs does that).
        note_nop_undo(buf);
        return handled_undo_killring(state, buf);
    }
    size_t mark = *buf->mark;
    size_t cursor = buf->cursor();
    size_t region_beg = std::min(mark, cursor);
    size_t region_end = std::max(mark, cursor);

    note_nop_undo(buf);
    record_yank(&state->clipboard, buf->copy_substr(region_beg, region_end), yank_side::none);
    return handled_undo_killring(state, buf);
}

undo_killring_handled delete_keypress(state *state, buffer *buf) {
    delete_result res = delete_char(buf);
    // TODO: Here, and perhaps in general, handle cases where no characters were actually deleted.
    return note_coalescent_action(state, buf, std::move(res));
}

// TODO: This rotation is stupid and makes no sense for multi-window, tabs, etc. -- use a
// buffer_number to point at the buf instead.
void rotate_to_buffer(state *state, buffer_number buf_number) {
    logic_check(buf_number.value < state->buflist.size(), "rotate_to_buffer with out-of-range buffer number %zu", buf_number.value);

    note_navigate_away_from_buf(buffer_ptr(state, buffer_number{state::topbuf_index_is_0}));

    std::rotate(state->buflist.begin(), state->buflist.begin() + buf_number.value, state->buflist.end());
}

// I guess we're rotating our _pointer_ into the buf list to the right, by rotating the
// bufs to the left.
undo_killring_handled rotate_buf_right(state *state, buffer *activeBuf) {
    undo_killring_handled ret = note_navigation_action(state, activeBuf);
    if (!state->is_normal()) {
        return ret;
    }

    note_navigate_away_from_buf(activeBuf);

    logic_checkg(!state->buflist.empty());

    buffer lastBuf = std::move(state->buflist.front());
    state->buflist.erase(state->buflist.begin());
    state->buflist.push_back(std::move(lastBuf));

    return ret;
}

// I guess we're rotating our _pointer_ into the buf list to the left, by rotating the
// bufs to the right.
undo_killring_handled rotate_buf_left(state *state, buffer *activeBuf) {
    undo_killring_handled ret = note_navigation_action(state, activeBuf);
    if (!state->is_normal()) {
        return ret;
    }

    note_navigate_away_from_buf(activeBuf);

    logic_checkg(!state->buflist.empty());

    buffer nextBuf = std::move(state->buflist.back());
    state->buflist.pop_back();
    state->buflist.insert(state->buflist.begin(), std::move(nextBuf));

    return ret;
}

undo_killring_handled yank_from_clipboard(state *state, buffer *buf) {
    std::optional<const buffer_string *> text = do_yank(&state->clipboard);
    if (text.has_value()) {
        insert_result res = insert_chars(buf, (*text)->data(), (*text)->size());
        note_undo(buf, std::move(res));
        return handled_undo_killring(state, buf);
    } else {
        note_nop_undo(buf);
        return handled_undo_killring(state, buf);
    }
    // Note that this gets called directly by C-y and by alt_yank_from_clipboard as a
    // helper.  Possibly false-DRY (someday).
}

undo_killring_handled alt_yank_from_clipboard(state *state, buffer *buf) {
    if (state->clipboard.justYanked.has_value()) {
        // TODO: this code will be wrong with undo impled -- the deletion and insertion should be a single undo chunk -- not a problem here but is this a bug in jsmacs?
        size_t amount_to_delete = *state->clipboard.justYanked;
        state->clipboard.stepPasteNumber();
        std::optional<const buffer_string *> text = do_yank(&state->clipboard);
        logic_check(text.has_value(), "with justYanked non-null, do_yank returns null");

        delete_result delres = delete_left(buf, amount_to_delete);
        insert_result insres = insert_chars(buf, (*text)->data(), (*text)->size());

        // Add the reverse action to undo history.
        int8_t rmd = -(delres.modificationFlagDelta.value + insres.modificationFlagDelta.value);
        atomic_undo_item item = {
            .beg = insres.new_cursor,
            .text_inserted = std::move(delres.deletedText),
            .text_deleted = std::move(insres.insertedText),
            .mod_delta = modification_delta{ rmd },
            .side = Side::left,
        };
        add_edit(&buf->undo_info, std::move(item));
        return handled_undo_killring(state, buf);
    } else {
        note_nop_undo(buf);
        return handled_undo_killring(state, buf);
    }
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

    // TODO: Named constants for these keyboard keys and such.
    if (ch == '\t' || (ch >= 32 && ch < 127)) {
        insert_result res = insert_char(active_buf, ch);
        return note_coalescent_action(state, active_buf, std::move(res));
    }
    if (ch == 13) {
        return enter_key(term, state);
    }
    if (ch == 28) {
        // Ctrl+backslash
        *exit_loop = true;
        return undo_killring_handled{};
        // TODO: Drop exit var and just break; here?  We have a spurious redraw.  Or just abort?
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
                            // TODO: Handle Insert key.
                            return unimplemented_keypress();

                        // (Yes, the escape codes aren't as contiguous as you'd expect.)
                        case 15: return rotate_buf_right(state, active_buf);  // F5
                        case 17: return rotate_buf_left(state, active_buf);  // F6
                        case 18: return buffer_switch_action(state, active_buf);  // F7
                        case 19: return nop_keypress();  // F8
                        case 20: return nop_keypress();  // F9
                        case 21: return nop_keypress();  // F10
                        case 24: return nop_keypress();  // F12
                        default:
                            break;
                        }
                    } else {
                        uint8_t numbers_second = *numbers.second;
                        if (numbers.first == 3 && numbers_second == 2) {
                            // TODO: Handle Shift+Del key.
                            return unimplemented_keypress();
                        }
                    }
                }
            } else {
                switch (ch) {
                case 'C':
                    move_right(active_buf);
                    return note_navigation_action(state, active_buf);
                case 'D':
                    move_left(active_buf);
                    return note_navigation_action(state, active_buf);
                case 'A':
                    move_up(active_buf);
                    return note_navigation_action(state, active_buf);
                case 'B':
                    move_down(active_buf);
                    return note_navigation_action(state, active_buf);
                case 'H':
                    move_home(active_buf);
                    return note_navigation_action(state, active_buf);
                case 'F':
                    move_end(active_buf);
                    return note_navigation_action(state, active_buf);
                default:
                    break;
                }
            }
        } else {
            switch (ch) {
            case 'f':
                // M-f
                move_forward_word(active_buf);
                return note_navigation_action(state, active_buf);
            case 'b':
                move_backward_word(active_buf);
                return note_navigation_action(state, active_buf);
            case 'q':
                return buffer_close_action(state, active_buf);
            case 'y':
                return alt_yank_from_clipboard(state, active_buf);
            case 'd':
                return delete_forward_word(state, active_buf);
            case ('?' ^ CTRL_XOR_MASK):
                return delete_backward_word(state, active_buf);
            case 'w':
                return copy_region(state, active_buf);
            case 'O': {
                check_read_tty_char(term, &ch);
                chars_read.push_back(ch);
                switch (ch) {
                case 'P': return nop_keypress();  // F1
                case 'Q': return nop_keypress();  // F2
                case 'R': return nop_keypress();  // F3
                case 'S': return nop_keypress();  // F4
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
        // Ctrl+Backspace
        return delete_backward_word(state, active_buf);
    }

    if (uint8_t(ch) <= 127) {
        switch (ch ^ CTRL_XOR_MASK) {
        case 'A':
            move_home(active_buf);
            return note_navigation_action(state, active_buf);
        case 'B':
            move_left(active_buf);
            return note_navigation_action(state, active_buf);
        case 'D':
            return delete_keypress(state, active_buf);
        case 'E':
            move_end(active_buf);
            return note_navigation_action(state, active_buf);
        case 'F':
            move_right(active_buf);
            return note_navigation_action(state, active_buf);
        case 'G':
            return cancel_key(state, active_buf);
        case 'N':
            move_down(active_buf);
            return note_navigation_action(state, active_buf);
        case 'O':
            return open_file_action(state, active_buf);
        case 'P':
            move_up(active_buf);
            return note_navigation_action(state, active_buf);
        case 'S':
            // May prompt if the buf isn't married to a file.
            return save_file_action(state, active_buf);
        case '?': {
            // TODO: Here, and perhaps elsewhere, handle undo where no characters were actually deleted.
            delete_result res = backspace_char(active_buf);
            return note_coalescent_action(state, active_buf, std::move(res));
        } break;
        case 'K':
            return kill_line(state, active_buf);
        case 'W':
            return kill_region(state, active_buf);
        case 'Y':
            return yank_from_clipboard(state, active_buf);
        case '@':
            // Ctrl+Space same as C-@
            set_mark(active_buf);
            return note_backout_action(state, active_buf);
        case '_':
            perform_undo(active_buf);
            return undo_killring_handled{};
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
