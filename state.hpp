#ifndef QWERTILLION_STATE_HPP_
#define QWERTILLION_STATE_HPP_

#include <inttypes.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "error.hpp"
#include "keyboard.hpp"
#include "region_stats.hpp"
#include "undo.hpp"
// TODO: We don't want this dependency exactly -- we kind of want ui info to be separate from state.
// Well, right now it's part of state -- this'll get resolved once we have a second GUI.
#include "terminal_size.hpp"

namespace qwi {

struct insert_result;
struct delete_result;
struct scratch_frame;

struct window_size {
    uint32_t rows = 0, cols = 0;
    bool operator==(const window_size&) const = default;
};

// An index into a ui_window::window_ctxs vector.  (Since tabs can be added or removed,
// these are easily invalidated!)
struct tab_number {
    size_t value;
};

struct buffer_id {
    uint64_t value;
    friend auto operator<=>(buffer_id x, buffer_id y) = default;
};

}  // namespace qwi

template<>
struct std::hash<qwi::buffer_id> {
    std::size_t operator()(const qwi::buffer_id& id) const noexcept {
        return std::hash<uint64_t>{}(id.value);
    }
};

namespace qwi {

struct mark_id {
    // index into marks array
    size_t index = SIZE_MAX;
};

// We remove detach checks because we haven't defined move constructors that leave the
// object in a "valid" state by the reasoning of the detach checks.
#define RUN_DETACH_CHECKS 0

struct ui_window_ctx {
    explicit ui_window_ctx(mark_id fvo, mark_id initial_cursor)
        : first_visible_offset(fvo), cursor_mark(initial_cursor) { }

#if RUN_DETACH_CHECKS
    ~ui_window_ctx() {
        logic_check(first_visible_offset.index == SIZE_MAX,
                    "ui_window_ctx was not detached from buffer");
    }
#endif

    // Column that is maintained as we press up and down arrow keys past shorter lines.
    // For now, this assumes a monospace font (maybe with 2x-width glyphs) on all GUIs.
    // If it's nullopt, it should be treated equivalently as if we computed the cursor()'s column
    // right now.
    std::optional<size_t> virtual_column;

    // This is and will continue to be the size of the text window -- does not include any
    // status bar rows, even if there is one per buffer.  Can be nullopt, which means the
    // buf doesn't have a last-rendered window.  Scrolling code and virtual_column code
    // just have to handle this case.
    std::optional<window_size> rendered_window;

    size_t window_cols_or_maxval() const {
        // TODO: This is trashy -- return a std::optional<size_t> and deal with it.
        return rendered_window.has_value() ? rendered_window->cols : SIZE_MAX;
    }

    // TODO: I think this is improperly treated by insert_chars_right, if the cursor is at
    // the first_visible_offset -- it pushes the f.v.o. forward.
    mark_id first_visible_offset;

    // This is gross, and we manually update this whenever we move the cursor or edit the buf.
    mark_id cursor_mark;

    // TODO: Rename set_last_rendered_window to set_last_rendered_size.
    void set_last_rendered_window(const window_size& win) {
        if (!rendered_window.has_value() || *rendered_window != win) {
            rendered_window = win;
            virtual_column = std::nullopt;
        }
    }
};

void detach_ui_window_ctx(buffer *buf, ui_window_ctx *ui);

void ensure_virtual_column_initialized(ui_window_ctx *ui, const buffer *buf);

struct buffer {
    buffer() = delete;
    explicit buffer(buffer_id _id) : id(_id), undo_info(), non_modified_undo_node(undo_info.current_node) { }
    explicit buffer(buffer_id _id, buffer_string&& str)
        : id(_id),
          bef_(std::move(str)),
          bef_stats_(compute_stats(bef_ /* careful about initialization order */)),
          undo_info(), non_modified_undo_node(undo_info.current_node) { }

    buffer_id id;

    // Used to choose in the list of buffers, unique to the buffer.  Program logic should
    // not allow empty or overlong values.  (name_str, name_number) pairs should be
    // unique.
    std::string name_str;
    uint64_t name_number = 0;  // TODO: Maybe can be size_t.

