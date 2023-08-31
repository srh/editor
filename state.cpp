#include "state.hpp"

#include "arith.hpp"
#include "buffer.hpp"  // for insert_result and delete_result in undo logic
#include "editing.hpp"
#include "error.hpp"
// TODO: We don't want this dependency exactly -- we kind of want ui info to be separate from state.
#include "terminal.hpp"
#include "term_ui.hpp"

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

buffer_string buffer::copy_substr(size_t beg, size_t end) const {
    logic_check(beg <= end && end <= size(), "buffer::copy_substr requires valid range, got [%zu, %zu) with size %zu",
                beg, end, size());
    buffer_string ret;
    ret.reserve(end - beg);
    if (end <= bef.size()) {
        ret = bef.substr(beg, end - beg);
    } else if (beg < bef.size()) {
        ret = bef.substr(beg, bef.size() - beg) + aft.substr(0, end - bef.size());
    } else {
        ret = aft.substr(beg - bef.size(), end - beg);
    }
    return ret;
}

buffer buffer::from_data(buffer_id id, buffer_string&& data) {
    buffer ret(id);
    ret.bef = std::move(data);
    return ret;
}

std::string buffer_name_str(const state *state, buffer_number buf_number) {
    // TODO: Eventually, make this not be O(n), or even, make this not allocate.
    const buffer *buf = buffer_ptr(state, buf_number);

    bool seen = false;
    for (size_t i = 0, e = state->buflist.size(); i < e; ++i) {
        if (i != buf_number.value && state->buflist[i].name_str == buf->name_str) {
            seen = true;
            break;
        }
    }

    if (seen) {
        return buf->name_str + "<" + std::to_string(buf->name_number) + ">";
    } else {
        return buf->name_str;
    }
}

buffer_string buffer_name(const state *state, buffer_number buf_number) {
    return to_buffer_string(buffer_name_str(state, buf_number));
}

size_t distance_to_eol(const buffer& buf, size_t pos) {
    size_t p = pos;
    for (size_t e = buf.size(); p < e; ++p) {
        if (buf.get(p) == buffer_char{'\n'}) {
            break;
        }
    }
    return p - pos;
}

size_t distance_to_beginning_of_line(const buffer& buf, size_t pos) {
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
    for (buffer& buf : st->buflist) {
        resize_buf_window(&buf.win_ctx, buf_window);
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
        case yank_side::none:
            clb->clips.push_back(deletedText);
            break;
        }
    } else {
        clb->clips.push_back(deletedText);
    }
    switch (side) {
    case yank_side::left:
        clb->justRecorded = true;
        break;
    case yank_side::right:
        clb->justRecorded = true;
        break;
    case yank_side::none:
        clb->justRecorded = false;
        break;
    }
    clb->pasteNumber = 0;
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

buffer_number find_or_create_buf(state *state, const std::string& name, int term, bool make_read_only) {
    buffer_number ret;
    if (find_buffer_by_name(state, name, &ret)) {
        return ret;
    }

    buffer buf(state->gen_buf_id());
    buf.read_only = make_read_only;
    buf.name_str = name;
    terminal_size window = get_terminal_size(term);
    window_size buf_window = main_buf_window_from_terminal_window(window);
    buf.win_ctx.set_window(buf_window);

    // We insert the buf just before the current buf -- thus we increment buf_ptr.
    logic_checkg(state->buf_ptr.value < state->buflist.size());
    ret = state->buf_ptr;
    state->buflist.insert(state->buflist.begin() + state->buf_ptr.value, std::move(buf));
    state->buf_ptr.value += 1;
    apply_number_to_buf(state, ret);
    return ret;
}

mark_id buffer::add_mark(size_t offset) {
    // TODO: Doesn't scale, and with region-based undo, it will need to scale.
    for (size_t i = 0; i < marks.size(); ++i) {
        if (marks[i] == SIZE_MAX) {
            marks[i] = offset;
            return mark_id{i};
        }
    }
    mark_id ret{marks.size()};
    marks.push_back(offset);
    return ret;
}

size_t buffer::get_mark_offset(mark_id id) const {
    logic_checkg(id.index < marks.size());
    size_t mark_offset = marks[id.index];
    logic_checkg(mark_offset != SIZE_MAX);
    return mark_offset;
}

void buffer::remove_mark(mark_id id) {
    logic_checkg(id.index < marks.size());
    size_t mark_offset = marks[id.index];
    logic_checkg(mark_offset != SIZE_MAX);
    marks[id.index] = SIZE_MAX;
}

void buffer::replace_mark(mark_id id, size_t new_offset) {
    logic_checkg(id.index < marks.size());
    size_t mark_offset = marks[id.index];
    logic_checkg(mark_offset != SIZE_MAX);
    marks[id.index] = new_offset;
}



void state::note_error_message(std::string&& msg) {
    if (!msg.empty()) {
        buffer_number num = find_or_create_buf(this, "*Messages*", term, true /* read-only */);
        buffer *buf = buffer_ptr(this, num);

        force_insert_chars_end_before_cursor(
            buf,
            as_buffer_chars(msg.data()), msg.size());

        char ch = '\n';
        force_insert_chars_end_before_cursor(
            buf,
            as_buffer_chars(&ch), 1);
        // We don't touch the buffer's undo history or yank history -- since we append at
        // the end of the buffer, the behavior doesn't need to adjust any undo offsets --
        // a concept we never had before.  (And so far we don't even have the concept of user
        // turning off read-only mode and editing the buffer.)
    }
    live_error_message = std::move(msg);
}


}  // namespace qwi
