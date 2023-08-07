#include "buffer.hpp"

#include <string.h>

#include "arith.hpp"
#include "error.hpp"

// TODO: Slightly unhappy about this include dependency -- should the ui logic updates be
// applied outside these fn's?  Event first_visible_offset?
#include "term_ui.hpp"

using qwi::buffer_char;

void insert_chars(qwi::buffer *buf, const buffer_char *chs, size_t count) {
    size_t og_cursor = buf->cursor();
    buf->bef.append(chs, count);
    if (buf->mark.has_value()) {
        *buf->mark += (*buf->mark > og_cursor ? count : 0);
    }
    // TODO: Don't recompute virtual_column every time.
    buf->virtual_column = current_column(*buf);
    buf->first_visible_offset += (buf->first_visible_offset > og_cursor ? count : 0);
    recenter_cursor_if_offscreen(buf);
}

void update_offset_for_delete_range(size_t *offset, size_t range_beg, size_t range_end) {
    if (*offset > range_end) {
        *offset -= (range_end - range_beg);
    } else if (*offset > range_beg) {
        *offset = range_beg;
    }
}

void delete_left(qwi::buffer *buf, size_t count) {
    count = std::min<size_t>(count, buf->bef.size());
    size_t og_cursor = buf->bef.size();
    size_t new_cursor = og_cursor - count;
    buf->bef.resize(new_cursor);
    if (buf->mark.has_value()) {
        update_offset_for_delete_range(&*buf->mark, new_cursor, og_cursor);
    }

    buf->virtual_column = current_column(*buf);
    update_offset_for_delete_range(&buf->first_visible_offset, new_cursor, og_cursor);
    recenter_cursor_if_offscreen(buf);
}

void delete_right(qwi::buffer *buf, size_t count) {
    size_t cursor = buf->cursor();
    count = std::min<size_t>(count, buf->aft.size());
    buf->aft.erase(0, count);
    if (buf->mark.has_value()) {
        update_offset_for_delete_range(&*buf->mark, cursor, cursor + count);
    }

    // TODO: We don't do this for doDeleteRight (or doAppendRight) in jsmacs -- the bug is in jsmacs!
    buf->virtual_column = current_column(*buf);
    update_offset_for_delete_range(&buf->first_visible_offset, cursor, cursor + count);
    recenter_cursor_if_offscreen(buf);
}