    std::optional<std::string> married_file;

private:
    // SIZE_MAX is the value for invalid, removed, reusable marks.
    // The values of these range within `0 <= marks[_] <= size()`.
    std::vector<size_t> marks;

    friend void add_to_marks_as_of(buffer *buf, size_t first_offset, size_t count);
    friend void update_marks_for_delete_range(buffer *buf, size_t range_beg, size_t range_end);

public:
    // An excessively(?) simple interface for mark handling.
    mark_id add_mark(size_t offset);
    size_t get_mark_offset(mark_id id) const;
    void remove_mark(mark_id id);

    // Same as remove_mark and add_mark.
    void replace_mark(mark_id, size_t offset);

    // Buffer content is private to ensure that everything respects read-only.
private:

    // We just have strings for text before/after the cursor.  Very bad perf.
    buffer_string bef_;
    region_stats bef_stats_;

    buffer_string aft_;
    region_stats aft_stats_;

    // True friends, necessary mutation functions.
    friend insert_result insert_chars(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, const buffer_char *chs, size_t count);
    friend insert_result insert_chars_right(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, const buffer_char *chs, size_t count);
    friend delete_result delete_left(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, size_t og_count);
    friend delete_result delete_right(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, size_t og_count);

    // Half friends -- a function I don't want to exist.
    friend void force_insert_chars_end_before_cursor(
        buffer *buf, const buffer_char *chs, size_t count);

    // False friends, that access bef and aft, but we'd like them to use the buf more abstractly.
    friend void move_right_by(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, size_t count);
    friend void move_left_by(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, size_t count);
    friend void save_buf_to_married_file_and_mark_unmodified(buffer *buf);
    friend buffer open_file_into_detached_buffer(state *state, const std::string& dirty_path);

    static void stats_to_line_info(const region_stats& stats, size_t *line_out, size_t *col_out) {
        *line_out = stats.newline_count + 1;
        *col_out = stats.last_line_size;
    }
public:
    void line_info_at_pos(size_t pos, size_t *line_out, size_t *col_out) const;

    // Absolute position of the mark, if there is one.
    std::optional<mark_id> mark;

    bool read_only = false;

    // TODO: Remove these as public functions.
    size_t cursor() const { return bef_.size(); }
    void set_cursor(size_t pos);

    size_t cursor_() const { return cursor(); }
    void set_cursor_(size_t pos) { set_cursor(pos); }

public:
    size_t size() const { return bef_.size() + aft_.size(); }
    buffer_char at(size_t i) const {
        return i < bef_.size() ? bef_[i] : aft_.at(i - bef_.size());
    }
    buffer_char get(size_t i) const {
        return i < bef_.size() ? bef_[i] : aft_[i - bef_.size()];
    }

    /* Undo info -- tracked per-buffer, apparently.  In principle, undo history could be a
       global ordered bag of past actions (including undo actions) but instead it's per
       buffer. */
    undo_history undo_info;

    undo_node_number non_modified_undo_node;

    bool modified_flag() const { return non_modified_undo_node != undo_info.current_node; }

    // Returns distance_to_beginning_of_line(*this, this->cursor()).
    size_t cursor_distance_to_beginning_of_line() const;

    std::string copy_to_string() const;

    buffer_string copy_substr(size_t beg, size_t end) const;

    static buffer from_data(buffer_id id, buffer_string&& data);
};

inline size_t get_ctx_cursor(const ui_window_ctx *ui, const buffer *buf) {
    return buf->get_mark_offset(ui->cursor_mark);
}
inline void set_ctx_cursor(ui_window_ctx *ui, buffer *buf) {
    buf->replace_mark(ui->cursor_mark, buf->cursor_());
}

// Loads and sets cursor_mark.  It's ugly.  We'll soon remove buf->cursor() (as an
// externally exposed concept) altogether.  TODO: Remove these (replacing some callers with set_ctx_cursor).
// TODO: These should be defined in state.cpp, not wherever they are.
void load_ctx_cursor(ui_window_ctx *ui, buffer *buf);
void save_ctx_cursor(ui_window_ctx *ui, buffer *buf);

// Generated and returned to indicate that the code exhaustively handles undo and killring behavior.
struct [[nodiscard]] undo_killring_handled { };

// TODO: All keypresses should be implemented.
inline undo_killring_handled unimplemented_keypress() { return undo_killring_handled{}; }

inline undo_killring_handled nop_keypress() { return undo_killring_handled{}; }

struct prompt {
    enum class type { proc, };
    type typ;
    buffer buf;

