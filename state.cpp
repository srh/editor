#include "state.hpp"

#include "arith.hpp"
#include "buffer.hpp"  // for insert_result and delete_result in undo logic
#include "error.hpp"
// TODO: We don't want this dependency exactly -- we kind of want ui info to be separate from state.
#include "terminal.hpp"
#include "term_ui.hpp"

// TODO: Decl somewhere, move from main.cpp.
qwi::undo_item make_reverse_action(insert_result&& i_res);
qwi::undo_item make_reverse_action(delete_result&& i_res);

namespace qwi {

size_t buffer::cursor_distance_to_beginning_of_line() const {
    size_t ix = bef.find_last_of(buffer_char{'\n'});
    // this works in the std::string::npos case too
    return bef.size() - ix - 1;
}

void buffer::set_cursor(size_t pos) {
    if (pos < bef.size()) {
        aft.insert(aft.begin(), bef.begin() + pos, bef.end());
        bef.resize(pos);
    } else {
        size_t aft_pos = pos - bef.size();
        logic_check(aft_pos <= aft.size(), "set_cursor outside buf range");
        bef.append(aft.data(), aft_pos);
        aft.erase(0, aft_pos);
    }
}

std::string buffer::copy_to_string() const {
    std::string ret;
    ret.reserve(bef.size() + aft.size());
    ret.append(as_chars(bef.data()), bef.size());
    ret.append(as_chars(aft.data()), aft.size());
    return ret;
}

size_t distance_to_eol(const qwi::buffer& buf, size_t pos) {
    size_t p = pos;
    for (size_t e = buf.size(); p < e; ++p) {
        if (buf.get(p) == buffer_char{'\n'}) {
            break;
        }
    }
    return p - pos;
}

size_t distance_to_beginning_of_line(const qwi::buffer& buf, size_t pos) {
    logic_check(pos <= buf.size(), "distance_to_beginning_of_line with out of range pos");
    size_t p = pos;
    for (;;) {
        if (p == 0) {
            return pos;
        }
        --p;
        if (buf.get(p) == buffer_char{'\n'}) {
            return pos - (p + 1);
        }
    }
}

window_size main_buf_window_from_terminal_window(const terminal_size& term_window) {
    return {
        .rows = std::max(STATUS_AREA_HEIGHT, term_window.rows) - STATUS_AREA_HEIGHT,
        .cols = term_window.cols,
    };
}

void resize_window(state *st, const terminal_size& new_window) {
    window_size buf_window = main_buf_window_from_terminal_window(new_window);
    resize_buf_window(&st->buf, buf_window);
    for (qwi::buffer& buf : st->bufs) {
        resize_buf_window(&buf, buf_window);
    }

    // TODO: Resize prompt window.
}

void record_yank(clip_board *clb, const buffer_string& deletedText, yank_side side) {
    if (clb->justRecorded) {
        runtime_check(!clb->clips.empty(), "justRecorded true, clips empty");
        switch (side) {
        case yank_side::left:
            clb->clips.back() = deletedText + clb->clips.back();
            break;
        case yank_side::right:
            clb->clips.back() += deletedText;
            break;
        }
    } else {
        clb->clips.push_back(deletedText);
    }
    clb->pasteNumber = 0;
    clb->justRecorded = true;
    clb->justYanked = std::nullopt;
}

std::optional<const buffer_string *> do_yank(clip_board *clb) {
    clb->justRecorded = false;
    if (clb->clips.empty()) {
        clb->justYanked = 0;
        return std::nullopt;
    }
    size_t sz = clb->clips.size();
    buffer_string *str = &clb->clips.at(sz - 1 - clb->pasteNumber % sz);
    clb->justYanked = str->size();
    return std::make_optional(str);
}

void no_yank(clip_board *clb) {
    clb->justRecorded = false;
    clb->justYanked = std::nullopt;
}

// Starts a new branch to undo history, but without any edits yet.
void add_nop_edit(undo_history *history) {
    if (!history->future.empty()) {
        history->past.push_back({
                .type = undo_item::Type::mountain,
                .atomic = {},
                .history = std::move(history->future)
            });
        history->future.clear();
    }
}

void add_edit(undo_history *history, undo_item&& item) {
    add_nop_edit(history);
    history->past.push_back(std::move(item));
}

atomic_undo_item opposite(const atomic_undo_item &item) {
    // TODO: Is .beg value right in jsmacs?
    atomic_undo_item ret = item;
    if (item.side == Side::left) {
        ret.beg = size_sub(size_add(ret.beg, item.text_inserted.size()), item.text_deleted.size());
    }
    std::swap(ret.text_inserted, ret.text_deleted);
    return ret;
}

void atomic_undo(buffer *buf, atomic_undo_item&& item) {
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
