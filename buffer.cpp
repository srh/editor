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

insert_result insert_chars(buffer *buf, const buffer_char *chs, size_t count) {
    const size_t og_cursor = buf->cursor();
    buf->bef.append(chs, count);
    if (buf->mark.has_value()) {
        *buf->mark += (*buf->mark > og_cursor ? count : 0);
    }
    // TODO: Don't recompute virtual_column every time.
    buf->virtual_column = current_column(*buf);
    buf->first_visible_offset += (buf->first_visible_offset > og_cursor ? count : 0);
    recenter_cursor_if_offscreen(buf);
    return {
        .new_cursor = buf->cursor(),
        .insertedText = buffer_string(chs, count),
        .side = Side::left };
}

insert_result insert_chars_right(buffer *buf, const buffer_char *chs, size_t count) {
    const size_t og_cursor = buf->cursor();
    buf->aft.insert(0, chs, count);
    if (buf->mark.has_value()) {
        // TODO: Is ">= og_cursor" (unlike insert_chars) what we want here?  (Seems like it.)
        *buf->mark += (*buf->mark >= og_cursor ? count : 0);
    }
    buf->virtual_column = current_column(*buf);
    // TODO: Do we want ">= og_cursor" here?  Seems very context-dependent.
    buf->first_visible_offset += (buf->first_visible_offset >= og_cursor ? count : 0);
    recenter_cursor_if_offscreen(buf);
    return {
        .new_cursor = buf->cursor(),
        .insertedText = buffer_string(chs, count),
        .side = Side::right };
}

void update_offset_for_delete_range(size_t *offset, size_t range_beg, size_t range_end) {
    if (*offset > range_end) {
        *offset -= (range_end - range_beg);
    } else if (*offset > range_beg) {
        *offset = range_beg;
    }
}

delete_result delete_left(buffer *buf, size_t og_count) {
    const size_t count = std::min<size_t>(og_count, buf->bef.size());
    size_t og_cursor = buf->bef.size();
    size_t new_cursor = og_cursor - count;

    delete_result ret;
    ret.new_cursor = new_cursor;
    ret.deletedText.assign(buf->bef, new_cursor, count);
    ret.side = Side::left;

    buf->bef.resize(new_cursor);
    if (buf->mark.has_value()) {
        update_offset_for_delete_range(&*buf->mark, new_cursor, og_cursor);
    }

    buf->virtual_column = current_column(*buf);
    update_offset_for_delete_range(&buf->first_visible_offset, new_cursor, og_cursor);
    recenter_cursor_if_offscreen(buf);
    if (count < og_count) {
        ret.error_message = "Beginning of buffer";  // TODO: Bad place for UI logic
    }
    return ret;
}

delete_result delete_right(buffer *buf, size_t og_count) {
    size_t cursor = buf->cursor();
    const size_t count = std::min<size_t>(og_count, buf->aft.size());

    delete_result ret;
    ret.new_cursor = cursor;
    ret.deletedText.assign(buf->aft, 0, count);
    ret.side = Side::right;

    buf->aft.erase(0, count);
    if (buf->mark.has_value()) {
        update_offset_for_delete_range(&*buf->mark, cursor, cursor + count);
    }

    // TODO: We don't do this for doDeleteRight (or doAppendRight) in jsmacs -- the bug is in jsmacs!
    buf->virtual_column = current_column(*buf);
    update_offset_for_delete_range(&buf->first_visible_offset, cursor, cursor + count);
    recenter_cursor_if_offscreen(buf);
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
    buf->virtual_column = current_column(*buf);
    recenter_cursor_if_offscreen(buf);
}

void move_left_by(buffer *buf, size_t count) {
    // TODO: Could both this and move_right_by be the same fn, using buf->set_cursor?
    count = std::min<size_t>(count, buf->bef.size());
    buf->aft.insert(0, buf->bef, buf->bef.size() - count, count);
    buf->bef.resize(buf->bef.size() - count);
    // TODO: Should we set virtual_column if count is 0?  (Can count be 0?)
    buf->virtual_column = current_column(*buf);
    recenter_cursor_if_offscreen(buf);
}

void set_mark(buffer *buf) {
    buf->mark = buf->cursor();
}

}  // namespace qwi

