#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
// TODO: Remove unused includes^

#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

#include "error.hpp"
#include "io.hpp"
#include "state.hpp"
#include "terminal.hpp"

namespace fs = std::filesystem;
using qwi::buffer_char;

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

// A coordinate relative to the terminal frame (as opposed to some smaller buffer window).
struct terminal_coord { uint32_t row = 0, col = 0; };
struct terminal_frame {
    // Carries the presumed window size that the frame was rendered for.
    terminal_size window;
    // Cursor pos (0..<window.*)

    // nullopt means it's invisible.
    std::optional<terminal_coord> cursor;

    // data.size() = u32_mul(window.rows, window.cols).
    std::vector<char> data;
};

terminal_frame init_frame(const terminal_size& window) {
    terminal_frame ret;
    ret.data.resize(u32_mul(window.rows, window.cols));
    ret.window = window;
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

void draw_empty_frame_for_exit(int fd, const terminal_size& window) {
    terminal_frame frame = init_frame(window);
    for (size_t i = 0; i < frame.data.size(); ++i) {
        frame.data[i] = ' ';
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

    f.read(reinterpret_cast<char *>(ret.data()), ret.size());
    runtime_check(!f.fail(), "error reading file %s", path.c_str());

    return ret;
}

qwi::state initial_state(const command_line_args& args, const terminal_size& window) {
    const size_t n_files = args.files.size();

    std::vector<std::basic_string<buffer_char>> file_content;
    file_content.reserve(n_files);
    std::vector<fs::path> filenames;
    filenames.reserve(n_files);
    for (const std::string& spath : args.files) {
        fs::path path = spath;
        file_content.push_back(read_file(path));
        fs::path filename = path.filename();
        filenames.push_back(filename);
    }

    std::vector<fs::path> sortedNames = filenames;
    std::sort(sortedNames.begin(), sortedNames.end());
    {
        auto it = std::adjacent_find(sortedNames.begin(), sortedNames.end());
        if (it != sortedNames.end()) {
            // TODO: Stupid.
            runtime_fail("duplicate filenames '%s' not allowed", it->c_str());
        }
    }

    qwi::state state;
    if (n_files == 0) {
        state.buf.set_window(qwi::window_size{.rows = window.rows, .cols = window.cols});
        state.buf.name = "*scratch*";
    } else {
        state.buf.set_window(qwi::window_size{.rows = window.rows, .cols = window.cols});
        state.buf.name = filenames.at(0);
        state.buf.aft = std::move(file_content.at(0));

        state.bufs.reserve(n_files - 1);
        for (size_t i = 1; i < n_files; ++i) {
            state.bufs.emplace_back();
            auto& buf = state.bufs.back();
            buf.set_window(qwi::window_size{.rows = window.rows, .cols = window.cols});
            buf.name = filenames.at(i);
            buf.aft = std::move(file_content.at(i));
        }
    }
    return state;
}

struct char_rendering {
    char buf[8];
    size_t count;  // SIZE_MAX means newline
};

constexpr uint8_t CTRL_XOR_MASK = 64;
constexpr uint8_t TAB_MOD_MASK = 7;  // 8 is hard-coded tab stop

// Returns true if not '\n'.  Sets *line_col in any case.  Calls emit_drawn_chars(char *,
// size_t) once to pass out chars to be rendered in the terminal (except when a newline is
// encountered).  Always passes a count of 1 or greater to emit_drawn_chars.
char_rendering compute_char_rendering(const buffer_char bch, size_t *line_col) {
    const uint8_t ch = bch.value;
    char_rendering ret;
    if (ch == '\n') {
        *line_col = 0;
        ret.count = SIZE_MAX;
    } else {
        if (ch == '\t') {
            size_t next_line_col = size_add((*line_col) | TAB_MOD_MASK, 1);
            //             12345678
            char buf[8] = { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
            memcpy(ret.buf, buf, sizeof(ret.buf));
            ret.count = next_line_col - *line_col;
        } else if (ch < 32 || ch == 127) {
            ret.buf[0] = '^';
            ret.buf[1] = char(ch ^ CTRL_XOR_MASK);
            ret.count = 2;
        } else {
            // I guess 128-255 get rendered verbatim.
            ret.buf[0] = char(ch);
            ret.count = 1;
        }
        *line_col += ret.count;
    }
    return ret;
}

size_t pos_current_column(const qwi::buffer& buf, const size_t pos) {
    size_t line_col = 0;
    bool saw_newline = false;
    for (size_t i = pos - qwi::distance_to_beginning_of_line(buf, pos); i < pos; ++i) {
        buffer_char ch = buf.get(i);
        char_rendering rend = compute_char_rendering(ch, &line_col);
        saw_newline |= (rend.count == SIZE_MAX);
    }
    runtime_check(!saw_newline, "encountered impossible newline in pos_current_column");

    return line_col;
}

size_t current_column(const qwi::buffer& buf) {
    return pos_current_column(buf, buf.cursor());
}

struct render_coord {
    size_t buf_pos;
    std::optional<terminal_coord> rendered_pos;
};

void render_frame(terminal_frame *frame_ptr, const qwi::buffer& buf, std::vector<render_coord> *coords);

bool too_small_to_render(const terminal_size& window) {
    return window.cols < 2 || window.rows == 0;
}

bool cursor_is_offscreen(qwi::buffer *buf, size_t cursor) {
    terminal_size window = terminal_size{buf->window.rows, buf->window.cols};
    if (too_small_to_render(window)) {
        // Return false?
        return false;
    }

    // We might say this is generic code -- even if the buf is for a smaller window, this
    // terminal frame is artificially constructed.

    if (cursor < buf->first_visible_offset) {
        // Take this easy early exit.  Note that sometimes when cursor ==
        // buf->first_visible_offset we still will return true.
        return true;
    }

    terminal_frame frame = init_frame(window);
    std::vector<render_coord> coords = { {cursor, std::nullopt} };
    render_frame(&frame, *buf, &coords);
    return !coords[0].rendered_pos.has_value();
}

// Scrolls buf so that buf_pos is close to rowno.  (Sometimes it can't get there, e.g. we
// can't scroll past front of buffer, or a very narrow window might force buf_pos's row <
// rowno without equality).
void scroll_to_row(qwi::buffer *buf, const uint32_t rowno, const size_t buf_pos) {
    // We're going to back up and render one line at a time.
    const size_t window_cols = buf->window.cols;

    size_t rows_stepbacked = 0;
    size_t pos = buf_pos;
    for (;;) {
        size_t col = pos_current_column(*buf, pos);
        size_t row_in_line = col / window_cols;
        rows_stepbacked += row_in_line;
        pos = pos - qwi::distance_to_beginning_of_line(*buf, pos);
        if (rows_stepbacked == rowno || pos == 0) {
            // First visible offset is pos, at beginning of line.
            buf->first_visible_offset = pos;
            return;
        } else if (rows_stepbacked < rowno) {
            // pos > 0, as we tested.
            --pos;
            ++rows_stepbacked;
        } else {
            // We stepped back too far -- first_visible_offset >= pos and <= previous
            // value of pos.
            break;
        }
    }

    // We stepped back too far.  We have rows_stepbacked > rowno.  pos is the beginning of
    // the line.  We want to walk pos forward until we've rendered `rows_stepbacked -
    // rowno` lines, and compute a new first_visible_offset.

    size_t i = pos;
    size_t line_col = 0;
    size_t col = 0;
    bool saw_newline = false;
    for (;; ++i) {
        if (i == buf_pos) {
            // idk how this would be possible; just a simple way to prove no infinite traversal.
            buf->first_visible_offset = pos;
            break;
        }

        buffer_char ch = buf->get(i);
        char_rendering rend = compute_char_rendering(ch, &line_col);
        saw_newline |= (rend.count == SIZE_MAX);
        col += rend.count == SIZE_MAX ? 0 : rend.count;
        while (col >= window_cols) {
            --rows_stepbacked;
            col -= window_cols;
            if (rows_stepbacked == rowno) {
                // Now what?  If col > 0, then first_visible_offset is i.  If col == 0, then
                // first_visible_offset is i + 1.
                buf->first_visible_offset = i + (col == 0);

                goto done_loop;
            }
        }
    }
 done_loop:
    runtime_check(!saw_newline, "encountered impossible newline in scroll_to_row");
}

// Scrolls buf so that buf_pos is close to the middle (as close as possible, e.g. if it's
// too close to the top of the buffer, it'll be above the middle).  buf_pos is probably
// the cursor position.
void scroll_to_mid(qwi::buffer *buf, size_t buf_pos) {
    scroll_to_row(buf, buf->window.rows / 2, buf_pos);
}

void recenter_cursor_if_offscreen(qwi::buffer *buf) {
    if (cursor_is_offscreen(buf, buf->cursor())) {
        scroll_to_mid(buf, buf->cursor());
    }
}



void redraw_state(int term, const terminal_size& window, const qwi::state& state) {
    terminal_frame frame = init_frame(window);

    if (!too_small_to_render(window)) {
        // TODO: Support resizing.
        runtime_check(window.cols == state.buf.window.cols, "window cols changed");
        runtime_check(window.rows == state.buf.window.rows, "window rows changed");

        std::vector<render_coord> coords = { {state.buf.cursor(), std::nullopt} };
        render_frame(&frame, state.buf, &coords);
        frame.cursor = coords[0].rendered_pos;
    }

    write_frame(term, frame);
}

// render_coords must be sorted by buf_pos.
// render_frame doesn't render the cursor -- that's computed with render_coords and rendered then.
void render_frame(terminal_frame *frame_ptr, const qwi::buffer& buf, std::vector<render_coord> *render_coords) {
    terminal_frame& frame = *frame_ptr;
    const qwi::window_size window = buf.window;
    // This is a runtime check of current hard-coded behavior -- there is one buf window.
    runtime_check(window.rows == frame.window.rows, "frame window rows mismatches buf window rows");
    runtime_check(window.cols == frame.window.cols, "frame window cols mismatches buf window cols");

    // first_visible_offset is the first rendered character in the buffer -- this may be a
    // tab character or 2-column-rendered control character, only part of which was
    // rendered.  We render the entire line first_visible_offset was part of, and
    // copy_row_if_visible conditionally copies the line into the `frame` for rendering --
    // taking care to call it before incrementing i for partially rendered characters, and
    // _after_ incrementing i for the completely rendered character.

    // TODO: We actually don't want to re-render a whole line
    size_t i = buf.first_visible_offset - distance_to_beginning_of_line(buf, buf.first_visible_offset);

    std::vector<char> render_row(window.cols, 0);
    size_t render_coords_begin = 0;
    size_t render_coords_end = 0;
    size_t line_col = 0;
    size_t col = 0;
    size_t row = 0;
    // This gets called after we paste our character into the row and i is the offset
    // after the last completely written character.  Called precisely when col ==
    // window.cols.
    auto copy_row_if_visible = [&]() {
        col = 0;
        if (i > buf.first_visible_offset) {
            // It simplifies code to throw in this (row < window.rows) check here, instead
            // of carefully calculating where we might need to check it.
            if (row < window.rows) {
                memcpy(&frame.data[row * window.cols], render_row.data(), window.cols);
                while (render_coords_begin < render_coords_end) {
                    (*render_coords)[render_coords_begin].rendered_pos->row = row;
                    ++render_coords_begin;
                }
            }
            ++row;
        }
        // Note that this only does anything if the while loop above wasn't hit.
        while (render_coords_begin < render_coords_end) {
            (*render_coords)[render_coords_begin].rendered_pos = std::nullopt;
            ++render_coords_begin;
        }
    };
    size_t render_coord_target = render_coords_end < render_coords->size() ? (*render_coords)[render_coords_end].buf_pos : SIZE_MAX;
    while (row < window.rows && i < buf.size()) {
        // col < window.cols.
        while (i == render_coord_target) {
            (*render_coords)[render_coords_end].rendered_pos = {UINT32_MAX, uint32_t(col)};
            ++render_coords_end;
            render_coord_target = render_coords_end < render_coords->size() ? (*render_coords)[render_coords_end].buf_pos : SIZE_MAX;
        }

        buffer_char ch = buf.get(i);

        char_rendering rend = compute_char_rendering(ch, &line_col);
        if (rend.count != SIZE_MAX) {
            // Always, count > 0.
            for (size_t j = 0; j < rend.count - 1; ++j) {
                render_row[col] = rend.buf[j];
                ++col;
                if (col == window.cols) {
                    copy_row_if_visible();
                }
            }
            render_row[col] = rend.buf[rend.count - 1];
            ++col;
            ++i;
            if (col == window.cols) {
                copy_row_if_visible();
            }
        } else {
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

    while (i == render_coord_target) {
        (*render_coords)[render_coords_end].rendered_pos = {UINT32_MAX, uint32_t(col)};
        ++render_coords_end;
        render_coord_target = render_coords_end < render_coords->size() ? (*render_coords)[render_coords_end].buf_pos : SIZE_MAX;
    }

    // If we reached end of buffer, we might still need to copy the current render_row and
    // the remaining screen.
    while (row < window.rows) {
        do {
            render_row[col] = ' ';
            ++col;
        } while (col < window.cols);
        memcpy(&frame.data[row * window.cols], render_row.data(), window.cols);
        while (render_coords_begin < render_coords_end) {
            (*render_coords)[render_coords_begin].rendered_pos->row = row;
            ++render_coords_begin;
        }
        ++row;
        col = 0;
    }
    while (render_coords_begin < render_coords_end) {
        (*render_coords)[render_coords_begin].rendered_pos = std::nullopt;
        ++render_coords_begin;
    }
}

void insert_chars(qwi::buffer *buf, const buffer_char *chs, size_t count) {
    size_t og_cursor = buf->cursor();
    buf->bef.append(chs, count);
    if (buf->mark.has_value()) {
        *buf->mark += (*buf->mark > og_cursor ? count : 0);
    }
    // TODO: Don't recompute virtual_column every time.
    buf->virtual_column = current_column(*buf);
    buf->first_visible_offset += (buf->first_visible_offset > og_cursor ? count : 0);
    recenter_cursor_if_offscreen(buf);
}

void insert_char(qwi::buffer *buf, buffer_char sch) {
    insert_chars(buf, &sch, 1);
}
void insert_char(qwi::buffer *buf, char sch) {
    buffer_char ch = {uint8_t(sch)};
    insert_chars(buf, &ch, 1);
}
// Cheap fn for debugging purposes.
void push_printable_repr(std::basic_string<buffer_char> *str, char sch);
void insert_printable_repr(qwi::buffer *buf, char sch) {
    std::basic_string<buffer_char> str;
    push_printable_repr(&str, sch);
    insert_chars(buf, str.data(), str.size());
}

void update_offset_for_delete_range(size_t *offset, size_t range_beg, size_t range_end) {
    if (*offset > range_end) {
        *offset -= (range_end - range_beg);
    } else if (*offset > range_beg) {
        *offset = range_beg;
    }
}

void delete_left(qwi::buffer *buf, size_t count) {
    count = std::min<size_t>(count, buf->bef.size());
    size_t og_cursor = buf->bef.size();
    size_t new_cursor = og_cursor - count;
    buf->bef.resize(new_cursor);
    if (buf->mark.has_value()) {
        update_offset_for_delete_range(&*buf->mark, new_cursor, og_cursor);
    }

    buf->virtual_column = current_column(*buf);
    update_offset_for_delete_range(&buf->first_visible_offset, new_cursor, og_cursor);
    recenter_cursor_if_offscreen(buf);
}

void backspace_char(qwi::buffer *buf) {
    delete_left(buf, 1);
}

void delete_right(qwi::buffer *buf, size_t count) {
    size_t cursor = buf->cursor();
    count = std::min<size_t>(count, buf->aft.size());
    buf->aft.erase(0, count);
    if (buf->mark.has_value()) {
        update_offset_for_delete_range(&*buf->mark, cursor, cursor + count);
    }

    // TODO: We don't do this for doDeleteRight (or doAppendRight) in jsmacs -- the bug is in jsmacs!
    buf->virtual_column = current_column(*buf);
    update_offset_for_delete_range(&buf->first_visible_offset, cursor, cursor + count);
    recenter_cursor_if_offscreen(buf);
}
void delete_char(qwi::buffer *buf) {
    delete_right(buf, 1);
}
void kill_line(qwi::buffer *buf) {
    // TODO: Store killed lines and clumps in kill ring.
    size_t eolDistance = qwi::distance_to_eol(*buf, buf->cursor());

    if (eolDistance == 0 && buf->cursor() < buf->size()) {
        // TODO: Record yank of this newline character.
        delete_right(buf, 1);
    } else {
        delete_right(buf, eolDistance);
    }
}

void move_right_by(qwi::buffer *buf, size_t count) {
    count = std::min<size_t>(count, buf->aft.size());
    buf->bef.append(buf->aft, 0, count);
    buf->aft.erase(0, count);
    // TODO: Should we set virtual_column if count is 0?  (Can count be 0?)
    buf->virtual_column = current_column(*buf);
    recenter_cursor_if_offscreen(buf);
}

void move_right(qwi::buffer *buf) {
    move_right_by(buf, 1);
}

void move_left_by(qwi::buffer *buf, size_t count) {
    // TODO: Could both this and move_right_by be the same fn, using buf->set_cursor?
    count = std::min<size_t>(count, buf->bef.size());
    buf->aft.insert(0, buf->bef, buf->bef.size() - count, count);
    buf->bef.resize(buf->bef.size() - count);
    // TODO: Should we set virtual_column if count is 0?  (Can count be 0?)
    buf->virtual_column = current_column(*buf);
    recenter_cursor_if_offscreen(buf);
}

void move_left(qwi::buffer *buf) {
    if (buf->bef.empty()) {
        return;
    }
    buf->aft.insert(buf->aft.begin(), buf->bef.back());
    buf->bef.pop_back();
    buf->virtual_column = current_column(*buf);
}

bool is_solid(buffer_char bch) {
    uint8_t ch = bch.value;
    // TODO: Figure out what chars should be in this set.
    // Used by move_forward_word, move_backward_word.
    return (ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9');
}

size_t forward_word_distance(const qwi::buffer *buf) {
    const size_t cursor = buf->cursor();
    size_t i = cursor;
    bool reachedSolid = false;
    for (; i < buf->size(); ++i) {
        buffer_char ch = buf->get(i);
        if (is_solid(ch)) {
            reachedSolid = true;
        } else if (reachedSolid) {
            break;
        }
    }
    return i - cursor;
}

size_t backward_word_distance(const qwi::buffer *buf) {
    const size_t cursor = buf->cursor();
    size_t count = 0;
    bool reachedSolid = false;
    while (count < cursor) {
        buffer_char ch = buf->get(cursor - (count + 1));
        if (is_solid(ch)) {
            reachedSolid = true;
        } else if (reachedSolid) {
            break;
        }

        ++count;
    }
    return count;
}

void move_forward_word(qwi::buffer *buf) {
    size_t d = forward_word_distance(buf);
    move_right_by(buf, d);
}

void move_backward_word(qwi::buffer *buf) {
    size_t d = backward_word_distance(buf);
    move_left_by(buf, d);
}

void move_up(qwi::buffer *buf) {
    const size_t window_cols = buf->window.cols;
    // We're not interested in virtual_column as a "line column" -- just interested in the
    // visual distance to beginning of line.
    const size_t target_column = buf->virtual_column % window_cols;

    // Basically we want the output of the following algorithm:
    // 1. Render the current line up to the cursor.
    // 2. Note the row the cursor's on.
    // 3. Back up to the previous row.
    // 4. If at least one character starts on the preceding row, set the cursor to the greater of:
    //    - the last character whose starting column is <= virtual_column
    //    - the first character that starts on the preceding row
    // 4-b. If NO characters start on the preceding row (e.g. window_cols == 3 and we have
    // a tab character), set the cursor to the last character that starts before the
    // preceding row.

    // Note that Emacs (in GUI mode, at least) never encounters the 4-b case because it
    // switches to line truncation when the window width gets low.

    const size_t cursor = buf->cursor();
    const size_t bol1 = cursor - buf->cursor_distance_to_beginning_of_line();
    const size_t bol = bol1 == 0 ? 0 : (bol1 - 1) - qwi::distance_to_beginning_of_line(*buf, bol1 - 1);

    // So we're going to render forward from bol, which is the beginning of the previous
    // "real" line.  For each row, we'll track the current proposed cursor position,
    // should that row end up being the previous line.
    size_t col = 0;
    size_t line_col = 0;
    size_t prev_row_cursor_proposal = SIZE_MAX;
    size_t current_row_cursor_proposal = bol;
    for (size_t i = bol; i < cursor; ++i) {
        buffer_char ch = buf->get(i);
        char_rendering rend = compute_char_rendering(ch, &line_col);
        if (rend.count == SIZE_MAX) {
            // is eol
            prev_row_cursor_proposal = current_row_cursor_proposal;
            col = 0;
            current_row_cursor_proposal = i + 1;
        } else {
            col += rend.count;
            if (col >= window_cols) {
                // Line wrapping case.
                col -= window_cols;
                prev_row_cursor_proposal = current_row_cursor_proposal;
                if (col >= window_cols) {
                    // Special narrow window case.
                    prev_row_cursor_proposal = i;
                    do {
                        col -= window_cols;
                    } while (col >= window_cols);
                }

                current_row_cursor_proposal = i + 1;
            } else {
                if (col <= target_column) {
                    current_row_cursor_proposal = i + 1;
                }
            }
        }
    }

    if (prev_row_cursor_proposal == SIZE_MAX) {
        // We're already on the top row.
        return;
    }
    buf->set_cursor(prev_row_cursor_proposal);
    recenter_cursor_if_offscreen(buf);
}

void move_down(qwi::buffer *buf) {
    const size_t window_cols = buf->window.cols;
    const size_t target_column = buf->virtual_column % window_cols;

    // Remember we do some traversing in current_column.
    size_t line_col = current_column(*buf);
    size_t col = line_col % window_cols;

    // Simple: We walk forward until the number of rows traversed is >= 1 _and_ we're at
    // either the first char of the row or the last char whose col is <= target_column.
    // We use candidate_index != SIZE_MAX to determine if we've entered the next line.

    size_t candidate_index = SIZE_MAX;
    for (size_t i = buf->cursor(), e = buf->size(); i < e; ++i) {
        buffer_char ch = buf->get(i);
        char_rendering rend = compute_char_rendering(ch, &line_col);
        if (rend.count == SIZE_MAX) {
            if (candidate_index != SIZE_MAX) {
                break;
            }
            col = 0;
            // The first index of the next line is always a candidate.
            candidate_index = i + 1;
        } else {
            col += rend.count;
            if (col >= window_cols) {
                if (candidate_index != SIZE_MAX) {
                    break;
                }
                do {
                    col -= window_cols;
                } while (col >= window_cols);
                // The first index of the next line is always a candidate.
                candidate_index = i + 1;
            } else {
                if (candidate_index != SIZE_MAX && col <= target_column) {
                    candidate_index = i + 1;
                }
            }
        }
    }

    if (candidate_index == SIZE_MAX) {
        candidate_index = buf->size();
    }

    buf->set_cursor(candidate_index);
    recenter_cursor_if_offscreen(buf);
}

void move_home(qwi::buffer *buf) {
    // TODO: Use uh, screen home and screen end?
    size_t distance = buf->cursor_distance_to_beginning_of_line();
    move_left_by(buf, distance);
}

void move_end(qwi::buffer *buf) {
    size_t distance = qwi::distance_to_eol(*buf, buf->cursor());
    move_right_by(buf, distance);
}

void set_mark(qwi::buffer *buf) {
    buf->mark = buf->cursor();
}

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
        } else if (ch == 'f') {
            // M-f
            move_forward_word(&state->buf);
            chars_read.clear();
        } else if (ch == 'b') {
            // M-b
            move_backward_word(&state->buf);
            chars_read.clear();
        }
        // Insert for the user (the developer, me) unrecognized escape codes.
        if (!chars_read.empty()) {
            insert_char(&state->buf, '\\');
            insert_char(&state->buf, 'e');
            for (char c : chars_read) {
                insert_char(&state->buf, c);
            }
        }
    } else if (uint8_t(ch) <= 127) {
        switch (ch ^ CTRL_XOR_MASK) {
        case 'A':
            move_home(&state->buf);
            break;
        case 'B':
            move_left(&state->buf);
            break;
        case 'D':
            delete_char(&state->buf);
            break;
        case 'E':
            move_end(&state->buf);
            break;
        case 'F':
            move_right(&state->buf);
            break;
        case 'N':
            move_down(&state->buf);
            break;
        case 'P':
            move_up(&state->buf);
            break;
        case '?':
            backspace_char(&state->buf);
            break;
        case 'K':
            kill_line(&state->buf);
            break;
        case 'W':
        case 'Y':
        case '@':
            // Ctrl+Space same as C-@
            set_mark(&state->buf);
            break;
        default:
            // For now we do push the printable repr for any unhandled chars, for debugging purposes.
            // TODO: Handle other possible control chars.
            insert_printable_repr(&state->buf, ch);
        }
    } else {
        // TODO: Handle high characters -- do we just insert them, or do we validate
        // UTF-8, or what?
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
