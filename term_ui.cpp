#include "term_ui.hpp"

#include <string.h>

#include "arith.hpp"

using qwi::buffer_char;

size_t pos_current_column(const qwi::buffer& buf, const size_t pos) {
    size_t line_col = 0;
    bool saw_newline = false;
    for (size_t i = pos - qwi::distance_to_beginning_of_line(buf, pos); i < pos; ++i) {
        buffer_char ch = buf.get(i);
        char_rendering rend = compute_char_rendering(ch, &line_col);
        saw_newline |= (rend.count == SIZE_MAX);
    }
    runtime_check(!saw_newline, "encountered impossible newline in pos_current_column");

    return line_col;
}

// TODO: Fix this cyclic reference cleanly somehow.
// Used in buffer.cpp
size_t current_column(const qwi::buffer& buf) {
    return pos_current_column(buf, buf.cursor());
}

constexpr uint8_t TAB_MOD_MASK = 7;  // 8 is hard-coded tab stop

// Returns true if not '\n'.  Sets *line_col in any case.  Calls emit_drawn_chars(char *,
// size_t) once to pass out chars to be rendered in the terminal (except when a newline is
// encountered).  Always passes a count of 1 or greater to emit_drawn_chars.
char_rendering compute_char_rendering(const buffer_char bch, size_t *line_col) {
    const uint8_t ch = bch.value;
    char_rendering ret;
    if (ch == '\n') {
        *line_col = 0;
        ret.count = SIZE_MAX;
    } else {
        if (ch == '\t') {
            size_t next_line_col = size_add((*line_col) | TAB_MOD_MASK, 1);
            //             12345678
            char buf[8] = { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
            memcpy(ret.buf, buf, sizeof(ret.buf));
            ret.count = next_line_col - *line_col;
        } else if (ch < 32 || ch == 127) {
            ret.buf[0] = '^';
            ret.buf[1] = char(ch ^ CTRL_XOR_MASK);
            ret.count = 2;
        } else {
            // I guess 128-255 get rendered verbatim.
            ret.buf[0] = char(ch);
            ret.count = 1;
        }
        *line_col += ret.count;
    }
    return ret;
}

terminal_frame init_frame(const terminal_size& window) {
    terminal_frame ret;
    static_assert(INIT_FRAME_INITIALIZES_WITH_SPACES);
    ret.data.resize(u32_mul(window.rows, window.cols), ' ');
    ret.window = window;
    return ret;
}

// render_coords must be sorted by buf_pos.
// render_frame doesn't render the cursor -- that's computed with render_coords and rendered then.
void render_into_frame(terminal_frame *frame_ptr, terminal_coord window_topleft,
                       const qwi::buffer& buf, std::vector<render_coord> *render_coords) {
    terminal_frame& frame = *frame_ptr;
    const qwi::window_size window = buf.window;
    // Some very necessary checks for memory safety.
    runtime_check(u32_add(window_topleft.row, window.rows) <= frame.window.rows,
                  "buf window rows exceeds frame window");
    runtime_check(u32_add(window_topleft.col, window.cols) <= frame.window.cols,
                  "buf window cols exceeds frame window");

    // first_visible_offset is the first rendered character in the buffer -- this may be a
    // tab character or 2-column-rendered control character, only part of which was
    // rendered.  We render the entire line first_visible_offset was part of, and
    // copy_row_if_visible conditionally copies the line into the `frame` for rendering --
    // taking care to call it before incrementing i for partially rendered characters, and
    // _after_ incrementing i for the completely rendered character.

    // TODO: We actually don't want to re-render a whole line
    size_t i = buf.first_visible_offset - distance_to_beginning_of_line(buf, buf.first_visible_offset);

    std::vector<char> render_row(window.cols, 0);
    size_t render_coords_begin = 0;
    size_t render_coords_end = 0;
    size_t line_col = 0;
    size_t col = 0;
    size_t row = 0;
    // This gets called after we paste our character into the row and i is the offset
    // after the last completely written character.  Called precisely when col ==
    // window.cols.
    auto copy_row_if_visible = [&]() {
        col = 0;
        if (i > buf.first_visible_offset) {
            // It simplifies code to throw in this (row < window.rows) check here, instead
            // of carefully calculating where we might need to check it.
            if (row < window.rows) {
                memcpy(&frame.data[(window_topleft.row + row) * frame.window.cols + window_topleft.col], render_row.data(), window.cols);

                while (render_coords_begin < render_coords_end) {
                    (*render_coords)[render_coords_begin].rendered_pos->row = row;
                    ++render_coords_begin;
                }
            }
            ++row;
        }
        // Note that this only does anything if the while loop above wasn't hit.
        while (render_coords_begin < render_coords_end) {
            (*render_coords)[render_coords_begin].rendered_pos = std::nullopt;
            ++render_coords_begin;
        }
    };
    size_t render_coord_target = render_coords_end < render_coords->size() ? (*render_coords)[render_coords_end].buf_pos : SIZE_MAX;
    while (row < window.rows && i < buf.size()) {
        // col < window.cols.
        while (i == render_coord_target) {
            (*render_coords)[render_coords_end].rendered_pos = {UINT32_MAX, uint32_t(col)};
            ++render_coords_end;
            render_coord_target = render_coords_end < render_coords->size() ? (*render_coords)[render_coords_end].buf_pos : SIZE_MAX;
        }

        buffer_char ch = buf.get(i);

        char_rendering rend = compute_char_rendering(ch, &line_col);
        if (rend.count != SIZE_MAX) {
            // Always, count > 0.
            for (size_t j = 0; j < rend.count - 1; ++j) {
                render_row[col] = rend.buf[j];
                ++col;
                if (col == window.cols) {
                    copy_row_if_visible();
                }
            }
            render_row[col] = rend.buf[rend.count - 1];
            ++col;
            ++i;
            if (col == window.cols) {
                copy_row_if_visible();
            }
        } else {
            // TODO: We could use '\x1bK'
            // clear to EOL
            do {
                render_row[col] = ' ';
                ++col;
            } while (col < window.cols);
            ++i;
            copy_row_if_visible();
        }
    }

    while (i == render_coord_target) {
        (*render_coords)[render_coords_end].rendered_pos = {UINT32_MAX, uint32_t(col)};
        ++render_coords_end;
        render_coord_target = render_coords_end < render_coords->size() ? (*render_coords)[render_coords_end].buf_pos : SIZE_MAX;
    }

    // If we reached end of buffer, we might still need to copy the current render_row and
    // the remaining screen.
    while (row < window.rows) {
        do {
            render_row[col] = ' ';
            ++col;
        } while (col < window.cols);
        memcpy(&frame.data[(window_topleft.row + row) * frame.window.cols + window_topleft.col], render_row.data(), window.cols);
        while (render_coords_begin < render_coords_end) {
            (*render_coords)[render_coords_begin].rendered_pos->row = row;
            ++render_coords_begin;
        }
        ++row;
        col = 0;
    }
    while (render_coords_begin < render_coords_end) {
        (*render_coords)[render_coords_begin].rendered_pos = std::nullopt;
        ++render_coords_begin;
    }
}

bool too_small_to_render(const qwi::window_size& window) {
    return window.cols < 2 || window.rows == 0;
}

bool cursor_is_offscreen(qwi::buffer *buf, size_t cursor) {
    if (too_small_to_render(buf->window)) {
        // Return false?
        return false;
    }

    // We might say this is generic code -- even if the buf is for a smaller window, this
    // terminal frame is artificially constructed.

    if (cursor < buf->first_visible_offset) {
        // Take this easy early exit.  Note that sometimes when cursor ==
        // buf->first_visible_offset we still will return true.
        return true;
    }

    terminal_size window = terminal_size{buf->window.rows, buf->window.cols};
    terminal_frame frame = init_frame(window);
    std::vector<render_coord> coords = { {cursor, std::nullopt} };
    terminal_coord window_topleft = { 0, 0 };
    render_into_frame(&frame, window_topleft, *buf, &coords);
    return !coords[0].rendered_pos.has_value();
}

// Scrolls buf so that buf_pos is close to rowno.  (Sometimes it can't get there, e.g. we
// can't scroll past front of buffer, or a very narrow window might force buf_pos's row <
// rowno without equality).
void scroll_to_row(qwi::buffer *buf, const uint32_t rowno, const size_t buf_pos) {
    // We're going to back up and render one line at a time.
    const size_t window_cols = buf->window.cols;

    size_t rows_stepbacked = 0;
    size_t pos = buf_pos;
    for (;;) {
        size_t col = pos_current_column(*buf, pos);
        size_t row_in_line = col / window_cols;
        rows_stepbacked += row_in_line;
        pos = pos - qwi::distance_to_beginning_of_line(*buf, pos);
        if (rows_stepbacked == rowno || pos == 0) {
            // First visible offset is pos, at beginning of line.
            buf->first_visible_offset = pos;
            return;
        } else if (rows_stepbacked < rowno) {
            // pos > 0, as we tested.
            --pos;
            ++rows_stepbacked;
        } else {
            // We stepped back too far -- first_visible_offset >= pos and <= previous
            // value of pos.
            break;
        }
    }

    // We stepped back too far.  We have rows_stepbacked > rowno.  pos is the beginning of
    // the line.  We want to walk pos forward until we've rendered `rows_stepbacked -
    // rowno` lines, and compute a new first_visible_offset.

    size_t i = pos;
    size_t line_col = 0;
    size_t col = 0;
    bool saw_newline = false;
    for (;; ++i) {
        if (i == buf_pos) {
            // idk how this would be possible; just a simple way to prove no infinite traversal.
            buf->first_visible_offset = pos;
            break;
        }

        buffer_char ch = buf->get(i);
        char_rendering rend = compute_char_rendering(ch, &line_col);
        saw_newline |= (rend.count == SIZE_MAX);
        col += rend.count == SIZE_MAX ? 0 : rend.count;
        while (col >= window_cols) {
            --rows_stepbacked;
            col -= window_cols;
            if (rows_stepbacked == rowno) {
                // Now what?  If col > 0, then first_visible_offset is i.  If col == 0, then
                // first_visible_offset is i + 1.
                buf->first_visible_offset = i + (col == 0);

                goto done_loop;
            }
        }
    }
 done_loop:
    runtime_check(!saw_newline, "encountered impossible newline in scroll_to_row");
}

// Scrolls buf so that buf_pos is close to the middle (as close as possible, e.g. if it's
// too close to the top of the buffer, it'll be above the middle).  buf_pos is probably
// the cursor position.
void scroll_to_mid(qwi::buffer *buf, size_t buf_pos) {
    scroll_to_row(buf, buf->window.rows / 2, buf_pos);
}

void recenter_cursor_if_offscreen(qwi::buffer *buf) {
    if (cursor_is_offscreen(buf, buf->cursor())) {
        scroll_to_mid(buf, buf->cursor());
    }
}

void resize_buf_window(qwi::buffer *buf, const qwi::window_size& buf_window) {
    // This means the window actually changed -- we need to set the column.
    buf->set_window(buf_window);
    buf->virtual_column = current_column(*buf);
}
