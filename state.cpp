#include "state.hpp"

#include "arith.hpp"
#include "buffer.hpp"  // for insert_result and delete_result in undo logic
#include "editing.hpp"
#include "error.hpp"
#include "term_ui.hpp"

namespace qwi {

void buffer::line_info_at_pos(size_t pos, size_t *line_out, size_t *col_out) const {
    region_stats stats;
    if (pos == bef_.size()) {
        stats = bef_stats_;
    } else if (pos < bef_.size()) {
        // Unsure what fraction is optimal; we're just making a Statement that
        // subtract_stats_right is more complicated.
        if (pos < (bef_.size() / 4) * 3) {
            stats = compute_stats(bef_.data(), pos);
        } else {
            stats = subtract_stats_right(bef_stats_, bef_.data(), pos, bef_.size());
        }
    } else {
        logic_check(pos <= bef_.size() + aft_.size(),
                    "line_info_at_pos: pos=%zu, bef_.size()=%zu, aft_.size()=%zu, sum=%zu",
                    pos, bef_.size(), aft_.size(), bef_.size() + aft_.size());
        size_t apos = pos - bef_.size();
        if (apos < (aft_.size() / 4) * 3) {
            stats = append_stats(bef_stats_,
                                 compute_stats(aft_.data(), apos));
        } else {
            stats = append_stats(bef_stats_,
                                 subtract_stats_right(aft_stats_, aft_.data(), apos, aft_.size()));
        }
    }

    return stats_to_line_info(stats, line_out, col_out);
}

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

// TODO: With C++ exceptions lurking, this actually does need to be in some destructor.
void detach_ui_window_ctx(buffer *buf, ui_window_ctx *ui) {
    buf->remove_mark(ui->first_visible_offset);
    ui->first_visible_offset = { SIZE_MAX, 0 };
    buf->remove_mark(ui->cursor_mark);
    ui->cursor_mark = { SIZE_MAX, 0 };
}

ui_window_ctx *ui_window::point_at(buffer_id id, state *st) {
    for (size_t i = 0, e = window_ctxs.size(); i < e; ++i) {
        if (window_ctxs[i].first == id) {
            active_tab = tab_number{i};
            return window_ctxs[i].second.get();
        }
    }
    // We maintain previous file_open_prompt behavior and insert the new buffer in the
    // list right before the open "tab", so the previous buffer is "next".
    buffer *buf = st->lookup(id);
    if (active_tab.value == SIZE_MAX) {
        active_tab.value = 0;
    }
    // TODO: XXX: point_at should take an optional param from another ui_window_ctx from which it borrows first_visible_offset and cursor.  In fact this is just broken because it sets the cursor but first_visible_offset is zero..
    window_ctxs.emplace(window_ctxs.begin() + active_tab.value,
                        buf->id, std::make_unique<ui_window_ctx>(buf->add_mark(0), buf->add_mark(buf->cursor_())));
    return window_ctxs[active_tab.value].second.get();
}



bool ui_window::detach_if_attached(buffer *buf) {
    buffer_id buf_id = buf->id;
    // We enumerate right to left, so that detach_all_bufs_from_ui_window is O(n).
    // TODO: We could instead factor out the loop body into a helper function
    for (size_t i = window_ctxs.size(); i-- > 0; ) {
        if (window_ctxs[i].first == buf_id) {
            detach_ui_window_ctx(buf, window_ctxs[i].second.get());
            window_ctxs.erase(window_ctxs.begin() + i);

            if (window_ctxs.size() == 0) {
                active_tab = {SIZE_MAX};
                return true;
            }
            if (active_tab.value > i) {
                active_tab.value -= 1;
            } else if (active_tab.value == window_ctxs.size()) {
                active_tab.value = 0;
            }
            return false;
        }
    }
    return false;
}

void detach_all_bufs_from_ui_window(state *state, ui_window *win) {
    while (!win->window_ctxs.empty()) {
        buffer *buf = state->lookup(win->window_ctxs.back().first);
        bool discard = win->detach_if_attached(buf);
        (void)discard;  // We already check win->window_ctxs.empty().
    }
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
    state->buf_set.emplace(ret, std::move(buf));
    apply_number_to_buf(state, ret);
    return ret;
}

mark_id buffer::add_mark(size_t offset) {
    uint64_t new_version = ++prev_mark_version;
    for (size_t i = 0; i < marks.size(); ++i) {
        if (marks[i].version == mark_data::unused) {
            marks[i].version = new_version;
            marks[i].offset = offset;
            return mark_id{.index = i, .assertion_version = new_version};
        }
    }
    mark_id ret = {.index = marks.size(), .assertion_version = new_version};
    marks.push_back({.version = new_version, .offset = offset});
    return ret;
}

size_t buffer::get_mark_offset(mark_id id) const {
    logic_check(id.index < marks.size(), "get_mark_offset");
    const mark_data& elem = marks[id.index];
    logic_check(elem.version != mark_data::unused, "get_mark_offset");
    logic_check(id.assertion_version == elem.version, "get_mark_offset");
    return elem.offset;
}

void buffer::remove_mark(mark_id id) {
    logic_check(id.index < marks.size(), "remove_mark");
    mark_data& elem = marks[id.index];
    logic_check(elem.version != mark_data::unused, "remove_mark");
    logic_check(id.assertion_version == elem.version, "remove_mark");
    elem.version = mark_data::unused;
    elem.offset = 0;
}

void buffer::replace_mark(mark_id id, size_t new_offset) {
    logic_check(id.index < marks.size(), "replace_mark");
    mark_data& elem = marks[id.index];
    logic_check(elem.version != mark_data::unused, "replace_mark");
    logic_check(id.assertion_version == elem.version, "replace_mark");
    elem.offset = new_offset;
}

weak_mark_id buffer::make_weak_mark_ref(mark_id id) const {
    logic_check(id.index < marks.size(), "make_weak_mark_ref");
    const mark_data& elem = marks[id.index];
    logic_checkg(elem.version != mark_data::unused);
    return {
        .version = elem.version,
        .index = id.index,
    };
}

std::optional<size_t> buffer::try_get_mark_offset(weak_mark_id id) const {
    logic_check(id.index < marks.size(), "try_get_mark_offset");
    const mark_data& elem = marks[id.index];
    if (elem.version != id.version) {
        return std::nullopt;
    }
    return std::make_optional(elem.offset);
}


void ui_window::note_rendered_window_size(
    buffer_id buf_id, const window_size& window_size) {

    const auto *p = &active_buf();
    logic_check(p->first == buf_id, "note_rendered_window_size buffer_id mismatches the active_tab");
    ui_window_ctx *win = p->second.get();
    win->set_last_rendered_window(window_size);
}

void window_layout::sanity_check() const {
    logic_checkg(!windows.empty());
    logic_checkg(row_relsizes.size() == windows.size());
    logic_checkg(!column_datas.empty());

    // For now, we comment out checks that any relsizes are zero.  Zero-size columns are
    // doable because they have a divider, but zero-size rows are not.  Probably we'll
    // make the minimum size, like, 3.

    // TODO: Force a minimum window size.
#if 0
    bool any_zero_row_relsizes = false;
    for (uint32_t sz : row_relsizes) {
        any_zero_row_relsizes |= (sz == 0);
    }
    logic_checkg(!any_zero_row_relsizes);
#endif

    size_t row_count = 0;
    bool any_zero_row_columns = false;
    // uint32_t any_zero_relsize_columns = false;
    uint32_t cols_denominator = 0;
    for (const col_data& elem : column_datas) {
        const size_t column_begin = row_count;
        row_count += elem.num_rows;
        any_zero_row_columns |= (elem.num_rows == 0);
        cols_denominator += elem.relsize;
        // any_zero_relsize_columns |= (elem.relsize == 0);

        // Since we allow zero entries, we need to check non-zero denominator.
        uint32_t rows_denominator = 0;
        for (size_t i = column_begin; i < row_count; ++i) {
            rows_denominator += row_relsizes[i];
        }
        logic_checkg(rows_denominator != 0);
    }
    logic_checkg(!any_zero_row_columns);
    logic_checkg(row_count == windows.size());
    logic_checkg(cols_denominator != 0);
    // logic_checkg(!any_zero_relsize_columns);

    logic_checkg(active_window.value < windows.size());
    logic_checkg(last_rendered_terminal_size.rows > 0);
    logic_checkg(last_rendered_terminal_size.cols > 0);
}

void state::add_message(const std::string& msg) {
    if (!msg.empty()) {
        buffer_id buf_id = find_or_create_buf(this, "*Messages*", true /* read-only */);
        buffer *buf = buf_set.find(buf_id)->second.get();

        // size_t og_cursor = buf->cursor();  // see below
        force_insert_chars_end_before_cursor(
            buf,
            as_buffer_chars(msg.data()), msg.size());

        char ch = '\n';
        force_insert_chars_end_before_cursor(
            buf,
            as_buffer_chars(&ch), 1);

        // We ALMOST don't touch the buffer's undo history or yank history -- since we
        // append at the end of the buffer, the behavior doesn't need to adjust any undo
        // offsets -- a concept we never had before.  (And so far we don't even have the
        // concept of user turning off read-only mode and editing the buffer.)
        //
        // The exception is that char coalescence needs to be broken, because if the
        // cursor's at the end of the buffer, we move it around.

        // TODO: Update this code for multi-window and add back the conditional coalescence break.
        if (true /* og_cursor != buf->cursor() */) {
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

void state::note_error(ui_result&& res) {
    logic_check(res.errored(), "note_error");
    add_message(res.message);
    live_error_message = std::move(res.message);
}

state::state() : scratch_{new scratch_frame{}} {}

state::~state() {
    if (popup_display.has_value()) {
        detach_ui_window_ctx(&popup_display->buf, &popup_display->win_ctx);
    }
    close_status_prompt(this);  // TODO: Make a prompt destructor do this?
    for (ui_window& win : layout.windows) {
        detach_all_bufs_from_ui_window(this, &win);
    }
}

}  // namespace qwi
