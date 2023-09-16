#ifndef QWERTILLION_EDITING_HPP_
#define QWERTILLION_EDITING_HPP_

#include "buffer.hpp"
#include "state.hpp"

namespace qwi {

// Callers will need to handle undo.
#if 0
inline undo_killring_handled undo_will_need_handling() {
    return undo_killring_handled{};
}
inline undo_killring_handled killring_will_need_handling() {
    return undo_killring_handled{};
}
#endif  // 0

inline undo_killring_handled handled_undo_killring(state *state, buffer *buf) {
    (void)state, (void)buf;
    return undo_killring_handled{};
}
inline undo_killring_handled handled_undo_killring_no_buf(state *state) {
    (void)state;
    return undo_killring_handled{};
}

undo_killring_handled note_backout_action(state *state, buffer *buf);
undo_killring_handled note_bufless_backout_action(state *state);
undo_killring_handled note_navigation_action(state *state, buffer *buf);
undo_killring_handled note_action(state *state, buffer *buf, insert_result&& i_res);
undo_killring_handled note_coalescent_action(state *state, buffer *buf, insert_result&& i_res);

undo_killring_handled open_file_action(state *state, buffer *active_buf);
undo_killring_handled save_file_action(state *state, buffer *active_buf);
undo_killring_handled save_as_file_action(state *state, buffer *active_buf);
undo_killring_handled exit_cleanly(state *state, buffer *active_buf, bool *exit_loop);  // L312
undo_killring_handled buffer_switch_action(state *state, buffer *active_buf);
buffer open_file_into_detached_buffer(state *state, const std::string& dirty_path);
void apply_number_to_buf(state *state, buffer_id buf_id);
buffer scratch_buffer(buffer_id id);
undo_killring_handled enter_handle_status_prompt(state *state, bool *exit_loop);
undo_killring_handled note_coalescent_action(state *state, buffer *buf, delete_result&& d_res);

undo_killring_handled buffer_close_action(state *state, buffer *active_buf);
bool find_buffer_by_name(const state *state, const std::string& text, buffer_id *out);
undo_killring_handled cancel_action(state *state, buffer *buf);

undo_killring_handled delete_backward_word(state *state, ui_window_ctx *ui, buffer *buf);
undo_killring_handled delete_forward_word(state *state, ui_window_ctx *ui, buffer *buf);
undo_killring_handled kill_line(state *state, ui_window_ctx *ui, buffer *buf);
undo_killring_handled kill_region(state *state, ui_window_ctx *ui, buffer *buf);
undo_killring_handled copy_region(state *state, buffer *buf);

void rotate_to_buffer(state *state, buffer_id buf_id);
undo_killring_handled rotate_buf_right(state *state, buffer *active_buf);
undo_killring_handled rotate_buf_left(state *state, buffer *active_buf);
undo_killring_handled yank_from_clipboard(state *state, ui_window_ctx *ui, buffer *buf);
undo_killring_handled alt_yank_from_clipboard(state *state, ui_window_ctx *ui, buffer *buf);

undo_killring_handled split_vertically(state *state, buffer *active_buf);
undo_killring_handled split_horizontally(state *state, buffer *active_buf);
enum class ortho_direction { Left, Right, Up, Down };
undo_killring_handled grow_window_size(state *state, buffer *active_buf, ortho_direction direction);

undo_killring_handled buffer_switch_action(state *state, buffer *active_buf);
undo_killring_handled help_menu(state *state);

}  // namespace qwi

#endif  // QWERTILLION_EDITING_HPP_
