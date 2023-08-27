#ifndef QWERTILLION_STATE_HPP_
#define QWERTILLION_STATE_HPP_

#include <optional>
#include <string>
#include <vector>

#include "error.hpp"

struct terminal_size;

namespace qwi {

struct window_size { uint32_t rows = 0, cols = 0; };

struct buffer_char {
    uint8_t value;

    static buffer_char from_char(char ch) { return buffer_char{uint8_t(ch)}; }
    friend auto operator<=>(buffer_char, buffer_char) = default;
};

using buffer_string = std::basic_string<buffer_char>;

buffer_string to_buffer_string(const std::string& s);

enum class Side { left, right, };

struct atomic_undo_item {
    // The cursor _before_ we apply this undo action.  This departs from jsmacs, where
    // it's the cursor after the action, or something incoherent and broken.
    size_t beg = 0;
    buffer_string text_inserted{};
    buffer_string text_deleted{};
    Side side = Side::left;
};

struct undo_item {
    // This duplicates the jsmacs undo implementation.
    // TODO: Figure out how we want to capitalize types.
    enum class Type { atomic, mountain, };

    // TODO: This should be a variant or something.
    Type type;
    // Type::atomic:
    atomic_undo_item atomic;

    // Type::mountain:
    std::vector<atomic_undo_item> history{};
};

struct undo_history {
    // TODO: There are no limits on undo history size.
    std::vector<undo_item> past;
    std::vector<atomic_undo_item> future;

    // If the last typed action is a sequence of characters, delete keypresses, or
    // backspace keypresses, we combine those events into a single undo operation.
    enum class char_coalescence {
        none,
        insert_char,
        delete_right,
        delete_left,
    };
    char_coalescence coalescence = char_coalescence::none;
};

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
    enum class type { file_open, file_save, buffer_switch };
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
inline qwi::buffer *buffer_ptr(qwi::state *state, buffer_number buf_number) {
    logic_checkg(buf_number.value < state->buflist.size());
    return &state->buflist[buf_number.value];
}

inline const qwi::buffer *buffer_ptr(const qwi::state *state, buffer_number buf_number) {
    logic_checkg(buf_number.value < state->buflist.size());
    return &state->buflist[buf_number.value];
}

// TODO: Rename to be buffer_name_linear_time
std::string buffer_name_str(const qwi::state *state, buffer_number buf_number);
qwi::buffer_string buffer_name(const qwi::state *state, buffer_number buf_number);


constexpr uint32_t STATUS_AREA_HEIGHT = 1;
void resize_window(state *st, const terminal_size& new_window);
window_size main_buf_window_from_terminal_window(const terminal_size& term_window);

inline void close_status_prompt(state *st) {
    st->status_prompt = std::nullopt;
}

size_t distance_to_eol(const qwi::buffer& buf, size_t pos);
size_t distance_to_beginning_of_line(const qwi::buffer& buf, size_t pos);

void record_yank(clip_board *clb, const buffer_string& deletedText, yank_side side);
std::optional<const buffer_string *> do_yank(clip_board *clb);

void no_yank(clip_board *clb);

void break_coalescence(undo_history *history);
void add_nop_edit(undo_history *history);
void add_edit(undo_history *history, atomic_undo_item&& item);
void add_coalescent_edit(undo_history *history, atomic_undo_item&& item, undo_history::char_coalescence coalescence);


void perform_undo(buffer *buf);

inline char *as_chars(qwi::buffer_char *chs) {
    static_assert(sizeof(*chs) == sizeof(char));
    return reinterpret_cast<char *>(chs);
}

inline const char *as_chars(const qwi::buffer_char *chs) {
    static_assert(sizeof(*chs) == sizeof(char));
    return reinterpret_cast<const char *>(chs);
}

inline qwi::buffer_char *as_buffer_chars(char *chs) {
    static_assert(sizeof(*chs) == sizeof(qwi::buffer_char));
    return reinterpret_cast<qwi::buffer_char *>(chs);
}

inline const qwi::buffer_char *as_buffer_chars(const char *chs) {
    static_assert(sizeof(*chs) == sizeof(qwi::buffer_char));
    return reinterpret_cast<const qwi::buffer_char *>(chs);
}

}  // namespace qwi

#endif  // QWERTILLION_STATE_HPP_
