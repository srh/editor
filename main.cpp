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
#include "buffer.hpp"
#include "error.hpp"
#include "io.hpp"
#include "movement.hpp"
#include "state.hpp"
#include "term_ui.hpp"
#include "terminal.hpp"

namespace fs = std::filesystem;
using qwi::buffer_char;

struct command_line_args {
    bool version = false;
    bool help = false;

    std::vector<std::string> files;
};

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

undo_killring_handled handled_undo_killring(qwi::state *state, qwi::buffer *buf) {
    (void)state, (void)buf;
    return undo_killring_handled{};
}

qwi::undo_item make_reverse_action(insert_result&& i_res) {
    using qwi::undo_item;

    undo_item item = {
        .type = undo_item::Type::atomic,
        .atomic = {
            .beg = i_res.new_cursor,
            .text_inserted = qwi::buffer_string{},
            .text_deleted = std::move(i_res.insertedText),
            .side = i_res.side,  // We inserted on left (right), hence we delete on left (right)
        },
    };

    return item;
}

void note_undo(qwi::buffer *buf, insert_result&& i_res) {
    using qwi::undo_item;

    // Make and add the _reverse_ action in the undo items.
    // (Why the reverse action?  Because jsmacs did it that way.)

    // TODO: Of course, in some cases we have reverseAddEdit -- but that's only when
    // actually undoing, so there are none yet.
    add_edit(&buf->undo_info, make_reverse_action(std::move(i_res)));
}

void note_nop_undo(qwi::buffer *buf) {
    add_nop_edit(&buf->undo_info);
}

undo_killring_handled note_action(qwi::state *state, qwi::buffer *buf, insert_result&& i_res) {
    no_yank(&state->clipboard);

    note_undo(buf, std::move(i_res));
    return undo_killring_handled{};
}

qwi::undo_item make_reverse_action(delete_result&& d_res) {
    using qwi::undo_item;

    // Make reverse action.
    undo_item item = {
        .type = undo_item::Type::atomic,
        .atomic = {
            .beg = d_res.new_cursor,
            .text_inserted = std::move(d_res.deletedText),
            .text_deleted = qwi::buffer_string{},
            .side = d_res.side,  // We deleted on left (right), hence we insert on left (right)
        },
    };

    return item;
}

void note_undo(qwi::buffer *buf, delete_result&& d_res) {
    add_edit(&buf->undo_info, make_reverse_action(std::move(d_res)));
}

undo_killring_handled note_action(qwi::state *state, qwi::buffer *buf, delete_result&& d_res) {
    no_yank(&state->clipboard);

    note_undo(buf, std::move(d_res));
    return undo_killring_handled{};
}

struct [[nodiscard]] noundo_killring_action { };
undo_killring_handled note_action(qwi::state *state, qwi::buffer *buf, const noundo_killring_action&) {
    no_yank(&state->clipboard);
    (void)buf;
    return undo_killring_handled{};
}

// Possibly a useless categorization -- maybe useful for refactoring later.
struct [[nodiscard]] navigation_action { };
undo_killring_handled note_action(qwi::state *state, qwi::buffer *buf, const navigation_action&) {
    no_yank(&state->clipboard);
    (void)buf;
    return undo_killring_handled{};
}

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

void draw_empty_frame_for_exit(int fd, const terminal_size& window) {
    terminal_frame frame = init_frame(window);
    if (!INIT_FRAME_INITIALIZES_WITH_SPACES) {
        for (size_t i = 0; i < frame.data.size(); ++i) {
            frame.data[i] = ' ';
        }
    }
    // TODO: Ensure cursor is restored on non-happy-paths.
    frame.cursor = {0, 0};

    write_frame(fd, frame);
}

