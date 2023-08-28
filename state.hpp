#ifndef QWERTILLION_STATE_HPP_
#define QWERTILLION_STATE_HPP_

#include <optional>
#include <string>
#include <vector>

#include "error.hpp"
#include "undo.hpp"

// TODO: Don't need this.
struct terminal_size;

namespace qwi {

struct window_size { uint32_t rows = 0, cols = 0; };

struct buffer_number {
    // 0..n-1.  So state->bufs[value - 1] is the buffer, and state->buf is the zero buffer.
    size_t value;
};

struct buffer {
    // Used to choose in the list of buffers, unique to the buffer.  Program logic should
    // not allow empty or overlong values.  (name_str, name_number) pairs should be
    // unique.
    std::string name_str;
    uint64_t name_number = 0;  // TODO: Maybe can be size_t.

    std::optional<std::string> married_file;

    // We just have strings for text before/after the cursor.  Very bad perf.
    buffer_string bef;
    buffer_string aft;
    // TODO: Our model for modified_flag is fundamentally broken, because when a file gets saved, we'd have to traverse our undo history to fix the modification flag deltas -- and arguably, there are multiple points where we'd have to set the flag back on!
    bool modified_flag = false;

    // Absolute position of the mark, if there is one.
    std::optional<size_t> mark;

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

    /* UI-specific stuff -- this could get factored out of buffer at some point */

    // Column that is maintained as we press up and down arrow keys past shorter lines.
    // For now, this assumes a monospace font (maybe with 2x-width glyphs) on all GUIs.
    size_t virtual_column = 0;

    // This is and will continue to be the size of the text window -- does not include any
    // status bar rows, even if there is one per buffer.
    window_size window;
    // 0 <= first_visible_offset <= size().
    size_t first_visible_offset = 0;

    // Returns distance_to_beginning_of_line(*this, this->cursor()).
    size_t cursor_distance_to_beginning_of_line() const;

    void set_window(const window_size& win) { window = win; }

    std::string copy_to_string() const;

    buffer_string copy_substr(size_t beg, size_t end) const;

    static buffer from_data(buffer_string&& data);
};

struct prompt {
    enum class type { file_open, file_save, buffer_switch, buffer_close };
    type typ;
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
    // Sorted in order from least-recently-used -- `buf` is the active buffer and should
    // get pushed onto the end of bufs after some other buf takes its place.
    //
    // Is never empty (after initial_state() returns).
    // NOTE: "Is never empty" may be a UI-specific constraint!  It seems reasonable for GUIs to support having no tabs open.
    std::vector<buffer> buflist;
    static constexpr size_t topbuf_index_is_0 = 0;
    buffer& topbuf() {
        logic_checkg(!buflist.empty());
        return buflist.front();
    }

    std::optional<prompt> status_prompt;
    bool is_normal() const { return !status_prompt.has_value(); }

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

// TODO: This should be defined in whatever header buffer_string and buffer_char get declared.
inline char *as_chars(buffer_char *chs) {
    static_assert(sizeof(*chs) == sizeof(char));
    return reinterpret_cast<char *>(chs);
}

inline const char *as_chars(const buffer_char *chs) {
    static_assert(sizeof(*chs) == sizeof(char));
    return reinterpret_cast<const char *>(chs);
}

inline buffer_char *as_buffer_chars(char *chs) {
    static_assert(sizeof(*chs) == sizeof(buffer_char));
    return reinterpret_cast<buffer_char *>(chs);
}

inline const buffer_char *as_buffer_chars(const char *chs) {
    static_assert(sizeof(*chs) == sizeof(buffer_char));
    return reinterpret_cast<const buffer_char *>(chs);
}

}  // namespace qwi

#endif  // QWERTILLION_STATE_HPP_