    std::string messageText;

    std::function<undo_killring_handled(state *st, buffer&& promptBuf, bool *exit_loop)> procedure;  // only for proc

    ui_window_ctx win_ctx{buf.add_mark(0), buf.add_mark(0)};
};

struct popup {
    buffer buf;
    ui_window_ctx win_ctx{buf.add_mark(0), buf.add_mark(0)};
};

struct clip_board {
    // TODO: There are no limits on kill ring size.
    // A list of strings stored in the clipboard.
    std::vector<buffer_string> clips;
    // Did we just record some text?  Future text recordings will be appended to the
    // previous.  For example, if we typed C-k C-k C-k, we'd want those contiguous
    // cuttings to be concatenated into one.
    bool justRecorded = false;
    // How many times have we pasted in a row, using M-y?
    size_t pasteNumber = 0;
    // Did we just yank some text from the clipboard?  This number tells how much text we
    // just yanked.
    std::optional<size_t> justYanked;

    void stepPasteNumber() { pasteNumber += 1; }
};

enum class yank_side { left, right, none, };

struct ui_mode {
    bool ansi_terminal = true;
};

struct window_id {
    uint64_t value;
};

struct ui_window {
    explicit ui_window(window_id x) : id(x) {}
    ui_window(ui_window&&) = default;
    ui_window& operator=(ui_window&&) = default;

    NO_COPY(ui_window);

#if RUN_DETACH_CHECKS
    ~ui_window() {
        logic_check(window_ctxs.empty(), "window_ctxs were not detached");
    }
#endif
    // TODO: Make use of window_id or remove it.
    window_id id;

    // 0 <= active_tab < window_ctxs.size() and 1 <= window_ctxs.size(), except after
    // initialization or when we prepare for destruction.
    tab_number active_tab = {SIZE_MAX};

    // We use std::unique_ptr for the same reason as state::buflist.
    std::vector<std::pair<buffer_id, std::unique_ptr<ui_window_ctx>>> window_ctxs;

    // Makes the buffer the active tab.
    ui_window_ctx *point_at(buffer_id id, state *st);

    void note_rendered_window_size(
        buffer_id buf_id, const window_size& window_size);

    const std::pair<buffer_id, std::unique_ptr<ui_window_ctx>>& active_buf() {
        return window_ctxs.at(active_tab.value);
    }

    // TODO: Violates what we want from const correctness (as ui_window_ctx is indirect).
    const std::pair<buffer_id, std::unique_ptr<ui_window_ctx>>& active_buf() const {
        return window_ctxs.at(active_tab.value);
    }

    // Returns true if the window no longer has any buffers!  It immediately needs at
    // least one buffer.
    [[nodiscard]] bool detach_if_attached(buffer *buf);
};

void detach_all_bufs_from_ui_window(state *state, ui_window *win);

// The UI-presented "window number" is 1 greater than this value.
// Generally, these have to be maintained if the window layout changes.
struct window_number {
    size_t value;
};

struct window_layout {
    window_layout()
        : active_window{0},
          row_relsizes{1},
          column_datas{{.relsize = 1, .num_rows = 1}} {

        // TODO: Why can't we construct this?
        windows.push_back(ui_window(gen_next_window_id()));
    }
    NO_COPY(window_layout);
    window_layout(window_layout&&) = default;
    window_layout& operator=(window_layout&&) = default;

private:
    uint64_t next_window_id_value = 0;

public:
    window_id gen_next_window_id() {
        return {next_window_id_value++};
    }

