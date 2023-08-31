#ifndef QWERTILLION_STATE_HPP_
#define QWERTILLION_STATE_HPP_

#include <inttypes.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "error.hpp"
#include "undo.hpp"

// TODO: Don't need this.
struct terminal_size;

namespace qwi {

struct insert_result;
struct delete_result;

struct window_size { uint32_t rows = 0, cols = 0; };

struct buffer_number {
    // 0..n-1.  So state->bufs[value - 1] is the buffer, and state->buf is the zero buffer.
    size_t value;
};

struct buffer_id {
    uint64_t value;
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

struct buffer {
    buffer() = delete;
    explicit buffer(buffer_id _id) : id(_id), undo_info(), non_modified_undo_node(undo_info.current_node) {
        first_visible_offset = add_mark(0);
    }
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
    buffer_string bef;
    buffer_string aft;

    // True friends, necessary mutation functions.
    friend insert_result insert_chars(buffer *buf, const buffer_char *chs, size_t count);
    friend insert_result insert_chars_right(buffer *buf, const buffer_char *chs, size_t count);
    friend delete_result delete_left(buffer *buf, size_t og_count);
    friend delete_result delete_right(buffer *buf, size_t og_count);

    // Half friends -- a function I don't want to exist.
    friend void force_insert_chars_end_before_cursor(
        buffer *buf, const buffer_char *chs, size_t count);

    // False friends, that access bef and aft, but we'd like them to use the buf more abstractly.
    friend void move_right_by(buffer *buf, size_t count);
    friend void move_left_by(buffer *buf, size_t count);
    friend void save_buf_to_married_file_and_mark_unmodified(buffer *buf);
    friend buffer open_file_into_detached_buffer(state *state, const std::string& dirty_path);

public:
    // Absolute position of the mark, if there is one.
    std::optional<mark_id> mark;

    bool read_only = false;

    size_t cursor() const { return bef.size(); }
    void set_cursor(size_t pos);
    size_t size() const { return bef.size() + aft.size(); }
    buffer_char at(size_t i) const {
        return i < bef.size() ? bef[i] : aft.at(i - bef.size());
    }
    buffer_char get(size_t i) const {
        return i < bef.size() ? bef[i] : aft[i - bef.size()];
    }

    /* Undo info -- tracked per-buffer, apparently.  In principle, undo history could be a
       global ordered bag of past actions (including undo actions) but instead it's per
       buffer. */
    undo_history undo_info;

    undo_node_number non_modified_undo_node;

    bool modified_flag() const { return non_modified_undo_node != undo_info.current_node; }

    /* UI-specific stuff -- this could get factored out of buffer at some point */

    // Column that is maintained as we press up and down arrow keys past shorter lines.
    // For now, this assumes a monospace font (maybe with 2x-width glyphs) on all GUIs.
    // If it's nullopt, it should be treated equivalently as if we computed the cursor()'s column
    // right now.
    std::optional<size_t> virtual_column;
    void ensure_virtual_column_initialized();

    // This is and will continue to be the size of the text window -- does not include any
    // status bar rows, even if there is one per buffer.
    window_size window;
    mark_id first_visible_offset;

    // Returns distance_to_beginning_of_line(*this, this->cursor()).
    size_t cursor_distance_to_beginning_of_line() const;

    void set_window(const window_size& win) { window = win; }

    std::string copy_to_string() const;

    buffer_string copy_substr(size_t beg, size_t end) const;

    static buffer from_data(buffer_id id, buffer_string&& data);
};

struct prompt {
    enum class type { file_open, file_save, buffer_switch, buffer_close, exit_without_save };
    type typ;
    buffer buf;
    constexpr static const char *const message_unused = "";
    std::string messageText;  // only for save_prompt
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
    // TODO: Remove term.
    explicit state(int _term) : term(_term) { }
    int term = -1;

    // Sorted in order from least-recently-used -- `buf` is the active buffer and should
    // get pushed onto the end of bufs after some other buf takes its place.
    //
    // Is never empty (after initial_state() returns).
    // NOTE: "Is never empty" may be a UI-specific constraint!  It seems reasonable for GUIs to support having no tabs open.
    std::vector<buffer> buflist;

private:
    uint64_t next_buf_id_value = 0;

public:
    buffer_id gen_buf_id() { return buffer_id{next_buf_id_value++}; }

    // 0 <= buf_ptr < buflist.size(), always (except at construction when buflist is empty).
    buffer_number buf_ptr = {SIZE_MAX};

    buffer& topbuf() {
        logic_check(buf_ptr.value < buflist.size(), "topbuf: out of range.  buf_ptr = %zu, buflist.size() = %zu",
                    buf_ptr.value, buflist.size());
        return buflist[buf_ptr.value];
    }

    std::optional<prompt> status_prompt;
    bool is_normal() const { return !status_prompt.has_value(); }

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
    return &state->buflist[buf_number.value];
}

inline const buffer *buffer_ptr(const state *state, buffer_number buf_number) {
    logic_checkg(buf_number.value < state->buflist.size());
    return &state->buflist[buf_number.value];
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
