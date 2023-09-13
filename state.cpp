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
    return distance_to_beginning_of_line(*this, bef_.size());
}

void buffer::set_cursor(size_t pos) {
    if (pos < bef_.size()) {
        bef_stats_ = subtract_stats_right(bef_stats_, bef_.data(), pos, bef_.size());
        aft_stats_ = append_stats(compute_stats(bef_.data() + pos, bef_.size() - pos), aft_stats_);
        aft_.insert(aft_.begin(), bef_.begin() + pos, bef_.end());
        bef_.resize(pos);
    } else {
        size_t aft_pos = pos - bef_.size();
        logic_check(aft_pos <= aft_.size(), "set_cursor outside buf range");
        region_stats segstats = compute_stats(aft_.data(), aft_pos);
        bef_stats_ = append_stats(bef_stats_, segstats);
        aft_stats_ = subtract_stats_left(aft_stats_, segstats, aft_.data() + aft_pos, aft_.size() - aft_pos);
        bef_.append(aft_.data(), aft_pos);
        aft_.erase(0, aft_pos);
    }
}

std::string buffer::copy_to_string() const {
    std::string ret;
    ret.reserve(bef_.size() + aft_.size());
    ret.append(as_chars(bef_.data()), bef_.size());
    ret.append(as_chars(aft_.data()), aft_.size());
    return ret;
}

buffer_string buffer::copy_substr(size_t beg, size_t end) const {
    logic_check(beg <= end && end <= size(), "buffer::copy_substr requires valid range, got [%zu, %zu) with size %zu",
                beg, end, size());
    buffer_string ret;
    ret.reserve(end - beg);
    if (end <= bef_.size()) {
        ret = bef_.substr(beg, end - beg);
    } else if (beg < bef_.size()) {
        ret = bef_.substr(beg, bef_.size() - beg) + aft_.substr(0, end - bef_.size());
    } else {
        ret = aft_.substr(beg - bef_.size(), end - beg);
    }
    return ret;
}

buffer buffer::from_data(buffer_id id, buffer_string&& data) {
    buffer ret(id);
    ret.bef_stats_ = compute_stats(data);
    ret.bef_ = std::move(data);
    return ret;
}

ui_window_ctx *ui_window::point_at(buffer_id id, state *st) {
    for (size_t i = 0, e = window_ctxs.size(); i < e; ++i) {
        if (window_ctxs[i].first == id) {
            return window_ctxs[i].second.get();
        }
    }
    buffer *buf = st->lookup(id);
    window_ctxs.emplace_back(id, std::make_unique<ui_window_ctx>(buf->add_mark(0)));
    return window_ctxs.back().second.get();
}

std::string buffer_name_str(const state *state, buffer_id id) {
    // TODO: Eventually, make this not be O(n), or even, make this not allocate.
    const buffer *buf = state->lookup(id);

    bool seen = false;
    for (auto& pair : state->buf_set) {
        if (pair.first != id && pair.second->name_str == buf->name_str) {
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

buffer_string buffer_name(const state *state, buffer_id buf_number) {
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

#if 0
void resize_window(state *st, const terminal_size& new_window) {
    window_size buf_window = main_buf_window_from_terminal_window(new_window);
    for (std::unique_ptr<buffer>& buf : st->buflist) {
        resize_buf_window(&buf->win_ctx, buf_window);
    }

    // TODO: Resize prompt window.
}
#endif

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

// Note that the buf as created is not a member of _any_ window.
buffer_id find_or_create_buf(state *state, const std::string& name, bool make_read_only) {
    buffer_id ret;
    if (find_buffer_by_name(state, name, &ret)) {
        return ret;
    }

    ret = state->gen_buf_id();
    auto buf = std::make_unique<buffer>(ret);
    buf->read_only = make_read_only;
    buf->name_str = name;

    // We just insert the buf, and call apply_number_to_buf.  It's _not_ added as part of
    // any window.
    state->buf_set.emplace(ret, std::make_unique<buffer>(std::move(buf)));
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
    logic_check(id.index < marks.size(), "get_mark_offset");
    size_t mark_offset = marks[id.index];
    logic_checkg(mark_offset != SIZE_MAX);
    return mark_offset;
}

void buffer::remove_mark(mark_id id) {
    logic_check(id.index < marks.size(), "remove_mark");
    size_t mark_offset = marks[id.index];
    logic_checkg(mark_offset != SIZE_MAX);
    marks[id.index] = SIZE_MAX;
}

void buffer::replace_mark(mark_id id, size_t new_offset) {
    logic_check(id.index < marks.size(), "replace_mark");
    size_t mark_offset = marks[id.index];
    logic_checkg(mark_offset != SIZE_MAX);
    marks[id.index] = new_offset;
}

void ui_window::note_rendered_window_size(
    buffer_id buf_id, const window_size& window_size) {

    const auto *p = &active_buf();
    logic_check(p->first == buf_id, "note_rendered_window_size buffer_id mismatches the active_tab");
    ui_window_ctx *win = p->second.get();
    win->set_last_rendered_window(window_size);
}

void state::add_message(const std::string& msg) {
    if (!msg.empty()) {
        buffer_id buf_id = find_or_create_buf(this, "*Messages*", true /* read-only */);
        buffer *buf = buf_set.find(buf_id).first->second.get();
        // TODO: This edit should _not_ be tied to any window!!!  In a way that makes sense there are multiple windows.  Or it should be tied to _all_ live windows in some way...?
        ui_window_ctx *ui = win_ctx(num);

        size_t og_cursor = buf->cursor();
        force_insert_chars_end_before_cursor(
            ui, buf,
            as_buffer_chars(msg.data()), msg.size());

        char ch = '\n';
        force_insert_chars_end_before_cursor(
            ui, buf,
            as_buffer_chars(&ch), 1);

        // We ALMOST don't touch the buffer's undo history or yank history -- since we
        // append at the end of the buffer, the behavior doesn't need to adjust any undo
        // offsets -- a concept we never had before.  (And so far we don't even have the
        // concept of user turning off read-only mode and editing the buffer.)
        //
        // The exception is that char coalescence needs to be broken, because if the
        // cursor's at the end of the buffer, we move it around.
        if (og_cursor != buf->cursor()) {
            add_coalescence_break(&buf->undo_info);

            // TODO: If we are _yanking_ the current *Messages* buffer, _and_ the cursor
            // is at the end of the *Messages* buffer (which is active) we need to call
            // no_yank as well.  Not a crashing bug, but a bug nonetheless.
        }
    }
}

void state::note_error_message(std::string&& msg) {
    add_message(msg);
    live_error_message = std::move(msg);
}


}  // namespace qwi