    // The windows have a numbering order (for Alt+1, Alt+2 shortcuts, etc.).  They are
    // stored in this order here.  So window #N is at offset N-1.
    std::vector<ui_window> windows;  // The windows (all with unique window ids).

    window_number active_window;

    terminal_size last_rendered_terminal_size = {.rows = 1, .cols = 1};

    // "relsize" = "relative size" -- needs to be normalized for actual terminal size.
    std::vector<uint32_t> row_relsizes;
    struct col_data {
        uint32_t relsize;
        size_t num_rows;
    };
    std::vector<col_data> column_datas;

    void sanity_check() const;
};

struct state {
    state();
    ~state();

    NO_COPY(state);
    state(state&&) = default;
    state& operator=(state&&) = default;

    // Just a set of buffers.  Buffer order is defined by tab order in ui_window.
    // TODO: Should buf_set include the prompt buffer (and the popup buffer?)
    std::unordered_map<buffer_id, std::unique_ptr<buffer>> buf_set;

    // Right now we only have one window.
    window_layout layout;

    // TODO: Unfortunate name clash with layout.active_window field; I'd like layout to
    // have the same function.
    const ui_window *active_window() const {
        return &layout.windows.at(layout.active_window.value);
    }
    ui_window *active_window() {
        return &layout.windows.at(layout.active_window.value);
    }
    std::pair<ui_window *, ui_window *> win_range() {
        return {layout.windows.data(), layout.windows.data() + layout.windows.size()};
    }

private:
    uint64_t next_buf_id_value = 0;

public:
    buffer *lookup(buffer_id id) {
        auto it = buf_set.find(id);
        logic_check(it != buf_set.end(), "buffer not found: id=%" PRIu64, id.value);
        return it->second.get();
    }
    const buffer *lookup(buffer_id id) const {
        auto it = buf_set.find(id);
        logic_check(it != buf_set.end(), "buffer not found: id=%" PRIu64, id.value);
        return it->second.get();
    }

    buffer_id gen_buf_id() { return buffer_id{next_buf_id_value++}; }

    std::optional<buffer_id> pick_buf_for_empty_window() const {
        auto it = buf_set.begin();
        if (it != buf_set.end()) {
            return it->first;
        } else {
            return std::nullopt;
        }
    }

    // C-x prefixes and the like, but not M-x, which would be something like a prompt.
    // We don't enumerate them in types or anything -- they're handled dynamically.
    std::vector<keypress> keyprefix;

    std::optional<prompt> status_prompt;
    bool is_normal() const { return !status_prompt.has_value(); }

    std::optional<popup> popup_display;

    void add_message(const std::string& msg);
    // TODO: Every use of this function is probably a bad place for UI logic.
    void note_error_message(std::string&& msg);
    void clear_error_message() { live_error_message = ""; }
    std::string live_error_message;  // empty means there is none

    clip_board clipboard;

    ui_mode ui_config;

    std::unique_ptr<scratch_frame> scratch_;
    scratch_frame *scratch() { return scratch_.get(); }
};

// TODO: Rename to be buffer_name_linear_time
std::string buffer_name_str(const state *state, buffer_id buf_id);
buffer_string buffer_name(const state *state, buffer_id buf_id);


constexpr uint32_t STATUS_AREA_HEIGHT = 1;
void resize_window(state *st, const terminal_size& new_window);
window_size main_buf_window_from_terminal_window(const terminal_size& term_window);

inline void do_close_status_prompt(prompt *prmpt) {
    detach_ui_window_ctx(&prmpt->buf, &prmpt->win_ctx);
}

inline void close_status_prompt(state *st) {
    if (st->status_prompt.has_value()) {
        do_close_status_prompt(&*st->status_prompt);
        st->status_prompt = std::nullopt;
    }
}

size_t distance_to_eol(const buffer& buf, size_t pos);
size_t distance_to_beginning_of_line(const buffer& buf, size_t pos);

void record_yank(clip_board *clb, const buffer_string& deletedText, yank_side side);
std::optional<const buffer_string *> do_yank(clip_board *clb);

void no_yank(clip_board *clb);

}  // namespace qwi

#endif  // QWERTILLION_STATE_HPP_
