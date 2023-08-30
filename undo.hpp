#ifndef QWERTILLION_UNDO_HPP_
#define QWERTILLION_UNDO_HPP_

#include <vector>

#include "chars.hpp"

namespace qwi {

struct undo_node_number {
    uint64_t value = 0;
    bool operator==(const undo_node_number&) const = default;
};

struct atomic_undo_item {
    // The cursor _before_ we apply this undo action.  This departs from jsmacs, where
    // it's the cursor after the action, or something incoherent and broken.
    size_t beg = 0;
    buffer_string text_inserted{};
    buffer_string text_deleted{};
    Side side = Side::left;

    undo_node_number before_node;
    undo_node_number after_node;
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

    undo_node_number current_node{1};

    undo_node_number next_node_number{2};
    undo_node_number unused_node_number() const { return next_node_number; }

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
struct state;
// TODO: Make error reporting object be a separate type, member object of state.
void perform_undo(state *st, buffer *buf);

}  // namespace qwi


#endif  // QWERTILLION_UNDO_HPP_
