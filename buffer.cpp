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
    buf->set_cursor(buf->get_mark_offset(ui->cursor_mark));
}

void save_ctx_cursor(ui_window_ctx *ui, buffer *buf) {
    buf->replace_mark(ui->cursor_mark, buf->cursor());
}

void add_to_marks_as_of(buffer *buf, size_t first_offset, size_t count) {
    for (size_t i = 0; i < buf->marks.size(); ++i) {
        if (buf->marks[i] != SIZE_MAX && buf->marks[i] >= first_offset) {
            buf->marks[i] = size_add(buf->marks[i], count);
        }
    }
}

insert_result insert_chars(ui_window_ctx *ui, buffer *buf, const buffer_char *chs, size_t count) {
    load_ctx_cursor(ui, buf);
    if (buf->read_only) {
        return {
            .new_cursor = buf->cursor(),
            .insertedText = buffer_string{},
            .side = Side::left,
            .error_message = "Buffer is read-only",  // TODO: UI logic
        };
    }

    const size_t og_cursor = buf->cursor();
    buf->bef_.append(chs, count);
    buf->bef_stats_ = append_stats(buf->bef_stats_, compute_stats(chs, count));
    add_to_marks_as_of(buf, og_cursor + 1, count);

    ui->virtual_column = std::nullopt;

    recenter_cursor_if_offscreen(ui, buf);

    save_ctx_cursor(ui, buf);
    return {
        .new_cursor = buf->cursor(),
        .insertedText = buffer_string(chs, count),
        .side = Side::left,
        .error_message = NO_ERROR };
}

insert_result insert_chars_right(ui_window_ctx *ui, buffer *buf, const buffer_char *chs, size_t count) {
    load_ctx_cursor(ui, buf);
    if (buf->read_only) {
        return {
            .new_cursor = buf->cursor(),
            .insertedText = buffer_string{},
            .side = Side::right,
            .error_message = "Buffer is read-only",  // TODO: UI logic
        };
    }

    const size_t og_cursor = buf->cursor();
    buf->aft_.insert(0, chs, count);
    buf->aft_stats_ = append_stats(compute_stats(chs, count), buf->aft_stats_);
    add_to_marks_as_of(buf, og_cursor, count);

    ui->virtual_column = std::nullopt;
    recenter_cursor_if_offscreen(ui, buf);

    save_ctx_cursor(ui, buf);
    return {
        .new_cursor = buf->cursor(),
        .insertedText = buffer_string(chs, count),
        .side = Side::right,
        .error_message = NO_ERROR,
    };
}

void force_insert_chars_end_before_cursor(buffer *buf,
                                          const buffer_char *chs, size_t count) {
    const size_t og_cursor = buf->cursor();
    const size_t og_size = buf->size();

    region_stats stats = compute_stats(chs, count);

    // If cursor is at end of buffer, we insert before the cursor.
    // TODO: XXX(?): We must update the cursor_marks.  Sadly.
    if (og_cursor != og_size) {
        buf->aft_stats_ = append_stats(buf->aft_stats_, stats);
        buf->aft_.append(chs, count);
    } else {
        buf->bef_stats_ = append_stats(buf->bef_stats_, stats);
        buf->bef_.append(chs, count);

        // TODO: We want this for every window where the *Messages* buf is active.
        // Windows' virtual column values could come with an incrementing edit number (not
        // quite an undo node number).
#if 0
        ui->virtual_column = std::nullopt;
#endif

        // I guess we don't touch first_visible_offset -- later we'll want
        // scroll-to-cursor behavior with *Messages*.

        // We _don't_ recenter cursor if offscreen.
    }
}

void update_marks_for_delete_range(buffer *buf, size_t range_beg, size_t range_end) {
    for (size_t i = 0; i < buf->marks.size(); ++i) {
        if (buf->marks[i] == SIZE_MAX) {
            continue;
        }

        size_t offset = buf->marks[i];
        if (offset > range_end) {
            offset -= (range_end - range_beg);
        } else if (offset > range_beg) {
            offset = range_beg;
        }
        buf->marks[i] = offset;
    }
}

