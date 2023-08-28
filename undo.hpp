#ifndef QWERTILLION_UNDO_HPP_
#define QWERTILLION_UNDO_HPP_

#include <stdint.h>

#include <string>
#include <vector>

namespace qwi {

// TODO: buffer_string and buffer_char don't belong in this header.
struct buffer_char {
    uint8_t value;

    static buffer_char from_char(char ch) { return buffer_char{uint8_t(ch)}; }
    friend auto operator<=>(buffer_char, buffer_char) = default;
};

using buffer_string = std::basic_string<buffer_char>;

buffer_string to_buffer_string(const std::string& s);

// TODO: "Side" isn't quite undo-specific, but it's in this header.
enum class Side { left, right, };

struct modification_delta {
    // Always -1, 0, or 1.
    int8_t value = 0;
};

modification_delta add(modification_delta x, modification_delta y);

struct atomic_undo_item {
    // The cursor _before_ we apply this undo action.  This departs from jsmacs, where
    // it's the cursor after the action, or something incoherent and broken.
    size_t beg = 0;
    buffer_string text_inserted{};
    buffer_string text_deleted{};
    modification_delta mod_delta;
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

void add_coalescence_break(undo_history *history);
void add_nop_edit(undo_history *history);
void add_edit(undo_history *history, atomic_undo_item&& item);
void add_coalescent_edit(undo_history *history, atomic_undo_item&& item, undo_history::char_coalescence coalescence);

struct buffer;
void perform_undo(buffer *buf);

}  // namespace qwi


#endif  // QWERTILLION_UNDO_HPP_
