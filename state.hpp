#ifndef QWERTILLION_STATE_HPP_
#define QWERTILLION_STATE_HPP_

#include <optional>
#include <string>
#include <vector>

namespace qwi {

struct window_size { uint32_t rows = 0, cols = 0; };

struct buffer {
    // Used to choose in the list of buffers, unique to the buffer.  Program logic should
    // not allow empty or overlong values.
    std::string name;

    // We just have strings for text before/after the cursor.  Very bad perf.
    std::string bef;
    std::string aft;

    // Absolute position of the mark, if there is one.
    std::optional<size_t> mark;

    size_t cursor() const { return bef.size(); }
    void set_cursor(size_t pos);
    size_t size() const { return bef.size() + aft.size(); }
    // TODO: Make wrapper type for char.
    char at(size_t i) const {
        return i < bef.size() ? bef[i] : aft.at(i - bef.size());
    }
    char operator[](size_t i) const {
        return i < bef.size() ? bef[i] : aft[i - bef.size()];
    }

    // Column that is maintained as we press up and down arrow keys past shorter lines.
    /* UI-specific stuff -- this could get factored out of buffer at some point */
    // For now, this assumes a monospace font (maybe with 2x-width glyphs) on all GUIs.
    size_t virtual_column = 0;

    window_size window;
    // 0 <= first_visible_offset <= size().
    size_t first_visible_offset = 0;
    // TODO: current_column is nonsense -- it doesn't account for tab characters or
    // double-wide.  We don't really want to have or use this function.
    size_t current_column() const;

    void set_window(const window_size& win) { window = win; }
};


struct state {
    // Sorted in order from least-recently-used -- `buf` is the active buffer and should
    // get pushed onto the end of bufs after some other buf takes its place.
    buffer buf;
    std::vector<buffer> bufs;
};

size_t distance_to_eol(const qwi::buffer& buf, size_t pos);
size_t distance_to_beginning_of_line(const qwi::buffer& buf, size_t pos);

}  // namespace qwi

#endif  // QWERTILLION_STATE_HPP_
