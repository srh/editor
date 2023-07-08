#ifndef QWERTILLION_STATE_HPP_
#define QWERTILLION_STATE_HPP_

#include <optional>
#include <string>
#include <vector>

namespace qwi {

struct buffer {
    // Used to choose in the list of buffers, unique to the buffer.  Program logic should
    // not allow empty or overlong values.
    std::string name;

    // We just have strings for text before/after the cursor.  Very bad perf.
    std::string bef;
    std::string aft;

    // Absolute position of the mark, if there is one.
    std::optional<size_t> mark;

    // Column that is maintained as we press up and down arrow keys past shorter lines.
    size_t virtual_column = 0;

    // 0 <= first_visible_offset <= size().
    size_t first_visible_offset = 0;

    size_t size() const { return bef.size() + aft.size(); }
};

struct state {
    // Sorted in order from least-recently-used -- 0 is the active buffer.
    // Is never empty (after initialization with *scratch* by the program).
    std::vector<buffer> bufs;
};

}  // namespace qwi

#endif  // QWERTILLION_STATE_HPP_
