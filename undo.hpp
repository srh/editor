#ifndef QWERTILLION_UNDO_HPP_
#define QWERTILLION_UNDO_HPP_

#include <vector>

#include "chars.hpp"
#include "state_types.hpp"

namespace qwi {

struct undo_node_number {
    uint64_t value = 0;
    bool operator==(const undo_node_number&) const = default;
};

struct atomic_undo_item {
    // The cursor _before_ we apply this undo action.  This departs from jsmacs, where
    // it's the cursor after the action, or something incoherent and broken.
    size_t beg = 0;

    // We apply an undo item by moving cursor to `beg`, then deleting the text in
    // `text_deleted` on the side of the cursor given by `side`.  (We can assert that the
    // text is identical.)  Then we insert the text in `text_inserted` on the side of the
    // cursor given by `side`.  (You might say we "replace" the text.)  (We also use
    // mark_adjustments to update other windows' mid-range marks upon insertion, if
    // applicable, and update the undo node number of the buffer (which is used for the
    // file modification flag and other lawful purposes).)
    buffer_string text_deleted{};
    buffer_string text_inserted{};
    Side side = Side::left;

    // TODO: At some point, when we have limited undo info, we'll also want to GC expired weak mark refs.

    /* If we have text_inserted non-empty, then we may need to adjust marks, if their
       offset is exactly at the insertion point.

       1) If side == Side::right, the .second values are in range (0,
       text_inserted.size()].  And when applying the undo action, we _add_ them to their
       respective marks' whose offsets are still at the insertion point.

       2) If side == Side::left, the .second values are in range (0,
       text_inserted.size()], BUT they are relative to the end (i.e positive values you
       subtract from the end) -- a mark whose offset is at the insertion point gets added
       to it text_inserted.size() MINUS the .second value.

       The function insert_chars has the default behavior that marks on the insertion
       point are _not_ adjusted to the right [1], and because we use that when applying
       the undo operation, that is the reason we use [0, N) for left insertions instead of
       (0, N] as for right insertions.

       [1] That's the same behavior as insert_chars_right.

       The reason for this is that it makes coalescent edit logic cleaner, and it also
       makes coalescent edit logic not have to keep readjusting _every_ mark adjustment,
       making it O(1) instead of O(n) for each keypress, so to speak.
    */
    std::vector<std::pair<weak_mark_id, size_t>> mark_adjustments;

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
struct ui_window_ctx;
// TODO: Make error reporting object be a separate type, member object of state.
void perform_undo(state *st, ui_window_ctx *ui, buffer *buf);

}  // namespace qwi


#endif  // QWERTILLION_UNDO_HPP_
