#ifndef QWERTILLION_STATE_HPP_
#define QWERTILLION_STATE_HPP_

#include <optional>
#include <string>
#include <vector>

struct terminal_size;

namespace qwi {

struct window_size { uint32_t rows = 0, cols = 0; };

struct buffer_char {
    uint8_t value;

    static buffer_char from_char(char ch) { return buffer_char{uint8_t(ch)}; }
    friend auto operator<=>(buffer_char, buffer_char) = default;
};

struct buffer {
    // Used to choose in the list of buffers, unique to the buffer.  Program logic should
    // not allow empty or overlong values.
    std::string name;
    std::optional<std::string> married_file;

    // We just have strings for text before/after the cursor.  Very bad perf.
    std::basic_string<buffer_char> bef;
    std::basic_string<buffer_char> aft;

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

    // Column that is maintained as we press up and down arrow keys past shorter lines.
    /* UI-specific stuff -- this could get factored out of buffer at some point */
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
};

struct prompt {
    enum class type { file_open, file_save, };
    type typ;
    buffer buf;
};

struct state {
    // Sorted in order from least-recently-used -- `buf` is the active buffer and should
    // get pushed onto the end of bufs after some other buf takes its place.
    buffer buf;
    std::vector<buffer> bufs;

    std::optional<prompt> status_prompt;
};

constexpr uint32_t STATUS_AREA_HEIGHT = 1;
void resize_window(state *st, const terminal_size& new_window);
window_size main_buf_window_from_terminal_window(const terminal_size& term_window);

size_t distance_to_eol(const qwi::buffer& buf, size_t pos);
size_t distance_to_beginning_of_line(const qwi::buffer& buf, size_t pos);

struct clipboard {
    // A list of strings stored in the clipboard.
    std::vector<std::string> clips;
    // Did we just record some text?  Future text recordings will be appended to the
    // previous.  For example, if we typed C-k C-k C-k, we'd want those contiguous
    // cuttings to be concatenated into one.
    bool justRecorded = false;
    // How many times have we pasted in a row, using M-y?
    size_t pasteNumber = 0;
    // Did we just yank some text from the clipboard?  This number tells how much text we
    // just yanked.
    std::optional<size_t> justYanked;
};

}  // namespace qwi

inline char *as_chars(qwi::buffer_char *chs) {
    static_assert(sizeof(*chs) == sizeof(char));
    return reinterpret_cast<char *>(chs);
}

inline const char *as_chars(const qwi::buffer_char *chs) {
    static_assert(sizeof(*chs) == sizeof(char));
    return reinterpret_cast<const char *>(chs);
}


#endif  // QWERTILLION_STATE_HPP_