void kill_line(qwi::buffer *buf) {
    // TODO: Store killed lines and clumps in kill ring.
    size_t eolDistance = qwi::distance_to_eol(*buf, buf->cursor());

    if (eolDistance == 0 && buf->cursor() < buf->size()) {
        // TODO: Record yank of this newline character.
        delete_right(buf, 1);
    } else {
        delete_right(buf, eolDistance);
    }
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

bool is_solid(buffer_char bch) {
    uint8_t ch = bch.value;
    // TODO: Figure out what chars should be in this set.
    // Used by move_forward_word, move_backward_word.
    return (ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9');
}

size_t forward_word_distance(const qwi::buffer *buf) {
    const size_t cursor = buf->cursor();
    size_t i = cursor;
    bool reachedSolid = false;
    for (; i < buf->size(); ++i) {
        buffer_char ch = buf->get(i);
        if (is_solid(ch)) {
            reachedSolid = true;
        } else if (reachedSolid) {
            break;
        }
    }
    return i - cursor;
}

size_t backward_word_distance(const qwi::buffer *buf) {
    const size_t cursor = buf->cursor();
    size_t count = 0;
    bool reachedSolid = false;
    while (count < cursor) {
        buffer_char ch = buf->get(cursor - (count + 1));
        if (is_solid(ch)) {
            reachedSolid = true;
        } else if (reachedSolid) {
            break;
        }

        ++count;
    }
    return count;
}

void move_forward_word(qwi::buffer *buf) {
    size_t d = forward_word_distance(buf);
    move_right_by(buf, d);
}

void move_backward_word(qwi::buffer *buf) {
    size_t d = backward_word_distance(buf);
    move_left_by(buf, d);
}

// Maybe move_up and move_down should be in term_ui.cpp.
void move_up(qwi::buffer *buf) {
    const size_t window_cols = buf->window.cols;
    // We're not interested in virtual_column as a "line column" -- just interested in the
    // visual distance to beginning of line.
    const size_t target_column = buf->virtual_column % window_cols;

    // Basically we want the output of the following algorithm:
    // 1. Render the current line up to the cursor.
    // 2. Note the row the cursor's on.
    // 3. Back up to the previous row.
    // 4. If at least one character starts on the preceding row, set the cursor to the greater of:
    //    - the last character whose starting column is <= virtual_column
    //    - the first character that starts on the preceding row
    // 4-b. If NO characters start on the preceding row (e.g. window_cols == 3 and we have
    // a tab character), set the cursor to the last character that starts before the
    // preceding row.

    // Note that Emacs (in GUI mode, at least) never encounters the 4-b case because it
    // switches to line truncation when the window width gets low.

    const size_t cursor = buf->cursor();
    const size_t bol1 = cursor - buf->cursor_distance_to_beginning_of_line();
    const size_t bol = bol1 == 0 ? 0 : (bol1 - 1) - qwi::distance_to_beginning_of_line(*buf, bol1 - 1);

    // So we're going to render forward from bol, which is the beginning of the previous
    // "real" line.  For each row, we'll track the current proposed cursor position,
    // should that row end up being the previous line.
    size_t col = 0;
    size_t line_col = 0;
    size_t prev_row_cursor_proposal = SIZE_MAX;
    size_t current_row_cursor_proposal = bol;
    for (size_t i = bol; i < cursor; ++i) {
        buffer_char ch = buf->get(i);
        char_rendering rend = compute_char_rendering(ch, &line_col);
        if (rend.count == SIZE_MAX) {
            // is eol
            prev_row_cursor_proposal = current_row_cursor_proposal;
            col = 0;
            current_row_cursor_proposal = i + 1;
        } else {
            col += rend.count;
            if (col >= window_cols) {
                // Line wrapping case.
                col -= window_cols;
                prev_row_cursor_proposal = current_row_cursor_proposal;
                if (col >= window_cols) {
                    // Special narrow window case.
                    prev_row_cursor_proposal = i;
                    do {
                        col -= window_cols;
                    } while (col >= window_cols);
                }

                current_row_cursor_proposal = i + 1;
            } else {
                if (col <= target_column) {
                    current_row_cursor_proposal = i + 1;
                }
            }
        }
    }

    if (prev_row_cursor_proposal == SIZE_MAX) {
        // We're already on the top row.
        return;
    }
    buf->set_cursor(prev_row_cursor_proposal);
    recenter_cursor_if_offscreen(buf);
}

void move_down(qwi::buffer *buf) {
    const size_t window_cols = buf->window.cols;
    const size_t target_column = buf->virtual_column % window_cols;

    // Remember we do some traversing in current_column.
    size_t line_col = current_column(*buf);
    size_t col = line_col % window_cols;

    // Simple: We walk forward until the number of rows traversed is >= 1 _and_ we're at
    // either the first char of the row or the last char whose col is <= target_column.
    // We use candidate_index != SIZE_MAX to determine if we've entered the next line.

    size_t candidate_index = SIZE_MAX;
    for (size_t i = buf->cursor(), e = buf->size(); i < e; ++i) {
        buffer_char ch = buf->get(i);
        char_rendering rend = compute_char_rendering(ch, &line_col);
        if (rend.count == SIZE_MAX) {
            if (candidate_index != SIZE_MAX) {
                break;
            }
            col = 0;
            // The first index of the next line is always a candidate.
            candidate_index = i + 1;
        } else {
            col += rend.count;
            if (col >= window_cols) {
                if (candidate_index != SIZE_MAX) {
                    break;
                }
                do {
                    col -= window_cols;
                } while (col >= window_cols);
                // The first index of the next line is always a candidate.
                candidate_index = i + 1;
            } else {
                if (candidate_index != SIZE_MAX && col <= target_column) {
                    candidate_index = i + 1;
                }
            }
        }
    }

    if (candidate_index == SIZE_MAX) {
        candidate_index = buf->size();
    }

    buf->set_cursor(candidate_index);
    recenter_cursor_if_offscreen(buf);
}

void move_home(qwi::buffer *buf) {
    // TODO: Use uh, screen home and screen end?
    size_t distance = buf->cursor_distance_to_beginning_of_line();
    move_left_by(buf, distance);
}

void move_end(qwi::buffer *buf) {
    size_t distance = qwi::distance_to_eol(*buf, buf->cursor());
    move_right_by(buf, distance);
}

void set_mark(qwi::buffer *buf) {
    buf->mark = buf->cursor();
}
