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

using qwi::buffer_char;

insert_result insert_chars(qwi::buffer *buf, const buffer_char *chs, size_t count) {
    size_t og_cursor = buf->cursor();
    buf->bef.append(chs, count);
    if (buf->mark.has_value()) {
        *buf->mark += (*buf->mark > og_cursor ? count : 0);
    }
    // TODO: Don't recompute virtual_column every time.
    buf->virtual_column = current_column(*buf);
    buf->first_visible_offset += (buf->first_visible_offset > og_cursor ? count : 0);
    recenter_cursor_if_offscreen(buf);
    return insert_result{};
}

void update_offset_for_delete_range(size_t *offset, size_t range_beg, size_t range_end) {
    if (*offset > range_end) {
        *offset -= (range_end - range_beg);
    } else if (*offset > range_beg) {
        *offset = range_beg;
    }
}

delete_result delete_left(qwi::buffer *buf, size_t count) {
    count = std::min<size_t>(count, buf->bef.size());
    size_t og_cursor = buf->bef.size();
    size_t new_cursor = og_cursor - count;
    delete_result ret;
    ret.deletedText.assign(buf->bef, new_cursor, count);
    buf->bef.resize(new_cursor);
    if (buf->mark.has_value()) {
        update_offset_for_delete_range(&*buf->mark, new_cursor, og_cursor);
    }

    buf->virtual_column = current_column(*buf);
    update_offset_for_delete_range(&buf->first_visible_offset, new_cursor, og_cursor);
    recenter_cursor_if_offscreen(buf);
    return ret;
}

delete_result delete_right(qwi::buffer *buf, size_t count) {
    size_t cursor = buf->cursor();
    count = std::min<size_t>(count, buf->aft.size());
    delete_result ret;
    ret.deletedText.assign(buf->aft, 0, count);
    buf->aft.erase(0, count);
    if (buf->mark.has_value()) {
        update_offset_for_delete_range(&*buf->mark, cursor, cursor + count);
    }

    // TODO: We don't do this for doDeleteRight (or doAppendRight) in jsmacs -- the bug is in jsmacs!
    buf->virtual_column = current_column(*buf);
    update_offset_for_delete_range(&buf->first_visible_offset, cursor, cursor + count);
    recenter_cursor_if_offscreen(buf);
    return ret;
}

void move_right_by(qwi::buffer *buf, size_t count) {
    count = std::min<size_t>(count, buf->aft.size());
    buf->bef.append(buf->aft, 0, count);
    buf->aft.erase(0, count);
    // TODO: Should we set virtual_column if count is 0?  (Can count be 0?)
    buf->virtual_column = current_column(*buf);
    recenter_cursor_if_offscreen(buf);
}

void move_left_by(qwi::buffer *buf, size_t count) {
    // TODO: Could both this and move_right_by be the same fn, using buf->set_cursor?
    count = std::min<size_t>(count, buf->bef.size());
    buf->aft.insert(0, buf->bef, buf->bef.size() - count, count);
    buf->bef.resize(buf->bef.size() - count);
    // TODO: Should we set virtual_column if count is 0?  (Can count be 0?)
    buf->virtual_column = current_column(*buf);
    recenter_cursor_if_offscreen(buf);
}

void set_mark(qwi::buffer *buf) {
    buf->mark = buf->cursor();
}
