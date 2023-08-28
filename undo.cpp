#include "undo.hpp"

#include "arith.hpp"
#include "buffer.hpp"

namespace qwi {

// TODO: As mentioned in the header undo.hpp, this doesn't really belong.
buffer_string to_buffer_string(const std::string& s) {
    buffer_string ret{as_buffer_chars(s.data()), s.size()};
    return ret;
}

modification_delta add(modification_delta x, modification_delta y) {
    int8_t result = x.value + y.value;
    logic_check(result >= -1 && result <= -1, "modification_delta operator+(%d, %d)", int(x.value), int(y.value));
    return modification_delta{result};
}


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

void add_edit(undo_history *history, atomic_undo_item&& item) {
    history->coalescence = undo_history::char_coalescence::none;
    move_future_to_mountain(history);
    history->past.push_back({
            .type = undo_item::Type::atomic,
            .atomic = std::move(item),
        });
}

void add_coalescent_edit(undo_history *history, atomic_undo_item&& item, undo_history::char_coalescence coalescence) {
    move_future_to_mountain(history);
    if (history->coalescence == coalescence && !history->past.empty()) {
        undo_item& back_item = history->past.back();
        if (back_item.type != undo_item::Type::mountain) {
            atomic_undo_item& back = back_item.atomic;

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
                back.mod_delta = add(back.mod_delta, item.mod_delta);
                back.beg = item.beg;
                {
                    return;
                }
            case undo_history::char_coalescence::delete_left:
                logic_check(back.side == Side::left && item.side == Side::left, "incompatible delete_left coalescence");
                logic_check(back.text_deleted.empty() && item.text_deleted.empty(), "incompatible delete_left coalescence");
                logic_check(size_add(item.beg, item.text_inserted.size()) == back.beg, "incompatible delete_left coalescence");
                back.text_inserted = std::move(item.text_inserted) + back.text_inserted;
                back.mod_delta = add(back.mod_delta, item.mod_delta);
                back.beg = item.beg;
                {
                    return;
                }
            case undo_history::char_coalescence::delete_right:
                logic_check(back.side == Side::right && item.side == Side::right, "incompatible delete_right coalescence");
                logic_check(back.text_deleted.empty() && item.text_deleted.empty(), "incompatible delete_right coalescence");
                logic_check(back.beg == item.beg, "incompatible delete_right coalescence");
                back.text_inserted += item.text_inserted;
                back.mod_delta = add(back.mod_delta, item.mod_delta);
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
}


atomic_undo_item opposite(const atomic_undo_item &item) {
    // TODO: Is .beg value right in jsmacs?
    atomic_undo_item ret = item;
    if (item.side == Side::left) {
        ret.beg = size_sub(size_add(ret.beg, item.text_inserted.size()), item.text_deleted.size());
    }
    std::swap(ret.text_inserted, ret.text_deleted);
    ret.mod_delta.value = -ret.mod_delta.value;
    return ret;
}

void atomic_undo(buffer *buf, atomic_undo_item&& item) {
    const bool og_modified_flag = buf->modified_flag;
    buf->set_cursor(item.beg);

    if (!item.text_deleted.empty()) {
        delete_result res;
        switch (item.side) {
        case Side::left:
            res = delete_left(buf, item.text_deleted.size());
            break;
        case Side::right:
            res = delete_right(buf, item.text_deleted.size());
            break;
        }
        logic_check(res.deletedText == item.text_deleted, "undo deletion action expecting text to match deleted text");
    }

    if (!item.text_inserted.empty()) {
        insert_result res;
        switch (item.side) {
        case Side::left:
            res = insert_chars(buf, item.text_inserted.data(), item.text_inserted.size());
            break;
        case Side::right:
            res = insert_chars_right(buf, item.text_inserted.data(), item.text_inserted.size());
            break;
        }
    }

    // TODO: It's kind of gross that we call insert/delete functions that touch the modified flag, then overwrite that action here.
    if (item.mod_delta.value == 0) {
        buf->modified_flag = og_modified_flag;
    } else if (item.mod_delta.value == 1) {
        logic_check(!og_modified_flag, "atomic_undo: mod_delta 1, buf modification flag is turned on");
        buf->modified_flag = true;
    } else if (item.mod_delta.value == -1) {
        logic_check(og_modified_flag, "atomic_undo: mod_delta -1, buf modification flag is turned off");
        buf->modified_flag = false;
    } else {
        logic_fail("atomic_undo: mod_delta invalid: %d", int(item.mod_delta.value));
    }

    // TODO: opposite with std::move.
    buf->undo_info.future.push_back(opposite(item));
}

void perform_undo(buffer *buf) {
    if (buf->undo_info.past.empty()) {
        return;
    }
    undo_item item = std::move(buf->undo_info.past.back());
    buf->undo_info.past.pop_back();
    switch (item.type) {
    case undo_item::Type::atomic: {
        atomic_undo(buf, std::move(item.atomic));
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
        atomic_undo(buf, std::move(it));
    } break;
    }
}


}  // namespace qwi
