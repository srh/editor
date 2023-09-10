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
#include "region_stats.hpp"
#include "undo.hpp"

// TODO: Don't need this.
struct terminal_size;

namespace qwi {

struct insert_result;
struct delete_result;

struct window_size {
    uint32_t rows = 0, cols = 0;
    bool operator==(const window_size&) const = default;
};

struct buffer_number {
    // 0..n-1.  So state->bufs[value - 1] is the buffer, and state->buf is the zero buffer.
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

struct ui_window_ctx {
    explicit ui_window_ctx(mark_id fvo) : first_visible_offset(fvo) { }

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

    mark_id first_visible_offset;

    void set_last_rendered_window(const window_size& win) {
        if (!rendered_window.has_value() || *rendered_window != win) {
            rendered_window = win;
            virtual_column = std::nullopt;
        }
    }
};

void ensure_virtual_column_initialized(ui_window_ctx *ui, const buffer *buf);

struct buffer {
    buffer() = delete;
    explicit buffer(buffer_id _id) : id(_id), undo_info(), non_modified_undo_node(undo_info.current_node),
                                     win_ctx(add_mark(0)) { }
    explicit buffer(buffer_id _id, buffer_string&& str)
        : id(_id),
          bef_(std::move(str)),
          bef_stats_(compute_stats(bef_ /* careful about initialization order */)),
          undo_info(), non_modified_undo_node(undo_info.current_node),
          win_ctx(add_mark(0)) { }

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
    friend insert_result insert_chars(ui_window_ctx *ui, buffer *buf, const buffer_char *chs, size_t count);
    friend insert_result insert_chars_right(ui_window_ctx *ui, buffer *buf, const buffer_char *chs, size_t count);
    friend delete_result delete_left(ui_window_ctx *ui, buffer *buf, size_t og_count);
    friend delete_result delete_right(ui_window_ctx *ui, buffer *buf, size_t og_count);

    // Half friends -- a function I don't want to exist.
    friend void force_insert_chars_end_before_cursor(
        ui_window_ctx *ui, buffer *buf, const buffer_char *chs, size_t count);

    // False friends, that access bef and aft, but we'd like them to use the buf more abstractly.
    friend void move_right_by(ui_window_ctx *ui, buffer *buf, size_t count);
    friend void move_left_by(ui_window_ctx *ui, buffer *buf, size_t count);
    friend void save_buf_to_married_file_and_mark_unmodified(buffer *buf);
    friend buffer open_file_into_detached_buffer(state *state, const std::string& dirty_path);

public:
    void line_info(size_t *line, size_t *col) const {
        *line = bef_stats_.newline_count + 1;
        *col = bef_stats_.last_line_size;
    }

    // Absolute position of the mark, if there is one.
    std::optional<mark_id> mark;

    bool read_only = false;

    size_t cursor() const { return bef_.size(); }
    void set_cursor(size_t pos);
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

    /* UI-specific stuff -- this could get factored out of buffer at some point */
    ui_window_ctx win_ctx;

    // Returns distance_to_beginning_of_line(*this, this->cursor()).
    size_t cursor_distance_to_beginning_of_line() const;

    std::string copy_to_string() const;

    buffer_string copy_substr(size_t beg, size_t end) const;

    static buffer from_data(buffer_id id, buffer_string&& data);
};

// Generated and returned to indicate that the code exhaustively handles undo and killring behavior.
struct [[nodiscard]] undo_killring_handled { };

struct prompt {
    // TODO: Replace buffer_close, exit_without_save prompts with proc prompts (which should be trivial)
    enum class type { buffer_close, exit_without_save, proc, };
    type typ;
    buffer buf;

    constexpr static const char *const message_unused = "";
    std::string messageText;  // only for exit_without_save and proc (also, is used slightly differently)

    std::function<undo_killring_handled(state *st, buffer&& promptBuf, bool *exit_loop)> procedure;  // only for proc
    static decltype(procedure) procedure_unused() { return decltype(procedure)(); }
};

struct popup {
    buffer buf;
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

struct state {
    state() = default;

    // Sorted in order from least-recently-used -- `buf` is the active buffer and should
    // get pushed onto the end of bufs after some other buf takes its place.
    //
    // Is never empty (after initial_state() returns).
    // NOTE: "Is never empty" may be a UI-specific constraint!  It seems reasonable for GUIs to support having no tabs open.
    //
    // Carries a pointer dereference because a lot of code passes buffer*, and we have
    // note_error_message which may resize the buflist (by adding *Messages*) at any time.
    std::vector<std::unique_ptr<buffer>> buflist;

private:
    uint64_t next_buf_id_value = 0;

public:
    buffer_id gen_buf_id() { return buffer_id{next_buf_id_value++}; }

    // buf_at vs. buffer_ptr vs. the buf_ptr field below -- stupid name-dodging.
    // TODO: Dedup this with buffer_ptr.
    buffer& buf_at(buffer_number buf_number) {
        logic_checkg(buf_number.value < buflist.size());
        return *buflist[buf_number.value];
    }
    const buffer& buf_at(buffer_number buf_number) const {
        logic_checkg(buf_number.value < buflist.size());
        return *buflist[buf_number.value];
    }

    void note_rendered_window_sizes(
        const std::vector<std::pair<buffer_id, window_size>>& window_sizes);

    // Note that the status prompt buf has a separate win_ctx not looked up by this
    // function.  In general, win_ctx should take a window_number and a buffer_number.
    // But right now there's only one window.
    ui_window_ctx *win_ctx(buffer_number n) { return &buf_at(n).win_ctx; }
    const ui_window_ctx *win_ctx(buffer_number n) const { return &buf_at(n).win_ctx; }

    // This is going to be a per-window value at some point.
    // 0 <= buf_ptr < buflist.size(), always (except at construction when buflist is empty).
    buffer_number buf_ptr = {SIZE_MAX};

    buffer& topbuf() { return buf_at(buf_ptr); }
    const buffer& topbuf() const { return buf_at(buf_ptr); }

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
};

// An unstable pointer.
inline buffer *buffer_ptr(state *state, buffer_number buf_number) {
    logic_checkg(buf_number.value < state->buflist.size());
    return state->buflist[buf_number.value].get();
}

inline const buffer *buffer_ptr(const state *state, buffer_number buf_number) {
    logic_checkg(buf_number.value < state->buflist.size());
    return state->buflist[buf_number.value].get();
}

// TODO: Rename to be buffer_name_linear_time
std::string buffer_name_str(const state *state, buffer_number buf_number);
buffer_string buffer_name(const state *state, buffer_number buf_number);


constexpr uint32_t STATUS_AREA_HEIGHT = 1;
void resize_window(state *st, const terminal_size& new_window);
window_size main_buf_window_from_terminal_window(const terminal_size& term_window);

inline void close_status_prompt(state *st) {
    st->status_prompt = std::nullopt;
}

size_t distance_to_eol(const buffer& buf, size_t pos);
size_t distance_to_beginning_of_line(const buffer& buf, size_t pos);

void record_yank(clip_board *clb, const buffer_string& deletedText, yank_side side);
std::optional<const buffer_string *> do_yank(clip_board *clb);

void no_yank(clip_board *clb);

}  // namespace qwi

#endif  // QWERTILLION_STATE_HPP_