std::basic_string<buffer_char> read_file(const fs::path& path) {
    if (!fs::is_regular_file(path)) {
        runtime_fail("Tried opening non-regular file %s", path.c_str());
    }

    static_assert(sizeof(buffer_char) == 1);
    std::basic_string<buffer_char> ret;
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



qwi::state initial_state(const command_line_args& args, const terminal_size& window) {
    const size_t n_files = args.files.size();

    std::vector<std::basic_string<buffer_char>> file_content;
    file_content.reserve(n_files);
    std::vector<std::string> bufNames;
    bufNames.reserve(n_files);
    /* TODO: Figure out the "correct" way to refer to a path.
         - we want to handle situations where the full path can't be resolved
     */
    std::vector<fs::path> og_paths;
    og_paths.reserve(n_files);
    for (const std::string& spath : args.files) {
        fs::path path = spath;
        og_paths.push_back(path);
        file_content.push_back(read_file(path));
        bufNames.push_back(buf_name_from_file_path(path));
    }

    std::vector<std::string> sortedNames = bufNames;
    std::sort(sortedNames.begin(), sortedNames.end());
    {
        auto it = std::adjacent_find(sortedNames.begin(), sortedNames.end());
        if (it != sortedNames.end()) {
            // TODO: Stupid.
            runtime_fail("duplicate filenames '%s' not allowed", it->c_str());
        }
    }

    qwi::window_size buf_window = qwi::main_buf_window_from_terminal_window(window);

    qwi::state state;
    if (n_files == 0) {
        state.buf.set_window(buf_window);
        state.buf.name = "*scratch*";
    } else {
        state.buf.set_window(buf_window);
        state.buf.name = bufNames.at(0);
        state.buf.married_file = og_paths.at(0).string();
        state.buf.aft = std::move(file_content.at(0));

        state.bufs.reserve(n_files - 1);
        for (size_t i = 1; i < n_files; ++i) {
            state.bufs.emplace_back();
            auto& buf = state.bufs.back();
            buf.set_window(buf_window);
            buf.name = bufNames.at(i);
            buf.married_file = og_paths.at(i).string();
            buf.aft = std::move(file_content.at(i));
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

void render_string(terminal_frame *frame, const terminal_coord& coord, const std::string& str) {
    uint32_t col = coord.col;  // <= frame->window.cols
    runtime_check(col <= frame->window.cols, "render_string: coord out of range");
    size_t line_col = 0;
    for (size_t i = 0; i < str.size() && col < frame->window.cols; ++i) {
        char_rendering rend = compute_char_rendering(buffer_char::from_char(str[i]), &line_col);
        if (rend.count == SIZE_MAX) {
            // Newline...(?)
            return;
        }
        size_t to_copy = std::min<size_t>(rend.count, frame->window.cols - col);
        memcpy(&frame->data[coord.row * frame->window.cols + col], rend.buf, to_copy);
        col += to_copy;
    }
}

// TODO: Non-const reference for state param -- we set its status_prompt's buf's window.
void render_status_area(terminal_frame *frame, qwi::state& state) {
    uint32_t last_row = u32_sub(frame->window.rows, 1);
    if (state.status_prompt.has_value()) {
        std::string message;
        switch (state.status_prompt->typ) {
        case qwi::prompt::type::file_open: message = "file to open: "; break;
        case qwi::prompt::type::file_save: message = "file to save: "; break;
        }
        render_string(frame, {.row = last_row, .col = 0}, message);

        std::vector<render_coord> coords = { {state.status_prompt->buf.cursor(), std::nullopt} };
        terminal_coord prompt_topleft = {.row = last_row, .col = uint32_t(message.size())};
        // TODO: Use resize_buf_window here, generally.
        state.status_prompt->buf.set_window({.rows = 1, .cols = frame->window.cols - prompt_topleft.col});
        render_into_frame(frame, prompt_topleft, state.status_prompt->buf, &coords);

        // TODO: This is super-hacky -- we overwrite the main buffer's cursor.
        frame->cursor = add(prompt_topleft, coords[0].rendered_pos);
    } else {
        render_string(frame, {.row = last_row, .col = 0}, state.buf.name);
    }
}

// TODO: non-const reference for state, passed into render_status_area
void redraw_state(int term, const terminal_size& window, qwi::state& state) {
    terminal_frame frame = init_frame(window);

    if (!too_small_to_render(state.buf.window)) {
        // TODO: Support resizing.
        runtime_check(window.cols == state.buf.window.cols, "window cols changed");
        runtime_check(window.rows == state.buf.window.rows + qwi::STATUS_AREA_HEIGHT, "window rows changed");

        std::vector<render_coord> coords = { {state.buf.cursor(), std::nullopt} };
        terminal_coord window_topleft = {0, 0};
        render_into_frame(&frame, window_topleft, state.buf, &coords);

        // TODO: This is super-hacky -- this gets overwritten if the status area has a
        // prompt.  With multiple buffers, we need some concept of an active buffer, with
        // an active cursor.
        // TODO: Also, we don't render our inactive cursor, and we should.
        frame.cursor = add(window_topleft, coords[0].rendered_pos);

        render_status_area(&frame, state);
    }

    write_frame(term, frame);
}

// Cheap fn for debugging purposes.
void push_printable_repr(std::basic_string<buffer_char> *str, char sch) {
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

undo_killring_handled insert_printable_repr(qwi::state *state, qwi::buffer *buf, char sch) {
    std::basic_string<buffer_char> str;
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

void save_buf_to_married_file(const qwi::buffer& buf) {
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

void set_save_prompt(qwi::state *state) {
    logic_check(!state->status_prompt.has_value(), "set_save_prompt with existing prompt");
    state->status_prompt = {qwi::prompt::type::file_save, qwi::buffer()};
    // TODO: How/where should we set the prompt's buf's window?
}

void save_file_action(qwi::state *state) {
    if (state->status_prompt.has_value()) {
        // Ignore keypress.
        return;
    }

    if (state->buf.married_file.has_value()) {
        save_buf_to_married_file(state->buf);
    } else {
        set_save_prompt(state);
    }
}

undo_killring_handled enter_key(qwi::state *state) {
    if (!state->status_prompt.has_value()) {
        insert_result res = insert_char(&state->buf, '\n');
        return note_action(state, &state->buf, std::move(res));
    } else {
        // end undo/kill ring stuff -- undo n/a because we're destructing the buf and haven't made changes.
        undo_killring_handled ret = note_action(state, &state->status_prompt->buf, noundo_killring_action{});
        // TODO: Of course, handle errors, such as if directory doesn't exist.
        std::string text = state->status_prompt->buf.copy_to_string();
        // TODO: Implement displaying errors to the user.
        if (text != "") {
            state->status_prompt = std::nullopt;
            state->buf.married_file = text;
            save_buf_to_married_file(state->buf);
            state->buf.name = buf_name_from_file_path(fs::path(text));
        }
        return ret;
    }
}

undo_killring_handled delete_backward_word(qwi::state *state, qwi::buffer *buf) {
    size_t d = backward_word_distance(buf);
    delete_result delres = delete_left(buf, d);
    record_yank(&state->clipboard, delres.deletedText, qwi::yank_side::left);
    note_undo(buf, std::move(delres));
    return handled_undo_killring(state, buf);
}

undo_killring_handled delete_forward_word(qwi::state *state, qwi::buffer *buf) {
    size_t d = forward_word_distance(buf);
    delete_result delres = delete_right(buf, d);
    record_yank(&state->clipboard, delres.deletedText, qwi::yank_side::right);
    note_undo(buf, std::move(delres));
    return handled_undo_killring(state, buf);
}

undo_killring_handled kill_line(qwi::state *state, qwi::buffer *buf) {
    size_t eolDistance = qwi::distance_to_eol(*buf, buf->cursor());

    delete_result delres;
    if (eolDistance == 0 && buf->cursor() < buf->size()) {
        delres = delete_right(buf, 1);
    } else {
        delres = delete_right(buf, eolDistance);
    }
    record_yank(&state->clipboard, delres.deletedText, qwi::yank_side::right);
    note_undo(buf, std::move(delres));
    return handled_undo_killring(state, buf);
}

undo_killring_handled kill_region(qwi::state *state, qwi::buffer *buf) {
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
        record_yank(&state->clipboard, delres.deletedText, qwi::yank_side::right);
        note_undo(buf, std::move(delres));
        return handled_undo_killring(state, buf);
    } else if (mark < cursor) {
        delete_result delres = delete_left(buf, cursor - mark);
        record_yank(&state->clipboard, delres.deletedText, qwi::yank_side::left);
        note_undo(buf, std::move(delres));
        return handled_undo_killring(state, buf);
    } else {
        // Do nothing (no clipboard actions either).
        // Same as above when there is no mark.
        note_nop_undo(buf);
        return handled_undo_killring(state, buf);
    }
}

undo_killring_handled delete_keypress(qwi::state *state, qwi::buffer *buf) {
    delete_result res = delete_char(buf);
    return note_action(state, buf, std::move(res));
}

undo_killring_handled yank_from_clipboard(qwi::state *state, qwi::buffer *buf) {
    std::optional<const qwi::buffer_string *> text = qwi::do_yank(&state->clipboard);
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

undo_killring_handled alt_yank_from_clipboard(qwi::state *state, qwi::buffer *buf) {
    if (state->clipboard.justYanked.has_value()) {
        // TODO: this code will be wrong with undo impled -- the deletion and insertion should be a single undo chunk -- not a problem here but is this a bug in jsmacs?
        size_t amount_to_delete = *state->clipboard.justYanked;
        state->clipboard.stepPasteNumber();
        std::optional<const qwi::buffer_string *> text = qwi::do_yank(&state->clipboard);
        logic_check(text.has_value(), "with justYanked non-null, do_yank returns null");

        delete_result delres = delete_left(buf, amount_to_delete);
        insert_result insres = insert_chars(buf, (*text)->data(), (*text)->size());

        // Add the reverse action to undo history.
        using qwi::undo_item;
        undo_item item = {
            .type = undo_item::Type::atomic,
            .atomic = {
                .beg = insres.new_cursor,
                .text_inserted = std::move(delres.deletedText),
                .text_deleted = std::move(insres.insertedText),
                .side = qwi::Side::left,
            },
        };
        add_edit(&buf->undo_info, std::move(item));
        return handled_undo_killring(state, buf);
    } else {
        note_nop_undo(buf);
        return handled_undo_killring(state, buf);
    }
}


undo_killring_handled read_and_process_tty_input(int term, qwi::state *state, bool *exit_loop) {
    // TODO: When term is non-blocking, we'll need to wait for readiness...?
    char ch;
    check_read_tty_char(term, &ch);

    qwi::buffer *active_buf = state->status_prompt.has_value() ? &state->status_prompt->buf : &state->buf;

    // TODO: Named constants for these keyboard keys and such.
    if (ch == 13) {
        return enter_key(state);
    } else if (ch == '\t' || (ch >= 32 && ch < 127)) {
        insert_result res = insert_char(active_buf, ch);
        return note_action(state, active_buf, std::move(res));
    } else if (ch == 28) {
        // Ctrl+backslash
        *exit_loop = true;
        return undo_killring_handled{};
        // TODO: Drop exit var and just break; here?  We have a spurious redraw.  Or just abort?
    } else if (ch == 27) {
        std::string chars_read;
        check_read_tty_char(term, &ch);
        chars_read.push_back(ch);
        // TODO: Handle all possible escapes...
        if (ch == '[') {
            check_read_tty_char(term, &ch);
            chars_read.push_back(ch);

            if (ch == 'C') {
                move_right(active_buf);
                return note_action(state, active_buf, navigation_action{});
            } else if (ch == 'D') {
                move_left(active_buf);
                return note_action(state, active_buf, navigation_action{});
            } else if (ch == 'A') {
                move_up(active_buf);
                return note_action(state, active_buf, navigation_action{});
            } else if (ch == 'B') {
                move_down(active_buf);
                return note_action(state, active_buf, navigation_action{});
            } else if (ch == 'H') {
                move_home(active_buf);
                return note_action(state, active_buf, navigation_action{});
            } else if (ch == 'F') {
                move_end(active_buf);
                return note_action(state, active_buf, navigation_action{});
            } else if (isdigit(ch)) {
                // TODO: Generic parsing of numeric/~ escape codes.
                if (ch == '3') {
                    check_read_tty_char(term, &ch);
                    chars_read.push_back(ch);
                    if (ch == '~') {
                        return delete_keypress(state, active_buf);
                    } else if (ch == ';') {
                        check_read_tty_char(term, &ch);
                        chars_read.push_back(ch);
                        if (ch == '2') {
                            check_read_tty_char(term, &ch);
                            chars_read.push_back(ch);
                            if (ch == '~') {
                                // TODO: Handle Shift+Del key.
                                chars_read.clear();
                                return unimplemented_keypress();
                            }
                        }
                    }
                } else if (ch == '2') {
                    check_read_tty_char(term, &ch);
                    chars_read.push_back(ch);
                    if (ch == '~') {
                        // TODO: Handle Insert key.
                        return unimplemented_keypress();
                    }
                }
            }
        } else if (ch == 'f') {
            // M-f
            move_forward_word(active_buf);
            return note_action(state, active_buf, navigation_action{});
        } else if (ch == 'b') {
            // M-b
            move_backward_word(active_buf);
            return note_action(state, active_buf, navigation_action{});
        } else if (ch == 'y') {
            return alt_yank_from_clipboard(state, active_buf);
        } else if (ch == 'd') {
            return delete_forward_word(state, active_buf);
        } else if (ch == ('?' ^ CTRL_XOR_MASK)) {
            // M-backspace
            return delete_backward_word(state, active_buf);
        }
        // Insert for the user (the developer, me) unrecognized escape codes.
        logic_check(!chars_read.empty(), "chars_read expected empty");
        {
            qwi::buffer_string str;
            str.push_back(buffer_char::from_char('\\'));
            str.push_back(buffer_char::from_char('e'));
            for (char c : chars_read) {
                str.push_back(buffer_char::from_char(c));
            }

            insert_result res = insert_chars(active_buf, str.data(), str.size());
            return note_action(state, active_buf, std::move(res));
        }
    } else if (ch == 8) {
        // Ctrl+Backspace
        return delete_backward_word(state, active_buf);
    } else if (uint8_t(ch) <= 127) {
        switch (ch ^ CTRL_XOR_MASK) {
        case 'A':
            move_home(active_buf);
            return note_action(state, active_buf, navigation_action{});
            break;
        case 'B':
            move_left(active_buf);
            return note_action(state, active_buf, navigation_action{});
            break;
        case 'D': {
            delete_result res = delete_char(active_buf);
            return note_action(state, active_buf, std::move(res));
        } break;
        case 'E':
            move_end(active_buf);
            return note_action(state, active_buf, navigation_action{});
            break;
        case 'F':
            move_right(active_buf);
            return note_action(state, active_buf, navigation_action{});
            break;
        case 'N':
            move_down(active_buf);
            return note_action(state, active_buf, navigation_action{});
            break;
        case 'P':
            move_up(active_buf);
            return note_action(state, active_buf, navigation_action{});
            break;
        case 'S':
            // May prompt if the buf isn't married to a file.
            save_file_action(state);
            return note_action(state, active_buf, noundo_killring_action{});
            break;
        case '?': {
            delete_result res = backspace_char(active_buf);
            return note_action(state, active_buf, std::move(res));
        } break;
        case 'K':
            return kill_line(state, active_buf);
            break;
        case 'W':
            return kill_region(state, active_buf);
            break;
        case 'Y':
            return yank_from_clipboard(state, active_buf);
            break;
        case '@':
            // Ctrl+Space same as C-@
            set_mark(active_buf);
            // TODO: We want this?
            return note_action(state, active_buf, noundo_killring_action{});
            break;
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
    qwi::state state = initial_state(args, window);

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
