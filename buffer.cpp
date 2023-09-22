#include "buffer.hpp"

#include <string.h>

#include "arith.hpp"
#include "error.hpp"

// TODO: Slightly unhappy about this include dependency -- should the ui logic updates be
// applied outside these fn's?  Event first_visible_offset?  (Same in movement.cpp.)
//
// TODO: We now have exhaustive "an action happened" logic -- virtual_column
// update/current_column calls could be handled with that.  But I think we would instead
// avoid recomputing every edit, using a std::optional.
#include "term_ui.hpp"

namespace qwi {

static const std::string NO_ERROR{};

void load_ctx_cursor(ui_window_ctx *ui, buffer *buf) {
    buf->set_cursor_(get_ctx_cursor(ui, buf));
}

void save_ctx_cursor(ui_window_ctx *ui, buffer *buf) {
    buf->replace_mark(ui->cursor_mark, buf->cursor_());
}

void add_to_marks_as_of(buffer *buf, size_t first_offset, size_t count) {
    for (size_t i = 0; i < buf->marks.size(); ++i) {
        if (buf->marks[i].version != buffer::mark_data::unused && buf->marks[i].offset >= first_offset) {
            buf->marks[i].offset = size_add(buf->marks[i].offset, count);
        }
    }
}

insert_result insert_chars(scratch_frame *scratch, ui_window_ctx *ui, buffer *buf, const buffer_char *chs, size_t count) {
    // TODO: We don't want to load_ctx_cursor for read-only bufs (for performance).  In
    // other functions here as well.
    const size_t og_cursor = get_ctx_cursor(ui, buf);
    if (buf->read_only) {
        return {
            .new_cursor = og_cursor,
            .insertedText = buffer_string{},
            .side = Side::left,
            .error_message = "Buffer is read-only",  // TODO: UI logic
        };
    }

    buf->set_cursor_(og_cursor);
    buf->bef_.append(chs, count);
    const size_t new_cursor = buf->bef_.size();
    buf->bef_stats_ = append_stats(buf->bef_stats_, compute_stats(chs, count));
    add_to_marks_as_of(buf, og_cursor + 1, count);

    ui->virtual_column = std::nullopt;

    set_ctx_cursor(ui, buf);

    recenter_cursor_if_offscreen(scratch, ui, buf);

    return {
        .new_cursor = new_cursor,
        .insertedText = buffer_string(chs, count),
        .side = Side::left,
        .error_message = NO_ERROR };
}

insert_result insert_chars_right(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, const buffer_char *chs, size_t count) {
    const size_t og_cursor = get_ctx_cursor(ui, buf);
    load_ctx_cursor(ui, buf);
    if (buf->read_only) {
        return {
            .new_cursor = og_cursor,
            .insertedText = buffer_string{},
            .side = Side::right,
            .error_message = "Buffer is read-only",  // TODO: UI logic
        };
    }

    buf->set_cursor_(og_cursor);
    buf->aft_.insert(0, chs, count);
    buf->aft_stats_ = append_stats(compute_stats(chs, count), buf->aft_stats_);
    add_to_marks_as_of(buf, og_cursor + 1, count);

    ui->virtual_column = std::nullopt;

    // Actually necessary, as long as add_to_marks_as_of above pushes cursor_mark to the right.
    set_ctx_cursor(ui, buf);

    recenter_cursor_if_offscreen(scratch_frame, ui, buf);

    return {
        .new_cursor = og_cursor,
        .insertedText = buffer_string(chs, count),
        .side = Side::right,
        .error_message = NO_ERROR,
    };
}

// TODO: Currently this function name is wrong -- since buffer no longer has a meaningful
// cursor (it's per-window, part of ui_window_ctx now).
void force_insert_chars_end_before_cursor(buffer *buf,
                                          const buffer_char *chs, size_t count) {
    region_stats stats = compute_stats(chs, count);

    // TODO: We want to update the cursor for every window where the *Messages* buf is
    // active, if the cursor is at the end of the buf.  Our logic here is currently silly,
    // in the multi-window case.

    buf->aft_stats_ = append_stats(buf->aft_stats_, stats);
    buf->aft_.append(chs, count);

    // TODO: We'll want this for every window where the *Messages* buf is active, likewise.
#if 0
    ui->virtual_column = std::nullopt;
#endif

    // I guess we don't touch first_visible_offset -- later we'll want
    // scroll-to-cursor behavior with *Messages*.  But only if it's on-screen(?).

    // We _don't_ recenter cursor if offscreen.
}

void update_marks_for_delete_range(buffer *buf, size_t range_beg, size_t range_end,
                                   std::vector<std::pair<mark_id, size_t>> *squeezed_marks_append) {
    for (size_t i = 0; i < buf->marks.size(); ++i) {
        if (buf->marks[i].version == 0) {
            continue;
        }

        size_t offset = buf->marks[i].offset;
        if (offset > range_end) {
            offset -= (range_end - range_beg);
        } else if (offset > range_beg) {
            // TODO: XXX: Test or review edge cases where we delete left or right a range, and another (window's) mark is on the end or beginning of it.
            squeezed_marks_append->emplace_back(mark_id{.index = i}, offset - range_beg);
            offset = range_beg;
        }
        buf->marks[i].offset = offset;
    }
}

delete_result delete_left(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, size_t og_count) {
    const size_t og_cursor = get_ctx_cursor(ui, buf);
    if (buf->read_only) {
        return {
            .new_cursor = og_cursor,
            .deletedText = buffer_string{},
            .side = Side::left,
            .squeezed_marks = {},
            .error_message = "Buffer is read-only",  // TODO: UI logic
        };
    }

    buf->set_cursor_(og_cursor);

    const size_t count = std::min<size_t>(og_count, og_cursor);
    const size_t new_cursor = og_cursor - count;

    delete_result ret;
    ret.new_cursor = new_cursor;
    ret.deletedText.assign(buf->bef_, new_cursor, count);
    ret.side = Side::left;

    buf->bef_stats_ = subtract_stats_right(buf->bef_stats_,
                                           buf->bef_.data(), new_cursor, buf->bef_.size());
    buf->bef_.resize(new_cursor);
    update_marks_for_delete_range(buf, new_cursor, og_cursor, &ret.squeezed_marks);
    // TODO: XXX: squeezed_marks might include the current window ctx's cursor.  Should it?  Maybe it should -- if we delet-left, then we undo a delete-left in another window, we want the current window's cursor to move right.  Don't we?

    ui->virtual_column = std::nullopt;

    set_ctx_cursor(ui, buf);  // Should be a no-op, but whatever.

    recenter_cursor_if_offscreen(scratch_frame, ui, buf);

    if (count < og_count) {
        ret.error_message = "Beginning of buffer";  // TODO: Bad place for UI logic
    }
    return ret;
}

delete_result delete_right(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, size_t og_count) {
    const size_t cursor = get_ctx_cursor(ui, buf);
    if (buf->read_only) {
        return {
            .new_cursor = cursor,
            .deletedText = buffer_string{},
            .side = Side::right,
            .squeezed_marks = {},
            .error_message = "Buffer is read-only",  // TODO: UI logic
        };
    }

    buf->set_cursor_(cursor);

    const size_t count = std::min<size_t>(og_count, buf->aft_.size());

    delete_result ret;
    ret.new_cursor = cursor;
    ret.deletedText.assign(buf->aft_, 0, count);
    ret.side = Side::right;

    buf->aft_stats_ = subtract_stats_left(buf->aft_stats_, compute_stats(buf->aft_.data(), count),
                                          buf->aft_.data() + count, buf->aft_.size() - count);
    buf->aft_.erase(0, count);
    update_marks_for_delete_range(buf, cursor, cursor + count, &ret.squeezed_marks);
    // TODO: XXX: squeezed_marks might include the current window ctx's cursor.  Keep it clean.

    // TODO: We don't do this for doDeleteRight (or doAppendRight) in jsmacs -- the bug is in jsmacs!
    ui->virtual_column = std::nullopt;

    set_ctx_cursor(ui, buf);  // Definitely a no-op.

    recenter_cursor_if_offscreen(scratch_frame, ui, buf);

    if (count < og_count) {
        ret.error_message = "End of buffer";  // TODO: Bad place for UI logic
    }
    return ret;
}

void move_right_by(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, size_t count) {
    const size_t cursor = get_ctx_cursor(ui, buf);
    count = std::min<size_t>(count, buf->size() - cursor);
    // Calling set_cursor isn't necessary, but it reduces (line, col) computation cost when rendering.
    buf->set_cursor_(cursor + count);
    // TODO: Should we set virtual_column if count is 0?  (Can count be 0?)
    ui->virtual_column = std::nullopt;
    set_ctx_cursor(ui, buf);
    recenter_cursor_if_offscreen(scratch_frame, ui, buf);
}

void move_left_by(scratch_frame *scratch_frame, ui_window_ctx *ui, buffer *buf, size_t count) {
    const size_t cursor = get_ctx_cursor(ui, buf);
    count = std::min<size_t>(count, cursor);
    // Calling set_cursor isn't necessary, but it reduces (line, col) computation cost when rendering.
    buf->set_cursor_(cursor - count);
    // TODO: Should we set virtual_column if count is 0?  (Can count be 0?)
    ui->virtual_column = std::nullopt;
    set_ctx_cursor(ui, buf);
    recenter_cursor_if_offscreen(scratch_frame, ui, buf);
}

void set_mark(ui_window_ctx *ui, buffer *buf) {
    const size_t cursor = get_ctx_cursor(ui, buf);
    if (buf->mark.has_value()) {
        buf->replace_mark(*buf->mark, cursor);
    } else {
        buf->mark = buf->add_mark(cursor);
    }
}

}  // namespace qwi

