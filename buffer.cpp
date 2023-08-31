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

void add_to_marks_as_of(buffer *buf, size_t first_offset, size_t count) {
    for (size_t i = 0; i < buf->marks.size(); ++i) {
        if (buf->marks[i] != SIZE_MAX && buf->marks[i] >= first_offset) {
            buf->marks[i] = size_add(buf->marks[i], count);
        }
    }
}

insert_result insert_chars(buffer *buf, const buffer_char *chs, size_t count) {
    if (buf->read_only) {
        return {
            .new_cursor = buf->cursor(),
            .insertedText = buffer_string{},
            .side = Side::left,
            .error_message = "Buffer is read-only",  // TODO: UI logic
        };
    }

    const size_t og_cursor = buf->cursor();
    buf->bef.append(chs, count);
    add_to_marks_as_of(buf, og_cursor + 1, count);

    buf->win_ctx.virtual_column = std::nullopt;
    recenter_cursor_if_offscreen(&buf->win_ctx, buf);
    return {
        .new_cursor = buf->cursor(),
        .insertedText = buffer_string(chs, count),
        .side = Side::left,
        .error_message = NO_ERROR };
}

insert_result insert_chars_right(buffer *buf, const buffer_char *chs, size_t count) {
    if (buf->read_only) {
        return {
            .new_cursor = buf->cursor(),
            .insertedText = buffer_string{},
            .side = Side::right,
            .error_message = "Buffer is read-only",  // TODO: UI logic
        };
    }

    const size_t og_cursor = buf->cursor();
    buf->aft.insert(0, chs, count);
    add_to_marks_as_of(buf, og_cursor, count);
    buf->win_ctx.virtual_column = std::nullopt;
    recenter_cursor_if_offscreen(&buf->win_ctx, buf);
    return {
        .new_cursor = buf->cursor(),
        .insertedText = buffer_string(chs, count),
        .side = Side::right,
        .error_message = NO_ERROR,
    };
}

// TODO: This non-generic function basically sucks.
void force_insert_chars_end_before_cursor(buffer *buf,
                                          const buffer_char *chs, size_t count) {
    const size_t og_cursor = buf->cursor();
    const size_t og_size = buf->size();

    // If cursor is at end of buffer, we insert before the cursor.
    if (og_cursor != og_size) {
        buf->aft.append(chs, count);
    } else {
        buf->bef.append(chs, count);
        buf->win_ctx.virtual_column = std::nullopt;
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

delete_result delete_left(buffer *buf, size_t og_count) {
    if (buf->read_only) {
        return {
            .new_cursor = buf->cursor(),
            .deletedText = buffer_string{},
            .side = Side::left,
            .error_message = "Buffer is read-only",  // TODO: UI logic
        };
    }

    const size_t count = std::min<size_t>(og_count, buf->bef.size());
    size_t og_cursor = buf->bef.size();
    size_t new_cursor = og_cursor - count;

    delete_result ret;
    ret.new_cursor = new_cursor;
    ret.deletedText.assign(buf->bef, new_cursor, count);
    ret.side = Side::left;

    buf->bef.resize(new_cursor);
    update_marks_for_delete_range(buf, new_cursor, og_cursor);

    buf->win_ctx.virtual_column = std::nullopt;
    recenter_cursor_if_offscreen(&buf->win_ctx, buf);
    if (count < og_count) {
        ret.error_message = "Beginning of buffer";  // TODO: Bad place for UI logic
    }
    return ret;
}

delete_result delete_right(buffer *buf, size_t og_count) {
    if (buf->read_only) {
        return {
            .new_cursor = buf->cursor(),
            .deletedText = buffer_string{},
            .side = Side::right,
            .error_message = "Buffer is read-only",  // TODO: UI logic
        };
    }

    size_t cursor = buf->cursor();
    const size_t count = std::min<size_t>(og_count, buf->aft.size());

    delete_result ret;
    ret.new_cursor = cursor;
    ret.deletedText.assign(buf->aft, 0, count);
    ret.side = Side::right;

    buf->aft.erase(0, count);
    update_marks_for_delete_range(buf, cursor, cursor + count);

    // TODO: We don't do this for doDeleteRight (or doAppendRight) in jsmacs -- the bug is in jsmacs!
    buf->win_ctx.virtual_column = std::nullopt;
    recenter_cursor_if_offscreen(&buf->win_ctx, buf);
    if (count < og_count) {
        ret.error_message = "End of buffer";  // TODO: Bad place for UI logic
    }
    return ret;
}

void move_right_by(buffer *buf, size_t count) {
    count = std::min<size_t>(count, buf->aft.size());
    buf->bef.append(buf->aft, 0, count);
    buf->aft.erase(0, count);
    // TODO: Should we set virtual_column if count is 0?  (Can count be 0?)
    buf->win_ctx.virtual_column = std::nullopt;
    recenter_cursor_if_offscreen(&buf->win_ctx, buf);
}

void move_left_by(buffer *buf, size_t count) {
    // TODO: Could both this and move_right_by be the same fn, using buf->set_cursor?
    count = std::min<size_t>(count, buf->bef.size());
    buf->aft.insert(0, buf->bef, buf->bef.size() - count, count);
    buf->bef.resize(buf->bef.size() - count);
    // TODO: Should we set virtual_column if count is 0?  (Can count be 0?)
    buf->win_ctx.virtual_column = std::nullopt;
    recenter_cursor_if_offscreen(&buf->win_ctx, buf);
}

void set_mark(buffer *buf) {
    if (buf->mark.has_value()) {
        buf->replace_mark(*buf->mark, buf->cursor());
    } else {
        buf->mark = buf->add_mark(buf->cursor());
    }
}

}  // namespace qwi