delete_result delete_left(ui_window_ctx *ui, buffer *buf, size_t og_count) {
    load_ctx_cursor(ui, buf);
    if (buf->read_only) {
        return {
            .new_cursor = buf->cursor(),
            .deletedText = buffer_string{},
            .side = Side::left,
            .error_message = "Buffer is read-only",  // TODO: UI logic
        };
    }

    const size_t count = std::min<size_t>(og_count, buf->bef_.size());
    size_t og_cursor = buf->bef_.size();
    size_t new_cursor = og_cursor - count;

    delete_result ret;
    ret.new_cursor = new_cursor;
    ret.deletedText.assign(buf->bef_, new_cursor, count);
    ret.side = Side::left;

    buf->bef_stats_ = subtract_stats_right(buf->bef_stats_,
                                           buf->bef_.data(), new_cursor, buf->bef_.size());
    buf->bef_.resize(new_cursor);
    update_marks_for_delete_range(buf, new_cursor, og_cursor);

    ui->virtual_column = std::nullopt;
    recenter_cursor_if_offscreen(ui, buf);

    save_ctx_cursor(ui, buf);
    if (count < og_count) {
        ret.error_message = "Beginning of buffer";  // TODO: Bad place for UI logic
    }
    return ret;
}

delete_result delete_right(ui_window_ctx *ui, buffer *buf, size_t og_count) {
    load_ctx_cursor(ui, buf);
    if (buf->read_only) {
        return {
            .new_cursor = buf->cursor(),
            .deletedText = buffer_string{},
            .side = Side::right,
            .error_message = "Buffer is read-only",  // TODO: UI logic
        };
    }

    size_t cursor = buf->cursor();
    const size_t count = std::min<size_t>(og_count, buf->aft_.size());

    delete_result ret;
    ret.new_cursor = cursor;
    ret.deletedText.assign(buf->aft_, 0, count);
    ret.side = Side::right;

    buf->aft_stats_ = subtract_stats_left(buf->aft_stats_, compute_stats(buf->aft_.data(), count),
                                          buf->aft_.data() + count, buf->aft_.size() - count);
    buf->aft_.erase(0, count);
    update_marks_for_delete_range(buf, cursor, cursor + count);

    // TODO: We don't do this for doDeleteRight (or doAppendRight) in jsmacs -- the bug is in jsmacs!
    ui->virtual_column = std::nullopt;
    recenter_cursor_if_offscreen(ui, buf);


    save_ctx_cursor(ui, buf);
    if (count < og_count) {
        ret.error_message = "End of buffer";  // TODO: Bad place for UI logic
    }
    return ret;
}

void move_right_by(ui_window_ctx *ui, buffer *buf, size_t count) {
    load_ctx_cursor(ui, buf);
    count = std::min<size_t>(count, buf->size() - buf->cursor());
    buf->set_cursor(buf->cursor() + count);
    // TODO: Should we set virtual_column if count is 0?  (Can count be 0?)
    ui->virtual_column = std::nullopt;
    recenter_cursor_if_offscreen(ui, buf);
    save_ctx_cursor(ui, buf);
}

void move_left_by(ui_window_ctx *ui, buffer *buf, size_t count) {
    load_ctx_cursor(ui, buf);

    count = std::min<size_t>(count, buf->cursor());
    buf->set_cursor(buf->cursor() - count);
    // TODO: Should we set virtual_column if count is 0?  (Can count be 0?)
    ui->virtual_column = std::nullopt;
    recenter_cursor_if_offscreen(ui, buf);

    save_ctx_cursor(ui, buf);
}

void set_mark(ui_window_ctx *ui, buffer *buf) {
    load_ctx_cursor(ui, buf);
    if (buf->mark.has_value()) {
        buf->replace_mark(*buf->mark, buf->cursor());
    } else {
        buf->mark = buf->add_mark(buf->cursor());
    }
    save_ctx_cursor(ui, buf);
}

}  // namespace qwi

