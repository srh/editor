#include "undo.hpp"

#include "arith.hpp"
#include "buffer.hpp"

namespace qwi {

void move_future_to_mountain(undo_history *history) {
    if (!history->future.empty()) {
        history->past.push_back({
                .type = undo_item::Type::mountain,
                .atomic = {},
                .history = std::move(history->future)
            });
        history->future.clear();
    }
}

// Starts a new branch to undo history, but without any edits yet.
void add_nop_edit(undo_history *history) {
    history->coalescence = undo_history::char_coalescence::none;
    move_future_to_mountain(history);
}

// add_nop_edit, but without forking history -- just cuts a hole in coalescence.
void add_coalescence_break(undo_history *history) {
    history->coalescence = undo_history::char_coalescence::none;
}

// TODO: Maybe we should catalog exactly which zero-effect edits (deleting at end of
// buffer, backspace at beginning, etc.) don't have undo actions, and which (M-y reyanking
// the same text) do.  Then adding the no-op edit is computed based on intent rather than
// the changeset.
//
// This might change behavior if an empty string is yanked twice in a row, if C-y and M-y
// in sequence both insert empty strings, and then we undo.
bool item_has_effect(const atomic_undo_item& item) {
    // We consider non-empty but equal text_inserted and text_deleted strings to have an
    // "effect".
    return !(item.text_inserted.empty() && item.text_deleted.empty());
}

void add_edit(undo_history *history, atomic_undo_item&& item) {
    history->coalescence = undo_history::char_coalescence::none;
    move_future_to_mountain(history);

    if (item_has_effect(item)) {
        history->past.push_back({
                .type = undo_item::Type::atomic,
                .atomic = std::move(item),
            });
        history->current_node = item.before_node;
        history->next_node_number.value += 1;
    }
}

void add_coalescent_edit(undo_history *history, atomic_undo_item&& item, undo_history::char_coalescence coalescence) {
    move_future_to_mountain(history);
    if (history->coalescence == coalescence && !history->past.empty()) {
        undo_item& back_item = history->past.back();
        if (back_item.type != undo_item::Type::mountain) {
            atomic_undo_item& back = back_item.atomic;
            // Kind of unnecessary (we'd catch it when we try to undo).
            logic_check(back.before_node == history->current_node, "add_coalescent_edit observing mismatching before_node");

            /* This code is annoying because undo history contains the _reverse_ actions
               but char_coalescence refers to the user actions that are getting coalesced. */
            switch (coalescence) {
            case undo_history::char_coalescence::none:
                break;
            case undo_history::char_coalescence::insert_char:
                logic_check(back.side == Side::left && item.side == Side::left, "incompatible insert_char coalescence");
                logic_check(back.text_inserted.empty() && item.text_inserted.empty(), "incompatible insert_char coalescence");
                logic_check(back.beg == size_sub(item.beg, item.text_deleted.size()), "incompatible insert_char coalescence");
                back.text_deleted += item.text_deleted;
                back.beg = item.beg;
                {
                    return;
                }
            case undo_history::char_coalescence::delete_left:
                logic_check(back.side == Side::left && item.side == Side::left, "incompatible delete_left coalescence");
                logic_check(back.text_deleted.empty() && item.text_deleted.empty(), "incompatible delete_left coalescence");
                logic_check(size_add(item.beg, item.text_inserted.size()) == back.beg, "incompatible delete_left coalescence");
                back.text_inserted = std::move(item.text_inserted) + back.text_inserted;
                back.beg = item.beg;
                {
                    return;
                }
            case undo_history::char_coalescence::delete_right:
                logic_check(back.side == Side::right && item.side == Side::right, "incompatible delete_right coalescence");
                logic_check(back.text_deleted.empty() && item.text_deleted.empty(), "incompatible delete_right coalescence");
                logic_check(back.beg == item.beg, "incompatible delete_right coalescence");
                back.text_inserted += item.text_inserted;
                {
                    return;
                }
            }
        }
    }
    history->coalescence = coalescence;
    history->past.push_back({
            .type = undo_item::Type::atomic,
            .atomic = std::move(item),
        });
    history->current_node = item.before_node;
    history->next_node_number.value += 1;
}


atomic_undo_item opposite(const atomic_undo_item &item) {
    // TODO: Is .beg value right in jsmacs?
    atomic_undo_item ret = item;
    if (item.side == Side::left) {
        ret.beg = size_sub(size_add(ret.beg, item.text_inserted.size()), item.text_deleted.size());
    }
    std::swap(ret.text_inserted, ret.text_deleted);
    std::swap(ret.before_node, ret.after_node);
    return ret;
}

void atomic_undo(ui_window_ctx *ui, buffer *buf, atomic_undo_item&& item) {
    logic_check(item.before_node == buf->undo_info.current_node, "atomic_undo node number mismatch, item.before_node=%" PRIu64 " vs %" PRIu64,
                item.before_node.value, buf->undo_info.current_node.value);

    buf->set_cursor(item.beg);

    if (!item.text_deleted.empty()) {
        delete_result res;
        switch (item.side) {
        case Side::left:
            res = delete_left(ui, buf, item.text_deleted.size());
            break;
        case Side::right:
            res = delete_right(ui, buf, item.text_deleted.size());
            break;
        }
        logic_check(res.deletedText == item.text_deleted, "undo deletion action expecting text to match deleted text");
    }

    if (!item.text_inserted.empty()) {
        insert_result res;
        switch (item.side) {
        case Side::left:
            res = insert_chars(ui, buf, item.text_inserted.data(), item.text_inserted.size());
            break;
        case Side::right:
            res = insert_chars_right(ui, buf, item.text_inserted.data(), item.text_inserted.size());
            break;
        }
    }

    buf->undo_info.current_node = item.after_node;

    // TODO: opposite with std::move.
    buf->undo_info.future.push_back(opposite(item));
}

void perform_undo(state *st, ui_window_ctx *ui, buffer *buf) {
    if (buf->undo_info.past.empty()) {
        st->note_error_message("No further undo information");  // TODO: UI logic
        return;
    }
    undo_item item = std::move(buf->undo_info.past.back());
    buf->undo_info.past.pop_back();
    switch (item.type) {
    case undo_item::Type::atomic: {
        atomic_undo(ui, buf, std::move(item.atomic));
    } break;
    case undo_item::Type::mountain: {
        atomic_undo_item it = std::move(item.history.back());
        item.history.pop_back();
        buf->undo_info.past.push_back({
                .type = undo_item::Type::atomic,
                .atomic = opposite(it)
            });
        if (!item.history.empty()) {
            buf->undo_info.past.push_back(std::move(item));
        }
        atomic_undo(ui, buf, std::move(it));
    } break;
    }
}


}  // namespace qwi
