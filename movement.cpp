#include "movement.hpp"

#include "arith.hpp"
#include "term_ui.hpp"

namespace qwi {

bool is_solid(buffer_char bch) {
    uint8_t ch = bch.value;
    // TODO: Figure out what chars should be in this set.
    // Used by move_forward_word, move_backward_word.
    return (ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9');
}

size_t forward_word_distance(const buffer *buf, const size_t cursor) {
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

size_t backward_word_distance(const buffer *buf, const size_t cursor) {
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

void move_forward_word(ui_window_ctx *ui, buffer *buf) {
    // TODO: It might be cleaner to compute the offset, instead of distance.  Then set the
    // cursor more directly.  Ditto in move_backward_word.
    size_t d = forward_word_distance(buf, get_ctx_cursor(ui, buf));
    move_right_by(ui, buf, d);
}

void move_backward_word(ui_window_ctx *ui, buffer *buf) {
    size_t d = backward_word_distance(buf, get_ctx_cursor(ui, buf));
    move_left_by(ui, buf, d);
}

// Maybe move_up and move_down should be in term_ui.cpp.
void move_up(ui_window_ctx *ui, buffer *buf) {
    const size_t cursor = get_ctx_cursor(ui, buf);

    const size_t window_cols = ui->window_cols_or_maxval();
    // We're not interested in virtual_column as a "line column" -- just interested in the
    // visual distance to beginning of line.  We'll have to change this logic once we have word wrapping.
    ensure_virtual_column_initialized(ui, buf);
    const size_t target_column = *ui->virtual_column % window_cols;

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

    const size_t bol1 = cursor - distance_to_beginning_of_line(*buf, cursor);
    const size_t bol = bol1 == 0 ? 0 : (bol1 - 1) - distance_to_beginning_of_line(*buf, bol1 - 1);

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
            // Technically (and practically!) an overflow is possible here with say,
            // 3GB file that is all control or tab characters.
            // TODO: Handle this weird overflow case cleanly.
            col = size_add(col, rend.count);
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
    buf->set_cursor_(prev_row_cursor_proposal);  // For UI rendering (line,col) perf.
    set_ctx_cursor(ui, buf);
    recenter_cursor_if_offscreen(ui, buf);
}

void move_down(ui_window_ctx *ui, buffer *buf) {
    const size_t cursor = get_ctx_cursor(ui, buf);
    const size_t window_cols = ui->window_cols_or_maxval();
    // TODO: This may compute current_column -- if it does, reuse the value below.
    ensure_virtual_column_initialized(ui, buf);
    const size_t target_column = *ui->virtual_column % window_cols;

    // Remember we do some traversing in current_column.
    size_t line_col = pos_current_column(*buf, cursor);
    size_t col = line_col % window_cols;

    // Simple: We walk forward until the number of rows traversed is >= 1 _and_ we're at
    // either the first char of the row or the last char whose col is <= target_column.
    // We use candidate_index != SIZE_MAX to determine if we've entered the next line.

    size_t candidate_index = SIZE_MAX;
    for (size_t i = cursor, e = buf->size(); i < e; ++i) {
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

    buf->set_cursor_(candidate_index);
    set_ctx_cursor(ui, buf);
    recenter_cursor_if_offscreen(ui, buf);
}

void move_home(ui_window_ctx *ui, buffer *buf) {
    size_t distance = distance_to_beginning_of_line(*buf, buf->get_mark_offset(ui->cursor_mark));
    move_left_by(ui, buf, distance);
}

void move_end(ui_window_ctx *ui, buffer *buf) {
    size_t distance = distance_to_eol(*buf, buf->get_mark_offset(ui->cursor_mark));
    move_right_by(ui, buf, distance);
}

}
