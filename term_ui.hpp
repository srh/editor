#ifndef QWERTILLION_TERM_UI_HPP_
#define QWERTILLION_TERM_UI_HPP_

#include <stdint.h>

#include <optional>
#include <vector>

#include "state.hpp"
// For terminal_size.  Not happy about this include dependency.
#include "terminal.hpp"

namespace qwi {

/* Generally, buffer logic that is more terminal UI-related than buffer-specific is
   located here.  Maybe move_up and move_down should be placed here.  It's not a 100%
   clean separation.  Includes terminal_frame and render_into_frame because some general
   buffer-update code makes use of it (instead of slickly duplicating its logic). */

// Used in rendering of control characters -- also used for a human-readable switch
// statement in main.cpp input processing.
constexpr uint8_t CTRL_XOR_MASK = 64;

struct terminal_char {
    uint8_t value;
    char as_char() const { return char(value); }
};

inline const char *as_chars(const terminal_char *p) {
    static_assert(sizeof(terminal_char) == 1);
    return reinterpret_cast<const char *>(p);
}

struct terminal_style {
    // Zero means normal.
    static constexpr uint8_t BOLD_BIT = 1 << 0;
    // static constexpr WHITE_ON_RED_BIT = 1 << 1;
    static terminal_style zero() { return terminal_style{0}; }
    static terminal_style bold() { return terminal_style{BOLD_BIT}; }

    uint8_t mask = 0;
};

// A coordinate relative to the terminal frame (as opposed to some smaller buffer window).
struct terminal_coord { uint32_t row = 0, col = 0; };
struct terminal_frame {
    // Carries the presumed window size that the frame was rendered for.
    terminal_size window;
    // Cursor pos (0..<window.*)

    // nullopt means it's invisible.
    std::optional<terminal_coord> cursor;

    // data.size() = u32_mul(window.rows, window.cols).
    std::vector<terminal_char> data;

    // Same length and dimensions as `data`.
    std::vector<terminal_style> style_data;

    // Doesn't really belong here -- we dump window size by buffer_id here, so we can
    // update the buffer ui contexts with the last rendered window size after rendering.
    std::vector<std::pair<buffer_id, window_size>> rendered_window_sizes;
};

struct window_coord { uint32_t row = 0, col = 0; };
struct render_coord {
    size_t buf_pos;
    std::optional<window_coord> rendered_pos;
};

struct char_rendering {
    terminal_char buf[8];
    size_t count;  // SIZE_MAX means newline
};

char_rendering compute_char_rendering(const buffer_char bch, size_t *line_col);

terminal_frame init_frame(const terminal_size& window);

// render_coords must be sorted by buf_pos.
// render_frame doesn't render the cursor -- that's computed with render_coords and rendered then.
void render_into_frame(terminal_frame *frame_ptr, terminal_coord window_topleft,
                       const window_size& window, const ui_window_ctx& ui, const buffer& buf, std::vector<render_coord> *render_coords);


bool too_small_to_render(const window_size& window);

// This isn't some option you can configure.
constexpr bool INIT_FRAME_INITIALIZES_WITH_SPACES = true;


size_t pos_current_column(const buffer& buf, const size_t pos);
size_t current_column(const buffer& buf);
void recenter_cursor_if_offscreen(ui_window_ctx *ui, buffer *buf);

// Changes buf->window; also resets virtual_column.
void resize_buf_window(ui_window_ctx *ui, const window_size& buf_window);

}  // namespace qwi

#endif  // QWERTILLION_TERM_UI_HPP_
