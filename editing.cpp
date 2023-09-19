#include "editing.hpp"

#include <filesystem>
#include <fstream>
#include <unordered_set>

#include "io.hpp"
#include "movement.hpp"
#include "layout.hpp"
#include "util.hpp"

namespace fs = std::filesystem;

namespace qwi {

atomic_undo_item make_reverse_action(const undo_history *history, insert_result&& i_res) {
    atomic_undo_item item = {
        .beg = i_res.new_cursor,
        .text_inserted = buffer_string{},
        .text_deleted = std::move(i_res.insertedText),
        .side = i_res.side,  // We inserted on left (right), hence we delete on left (right)
        .before_node = history->unused_node_number(),
        .after_node = history->current_node,
    };

    return item;
}

void note_undo(buffer *buf, insert_result&& i_res) {
    // Make and add the _reverse_ action in the undo items.
    // (Why the reverse action?  Because jsmacs did it that way.)

    add_edit(&buf->undo_info, make_reverse_action(&buf->undo_info, std::move(i_res)));
}

void note_nop_undo(buffer *buf) {
    add_nop_edit(&buf->undo_info);
}

undo_killring_handled note_action(state *state, buffer *buf, insert_result&& i_res) {
    no_yank(&state->clipboard);

    note_undo(buf, std::move(i_res));
    state->clear_error_message();
    return undo_killring_handled{};
}

undo_killring_handled note_coalescent_action(state *state, buffer *buf, insert_result&& i_res) {
    no_yank(&state->clipboard);

    add_coalescent_edit(&buf->undo_info, make_reverse_action(&buf->undo_info, std::move(i_res)),
                        undo_history::char_coalescence::insert_char);
    state->clear_error_message();
    return undo_killring_handled{};
}

atomic_undo_item make_reverse_action(const undo_history *history, delete_result&& d_res) {
    atomic_undo_item item = {
        .beg = d_res.new_cursor,
        .text_inserted = std::move(d_res.deletedText),
        .text_deleted = buffer_string{},
        .side = d_res.side,  // We deleted on left (right), hence we insert on left (right)
        .before_node = history->unused_node_number(),
        .after_node = history->current_node,
    };

    return item;
}

void note_undo(buffer *buf, delete_result&& d_res) {
    add_edit(&buf->undo_info, make_reverse_action(&buf->undo_info, std::move(d_res)));
}

undo_killring_handled note_action(state *state, buffer *buf, delete_result&& d_res) {
    no_yank(&state->clipboard);

    state->note_error_message(std::move(d_res.error_message));
    note_undo(buf, std::move(d_res));
    return undo_killring_handled{};
}

undo_killring_handled note_coalescent_action(state *state, buffer *buf, delete_result&& d_res) {
    no_yank(&state->clipboard);

    state->note_error_message(std::move(d_res.error_message));
    Side side = d_res.side;
    using char_coalescence = undo_history::char_coalescence;
    add_coalescent_edit(&buf->undo_info, make_reverse_action(&buf->undo_info, std::move(d_res)),
                        side == Side::left ? char_coalescence::delete_left : char_coalescence::delete_right);
    return undo_killring_handled{};
}

// Callers want to back out of any killring stuff, but don't want to break undo history
// for some reason.
undo_killring_handled note_noundo_killring_action(state *state, buffer *buf) {
    no_yank(&state->clipboard);
    add_coalescence_break(&buf->undo_info);
    state->clear_error_message();
    return undo_killring_handled{};
}

// An action that backs out of the yank sequence or some undo sequence.  C-g typed into a
// buffer, for example.
undo_killring_handled note_backout_action(state *state, buffer *buf) {
    no_yank(&state->clipboard);
    add_nop_edit(&buf->undo_info);
    state->clear_error_message();
    return undo_killring_handled{};
}

// When we don't have a buf (as with certain types of prompts, soon), or we're going to
// destruct the buf anyway.
undo_killring_handled note_bufless_backout_action(state *state) {
    no_yank(&state->clipboard);
    state->clear_error_message();
    return undo_killring_handled{};
}

// Possibly a useless categorization -- maybe useful for refactoring later.
undo_killring_handled note_navigation_action(state *state, buffer *buf) {
    return note_backout_action(state, buf);
}

// For the purposes of undo or killring, an action which did not happen.  One completely
// out of the context of editing, I guess.
undo_killring_handled note_nop_action(state *state) {
    (void)state;
    return undo_killring_handled{};
}

// TODO: We don't call this everywhere we should.  (e.g. file open, buffer close actions.)
//
// I'm not even sure whether this should cover window switching or not.  Should we remove this?
//
// Currently a nop, we might need a generic action or code adjustments in the future.
// (Current callers also invoke note_navigation_action.)
void note_navigate_away_from_buf(ui_window *win, buffer *buf) {
    (void)win, (void)buf;
}

// TODO: Make a special fn for yes/no prompts?
prompt buffer_close_prompt(buffer&& initialBuf) {
    return {prompt::type::proc, std::move(initialBuf), "close without saving? (yes/no): ",
        [](state *state, buffer&& promptBuf, bool *) {
            // killring important, undo not because we're destructing the status_prompt buf.
            undo_killring_handled ret = note_backout_action(state, &promptBuf);
            std::string text = promptBuf.copy_to_string();
            if (text == "yes") {
                // Yes, close without saving.

                // 1. Detach from all windows.
                // 2. Close.

                buffer_id closed_id = state->active_window()->active_buf().first;

                // We'll need to do this for every window.
                auto range = state->win_range();
                std::vector<bool> needs_new_target;  // Absolutely gross parallel vector.
                bool any_need_new_target = false;
                for (ui_window *w = range.first; w < range.second; ++w) {
                    bool win_needs_new_target = w->detach_if_attached(state->lookup(closed_id));
                    needs_new_target.push_back(win_needs_new_target);
                    any_need_new_target |= win_needs_new_target;
                }

                state->buf_set.erase(closed_id);

                // buf_set or the window's tab set might be empty.  Not allowed.
                for (size_t i = 0, e = needs_new_target.size(); i < e; ++i) {
                    if (needs_new_target[i]) {
                        if (std::optional<buffer_id> buf_id = state->pick_buf_for_empty_window()) {
                            // buf_set is not empty, it has `*buf_id`.
                            range.first[i].point_at(*buf_id, state);
                        } else {
                            // buf_set is empty.

                            // There are no buffers (at all).  Create a scratch buffer.
                            buffer_id scratch_id = state->gen_buf_id();
                            state->buf_set.emplace(scratch_id,
                                                   std::make_unique<buffer>(scratch_buffer(scratch_id)));
                            // buf_set is not empty.
                            apply_number_to_buf(state, scratch_id);
                            range.first[i].point_at(scratch_id, state);
                        }
                    } else {
                        // buf_set is not empty in this case because the window is pointing at a buffer.
                        logic_checkg(!state->buf_set.empty());
                    }
                }

                return ret;
            } else if (text == "no") {
                // No, don't close without saving.
                return ret;
            } else {
                state->note_error_message("Please type yes or no");
                state->status_prompt = buffer_close_prompt(std::move(promptBuf));
                return ret;
            }
        }};
}

undo_killring_handled buffer_close_action(state *state, buffer *active_buf) {
    undo_killring_handled ret = note_backout_action(state, active_buf);
    if (state->status_prompt.has_value()) {
        state->note_error_message("Cannot close buffer while prompt is active");  // TODO: UI logic
        return ret;
    }

    state->status_prompt = buffer_close_prompt(buffer(state->gen_buf_id()));
    return ret;
}

bool find_buffer_by_name(const state *state, const std::string& text, buffer_id *out) {
    // O(n^2) algo
    for (auto& p : state->buf_set) {
        if (buffer_name_str(state, p.first) == text) {
            *out = p.first;
            return true;
        }
    }
    return false;
}

undo_killring_handled cancel_action(state *state, buffer *buf) {
    // We break the yank and undo sequence in `buf` -- of course, when creating the status
    // prompt, we already broke the yank and undo sequence in the _original_ buf.
    undo_killring_handled ret = note_backout_action(state, buf);

    return ret;
}

undo_killring_handled delete_backward_word(state *state, ui_window_ctx *ui, buffer *buf) {
    size_t d = backward_word_distance(buf);
    delete_result delres = delete_left(ui, buf, d);
    record_yank(&state->clipboard, delres.deletedText, yank_side::left);
    state->note_error_message(std::move(delres.error_message));
    note_undo(buf, std::move(delres));
    return handled_undo_killring(state, buf);
}

undo_killring_handled delete_forward_word(state *state, ui_window_ctx *ui, buffer *buf) {
    size_t d = forward_word_distance(buf);
    delete_result delres = delete_right(ui, buf, d);
    record_yank(&state->clipboard, delres.deletedText, yank_side::right);
    state->note_error_message(std::move(delres.error_message));
    note_undo(buf, std::move(delres));
    return handled_undo_killring(state, buf);
}

undo_killring_handled kill_line(state *state, ui_window_ctx *ui, buffer *buf) {
    size_t eolDistance = distance_to_eol(*buf, buf->cursor());

    delete_result delres;
    if (eolDistance == 0 && buf->cursor() < buf->size()) {
        delres = delete_right(ui, buf, 1);
    } else {
        delres = delete_right(ui, buf, eolDistance);
    }
    record_yank(&state->clipboard, delres.deletedText, yank_side::right);
    state->note_error_message(std::move(delres.error_message));
    note_undo(buf, std::move(delres));
    return handled_undo_killring(state, buf);
}

undo_killring_handled kill_region(state *state, ui_window_ctx *ui, buffer *buf) {
    if (!buf->mark.has_value()) {
        // (We do NOT want no_yank here.)  We do want to disrupt the undo action chain (if only because Emacs does that).
        note_nop_undo(buf);
        state->note_error_message("No mark set");  // TODO: UI logic
        return handled_undo_killring(state, buf);
    }
    size_t mark = buf->get_mark_offset(*buf->mark);
    size_t cursor = buf->cursor();
    if (mark > cursor) {
        delete_result delres = delete_right(ui, buf, mark - cursor);
        record_yank(&state->clipboard, delres.deletedText, yank_side::right);
        note_undo(buf, std::move(delres));
        return handled_undo_killring(state, buf);
    } else if (mark < cursor) {
        delete_result delres = delete_left(ui, buf, cursor - mark);
        record_yank(&state->clipboard, delres.deletedText, yank_side::left);
        note_undo(buf, std::move(delres));
        return handled_undo_killring(state, buf);
    } else {
        // We actually do want to yank, and combine yanks with successive yanks.  Right or
        // left yank side doesn't matter except for string concatenation efficiency.  Note
        // that we can't "do nothing" -- if state->clipboard.justRecorded is false, we
        // need to create an empty string clipboard entry.  That's what this record_yank
        // call does.
        record_yank(&state->clipboard, buffer_string{}, yank_side::right);
        note_nop_undo(buf);
        return handled_undo_killring(state, buf);
    }
}

undo_killring_handled copy_region(state *state, buffer *buf) {
    if (!buf->mark.has_value()) {
        // (We do NOT want no_yank here.)  We do want to disrupt the undo action chain (if only because Emacs does that).
        note_nop_undo(buf);
        state->note_error_message("No mark set");  // TODO: UI logic, and duplicated string
        return handled_undo_killring(state, buf);
    }
    size_t mark = buf->get_mark_offset(*buf->mark);
    size_t cursor = buf->cursor();
    size_t region_beg = std::min(mark, cursor);
    size_t region_end = std::max(mark, cursor);

    note_nop_undo(buf);
    record_yank(&state->clipboard, buf->copy_substr(region_beg, region_end), yank_side::none);
    return handled_undo_killring(state, buf);
}

void rotate_to_buffer(state *state, ui_window *win, buffer_id buf_id) {
    note_navigate_away_from_buf(win, state->lookup(buf_id));

    // (If the buf isn't part of the ui_window's buf list, this puts it in a specific
    // place.)
    win->point_at(buf_id, state);
}

prompt file_open_prompt(buffer_id promptBufId) {
    // TODO: UI logic
    return {prompt::type::proc, buffer(promptBufId), "file to open: ",
        [](state *state, buffer&& promptBuf, bool *) {
            // killring important, undo not because we're destructing the status_prompt buf.
            undo_killring_handled ret = note_backout_action(state, &promptBuf);
            std::string text = promptBuf.copy_to_string();

            if (text != "") {
                // TODO: Handle error!
                buffer buf = open_file_into_detached_buffer(state, text);

                buffer_id buf_id = buf.id;
                state->buf_set.emplace(buf_id, std::make_unique<buffer>(std::move(buf)));
                apply_number_to_buf(state, buf_id);
                state->active_window()->point_at(buf_id, state);

            } else {
                state->note_error_message("No filename given");
            }

            return ret;
        }};
}

undo_killring_handled open_file_action(state *state, buffer *active_buf) {
    undo_killring_handled ret = note_navigation_action(state, active_buf);
    if (state->status_prompt.has_value()) {
        return ret;
    }

    state->status_prompt = file_open_prompt(state->gen_buf_id());
    return ret;
}

void save_buf_to_married_file_and_mark_unmodified(buffer *buf) {
    // TODO: Display that save succeeded, somehow.
    logic_check(buf->married_file.has_value(), "save_buf_to_married_file with unmarried buf");
    std::ofstream fstream(*buf->married_file, std::ios::binary | std::ios::trunc);
    // TODO: Write a temporary file and rename it.  Use pwrite.  Etc.
    fstream.write(as_chars(buf->bef_.data()), buf->bef_.size());
    fstream.write(as_chars(buf->aft_.data()), buf->aft_.size());
    fstream.close();
    // TODO: Better error handling
    runtime_check(!fstream.fail(), "error writing to file %s", buf->married_file->c_str());

    buf->non_modified_undo_node = buf->undo_info.current_node;
}

std::string buf_name_from_file_path(const fs::path& path) {
    return path.filename().string();
}

prompt file_save_prompt(buffer_id promptBufId) {
    return {prompt::type::proc, buffer(promptBufId), "file to save: ",
        [](state *state, buffer&& promptBuf, bool *) {
            // killring important, undo not because we're destructing the status_prompt buf.
            undo_killring_handled ret = note_backout_action(state, &promptBuf);
            // TODO: Of course, handle errors, such as if directory doesn't exist, permissions.
            std::string text = promptBuf.copy_to_string();
            if (text != "") {
                buffer_id buf_id = state->active_window()->active_buf().first;
                buffer *buf = state->lookup(buf_id);
                buf->married_file = text;
                save_buf_to_married_file_and_mark_unmodified(buf);
                buf->name_str = buf_name_from_file_path(fs::path(text));
                buf->name_number = 0;
                apply_number_to_buf(state, buf_id);
            } else {
                state->note_error_message("No filename given");  // TODO: UI logic
            }
            return ret;
        }};
}

undo_killring_handled save_file_action(state *state, buffer *active_buf) {
    // Specifically, I don't want to break the undo chain here.
    undo_killring_handled ret = note_noundo_killring_action(state, active_buf);
    if (state->status_prompt.has_value()) {
        state->note_error_message("Cannot save file when prompt is active");  // TODO: UI logic
        // Ignore keypress.
        return ret;
    }

    if (active_buf->married_file.has_value()) {
        save_buf_to_married_file_and_mark_unmodified(active_buf);
    } else {
        state->status_prompt = file_save_prompt(state->gen_buf_id());
    }
    return ret;
}

undo_killring_handled save_as_file_action(state *state, buffer *active_buf) {
    // Specifically, I don't want to break the undo chain here.
    undo_killring_handled ret = note_noundo_killring_action(state, active_buf);
    if (state->status_prompt.has_value()) {
        state->note_error_message("Cannot save file when prompt is active");  // TODO: UI logic, duplicated string

        // Ignore keypress.
        return ret;
    }

    state->status_prompt = file_save_prompt(state->gen_buf_id());
    return ret;
}

std::vector<std::string> modified_buffers(const state *state) {
    std::vector<std::string> ret;
    for (const auto& elem : state->buf_set) {
        if (elem.second->modified_flag()) {
            // O(n^2)
            ret.push_back(buffer_name_str(state, elem.first));
        }
    }
    return ret;
}

// TODO: Make a special fn for yes/no prompts (used also by buffer_close_prompt)?
prompt exit_without_save_prompt(std::vector<std::string>&& bufnames, buffer&& initialBuf) {
    // TODO: UI logic
    std::string messageText = "exit without saving? (" + string_join(", ", bufnames) + ") (yes/no): ";
    return {prompt::type::proc, std::move(initialBuf), std::move(messageText),
        // TODO: Make a macro for std::move into capture list.
        [bufnames = std::move(bufnames)](state *state, buffer&& promptBuf, bool *exit_loop) mutable {
            // killring important, undo not because we're destructing the status_prompt buf.
            undo_killring_handled ret = note_backout_action(state, &promptBuf);
            std::string text = promptBuf.copy_to_string();
            // TODO: Implement displaying errors to the user.
            if (text == "yes") {
                // Yes, exit without saving.
                *exit_loop = true;
                return ret;
            } else if (text == "no") {
                // No, don't exit without saving.
                return ret;
            } else {
                state->note_error_message("Please type yes or no");
                state->status_prompt = exit_without_save_prompt(std::move(bufnames), std::move(promptBuf));
                return ret;
            }
        }};
}

undo_killring_handled exit_cleanly(state *state, buffer *active_buf, bool *exit_loop) {
    undo_killring_handled ret = note_backout_action(state, active_buf);

    if (state->status_prompt.has_value()) {
        close_status_prompt(state);
    }

    std::vector<std::string> bufnames = modified_buffers(state);
    if (!bufnames.empty()) {
        state->status_prompt = exit_without_save_prompt(std::move(bufnames), buffer(state->gen_buf_id()));
    } else {
        *exit_loop = true;
    }
    return ret;
}

prompt buffer_switch_prompt(buffer_id promptBufId, buffer_string&& data) {
    // TODO: UI logic
    return {prompt::type::proc, buffer::from_data(promptBufId, std::move(data)), "switch to buffer: ",
        [](state *state, buffer&& promptBuf, bool *) {
            // killring important, undo not because we're destructing the status_prompt buf.
            undo_killring_handled ret = note_backout_action(state, &promptBuf);
            std::string text = promptBuf.copy_to_string();
            if (text != "") {
                ui_window *win = state->active_window();
                buffer_id buf_id;
                if (find_buffer_by_name(state, text, &buf_id)) {
                    rotate_to_buffer(state, win, buf_id);
                } else {
                    state->note_error_message("Buffer not found");
                }
            } else {
                state->note_error_message("No buffer name given");
            }

            return ret;
        }};
}

// TODO: When we switch to a buffer, or rotate to one, we should render the buffer with the cursor on screen.  No?  Well, if it's a *Messages* buffer... Hmmm.
undo_killring_handled buffer_switch_action(state *state, buffer *active_buf) {
    undo_killring_handled ret = note_navigation_action(state, active_buf);
    if (state->status_prompt.has_value()) {
        // TODO: We'll have to handle M-x C-s or C-x C-s somehow -- probably by generic
        // logic at the keypress level.

        // Ignore keypress.
        return ret;
    }

    buffer_string data = buffer_name(state, active_buf->id);
    state->status_prompt = buffer_switch_prompt(state->gen_buf_id(), std::move(data));
    return ret;
}

// Caller needs to call set_window on the buf, generally, or other ui-specific stuff.
buffer open_file_into_detached_buffer(state *state, const std::string& dirty_path) {
    fs::path path = dirty_path;
    buffer_string data = read_file(path);
    std::string name = buf_name_from_file_path(path);

    buffer ret(state->gen_buf_id());
    ret.name_str = std::move(name);
    ret.married_file = path.string();
    ret.aft_ = std::move(data);
    ret.aft_stats_ = compute_stats(ret.aft_);
    return ret;
}

void apply_number_to_buf(state *state, buffer_id buf_id) {
    buffer *the_buf = state->lookup(buf_id);
    const std::string& name = the_buf->name_str;
    std::unordered_set<uint64_t> numbers;
    for (auto& elem : state->buf_set) {
        if (elem.first != buf_id && elem.second->name_str == name) {
            auto res = numbers.insert(elem.second->name_number);
            logic_check(res.second,
                        "insert_with_name_number_into_buflist seeing bufs with duplicate numbers, name = %s",
                        the_buf->name_str.c_str());
        }
    }

    // TODO: Consider making the numbers start at 1.  So name_number=0 means a number has
    // not been applied.  (Aside from UI differences, the purpose is, by making zero a
    // special value, to make the buf never have an invalid, conflicting number upon
    // mutations of the state.)
    uint64_t n = 0;
    while (numbers.count(n) == 1) {
        ++n;
    }
    the_buf->name_number = n;
}

buffer scratch_buffer(buffer_id id) {
    buffer ret(id);
    ret.name_str = "*scratch*";
    ret.name_number = 0;
    return ret;
}

undo_killring_handled enter_handle_status_prompt(state *state, bool *exit_loop) {
    switch (state->status_prompt->typ) {
    case prompt::type::proc: {
        // TODO: Maybe every branch of this function should start with these lines, and
        // have to re-initiate the prompt later.
        do_close_status_prompt(&*state->status_prompt);
        prompt status_prompt = std::move(*state->status_prompt);
        state->status_prompt = std::nullopt;

        return (status_prompt.procedure)(state, std::move(status_prompt.buf), exit_loop);
    } break;
    default:
        logic_fail("status prompt unreachable default case");
        break;
    }
}

undo_killring_handled rotate_buf_right(state *state, buffer *active_buf) {
    undo_killring_handled ret = note_navigation_action(state, active_buf);
    if (!state->is_normal()) {
        return ret;
    }

    ui_window *win = state->active_window();
    note_navigate_away_from_buf(win, active_buf);

    logic_checkg(win->active_tab.value < win->window_ctxs.size());
    size_t val = win->active_tab.value;
    val += 1;
    if (val == win->window_ctxs.size()) {
        val = 0;
    }
    win->active_tab.value = val;

    return ret;
}

undo_killring_handled rotate_buf_left(state *state, buffer *active_buf) {
    undo_killring_handled ret = note_navigation_action(state, active_buf);
    if (!state->is_normal()) {
        return ret;
    }

    ui_window *win = state->active_window();
    note_navigate_away_from_buf(win, active_buf);

    logic_checkg(win->active_tab.value < win->window_ctxs.size());
    size_t val = win->active_tab.value;
    if (val == 0) {
        val = win->window_ctxs.size();
    }
    val -= 1;
    win->active_tab.value = val;

    return ret;
}

undo_killring_handled yank_from_clipboard(state *state, ui_window_ctx *ui, buffer *buf) {
    std::optional<const buffer_string *> text = do_yank(&state->clipboard);
    if (text.has_value()) {
        insert_result res = insert_chars(ui, buf, (*text)->data(), (*text)->size());
        note_undo(buf, std::move(res));
        return handled_undo_killring(state, buf);
    } else {
        note_nop_undo(buf);
        state->note_error_message("Killring is empty");  // TODO: UI logic (and Emacs verbiage)
        return handled_undo_killring(state, buf);
    }
    // Note that this gets called directly by C-y and by alt_yank_from_clipboard as a
    // helper.  Possibly false-DRY (someday).
}

undo_killring_handled alt_yank_from_clipboard(state *state, ui_window_ctx *ui, buffer *buf) {
    if (state->clipboard.justYanked.has_value()) {
        // TODO: this code will be wrong with undo impled -- the deletion and insertion should be a single undo chunk -- not a problem here but is this a bug in jsmacs?
        size_t amount_to_delete = *state->clipboard.justYanked;
        state->clipboard.stepPasteNumber();
        std::optional<const buffer_string *> text = do_yank(&state->clipboard);
        logic_check(text.has_value(), "with justYanked non-null, do_yank returns null");

        delete_result delres = delete_left(ui, buf, amount_to_delete);
        insert_result insres = insert_chars(ui, buf, (*text)->data(), (*text)->size());

        // Add the reverse action to undo history.
        atomic_undo_item item = {
            .beg = insres.new_cursor,
            .text_inserted = std::move(delres.deletedText),
            .text_deleted = std::move(insres.insertedText),
            .side = Side::left,
            .before_node = buf->undo_info.unused_node_number(),
            .after_node = buf->undo_info.current_node,
        };
        add_edit(&buf->undo_info, std::move(item));
        return handled_undo_killring(state, buf);
    } else {
        state->note_error_message("Previous command was not a yank");  // TODO: UI logic (and Emacs verbiage)
        note_nop_undo(buf);
        return handled_undo_killring(state, buf);
    }
}

void window_column(const window_layout *layout, window_number winnum,
                   size_t *col_num_out, size_t *col_begin_out, size_t *col_end_out) {
    size_t k = 0;
    for (size_t i = 0; i < layout->column_datas.size(); ++i) {
        const window_layout::col_data& cd = layout->column_datas[i];
        size_t next_k = k + cd.num_rows;
        if (winnum.value < next_k) {
            *col_num_out = i;
            *col_begin_out = k;
            *col_end_out = next_k;
            return;
        }
        k = next_k;
    }
    layout->sanity_check();
    logic_fail("window_column failure despite layout sanity check");
}

void renormalize_column(window_layout *layout, size_t col_num, size_t col_begin, size_t col_end) {
    logic_checkg(col_begin <= col_num);
    logic_checkg(col_num < col_end);
    logic_checkg(col_end <= layout->row_relsizes.size());

    // TODO: Actually, this whole code is duplicated -- we could make a function returning
    // true_sizes from window_layout and col_-params.
    const uint32_t divider_size = 0;  // TODO: Duplicated constant with rendering logic.
    std::vector<uint32_t> true_sizes = true_split_sizes<uint32_t>(
        layout->last_rendered_terminal_size.rows,
        divider_size,
        layout->row_relsizes.begin() + col_begin,
        layout->row_relsizes.begin() + col_end,
        [](const uint32_t& elem) { return elem; });

    std::copy(true_sizes.begin(), true_sizes.end(), layout->row_relsizes.begin() + col_begin);
}

void renormalize_column_widths(window_layout *layout) {
    const uint32_t column_divider_size = 1;  // TODO: Duplicated constant with rendering logic.
    std::vector<uint32_t> true_sizes
        = true_split_sizes<window_layout::col_data>(
            layout->last_rendered_terminal_size.cols, column_divider_size,
            layout->column_datas.begin(),
            layout->column_datas.end(),
            [](const window_layout::col_data& cd) { return cd.relsize; });

    for (size_t i = 0; i < true_sizes.size(); ++i) {
        layout->column_datas[i].relsize = true_sizes[i];
    }
}

// Does _everything_ about duplicating a window except it gives it a blank row size, and
// doesn't adjust column_datas alignment.
//
// So, window_layout is temporarily in an invalid state.  (Hence the term "help.")
void help_duplicate_window(state *state, window_number duplicee, size_t insertion_point) {
    // (insertion_point isn't quite a window_number because it's an offset in [0,
    // layout->windows.size()].)

    const auto& ab = state->layout.windows.at(duplicee.value).active_buf();
    buffer_id buf_id = ab.first;
    buffer *buf = state->lookup(buf_id);
    size_t fvo = buf->get_mark_offset(ab.second->first_visible_offset);
    ui_window window{state->layout.gen_next_window_id()};
    ui_window_ctx *ctx = window.point_at(buf_id, state);
    buf->replace_mark(ctx->first_visible_offset, fvo);
    state->layout.windows.insert(state->layout.windows.begin() + insertion_point,
                                 std::move(window));
    state->layout.row_relsizes.insert(state->layout.row_relsizes.begin() + insertion_point,
                                      0);
}

undo_killring_handled split_horizontally(state *state, buffer *active_buf) {
    undo_killring_handled ret = note_navigation_action(state, active_buf);
    if (!state->is_normal()) {
        return ret;
    }

    const window_number active_winnum = state->layout.active_window;

    size_t col_num, col_begin, col_end;
    window_column(&state->layout, active_winnum, &col_num, &col_begin, &col_end);

    renormalize_column(&state->layout, col_num, col_begin, col_end);
    uint32_t active_window_height = state->layout.row_relsizes.at(active_winnum.value);
    uint32_t new_window_height = active_window_height / 2;  // this one gets rounded down.
    uint32_t new_active_window_height = active_window_height - new_window_height;

    if (new_window_height == 0) {
        state->layout.sanity_check();
        state->note_error_message("Window would be too short");  // TODO: UI logic
        return ret;
    }
    logic_checkg(new_active_window_height != 0);  // Provably true because it's >= new_window_height.

    help_duplicate_window(state, active_winnum, active_winnum.value + 1);
    ++state->layout.column_datas[col_num].num_rows;
    state->layout.row_relsizes[active_winnum.value] = new_active_window_height;
    state->layout.row_relsizes[active_winnum.value + 1] = new_window_height;

    state->layout.sanity_check();
    return ret;
}

undo_killring_handled split_vertically(state *state, buffer *active_buf) {
    undo_killring_handled ret = note_navigation_action(state, active_buf);
    if (!state->is_normal()) {
        return ret;
    }

    const window_number active_winnum = state->layout.active_window;

    renormalize_column_widths(&state->layout);

    size_t col_num, col_begin, col_end;
    window_column(&state->layout, active_winnum, &col_num, &col_begin, &col_end);

    const uint32_t column_divider_size = 1;  // TODO: Duplicated constant with rendering logic.

    uint32_t active_column_width = state->layout.column_datas[col_num].relsize;
    uint32_t cols_after_divider = active_column_width - column_divider_size;
    uint32_t new_column_width = cols_after_divider / 2;
    uint32_t new_active_column_width = cols_after_divider - new_column_width;

    // TODO: Kind of gross, assumes column_divider_size is 1.
    if (active_column_width == 0 || cols_after_divider == 0 || new_column_width == 0) {
        state->layout.sanity_check();
        state->note_error_message("Window would be too narrow");  // TODO: UI logic
        return ret;
    }
    logic_checkg(new_active_column_width != 0);  // Provably true because it's >= new_column_width.

    help_duplicate_window(state, active_winnum, col_end);
    state->layout.row_relsizes[col_end] = state->layout.last_rendered_terminal_size.rows;
    state->layout.column_datas.insert(state->layout.column_datas.begin() + col_num + 1,
                                      {.relsize = new_column_width, .num_rows = 1});
    state->layout.column_datas[col_num].relsize = new_active_column_width;

    state->layout.sanity_check();
    return ret;
}

undo_killring_handled grow_window_size(state *state, buffer *active_buf, ortho_direction direction) {
    undo_killring_handled ret = note_navigation_action(state, active_buf);
    if (!state->is_normal()) {
        return ret;
    }


  
    (void)state, (void)direction;
    return unimplemented_keypress();
}

undo_killring_handled switch_to_next_window_action(state *state, buffer *active_buf) {
    undo_killring_handled ret = note_navigation_action(state, active_buf);
    if (!state->is_normal()) {
        return ret;
    }

    logic_checkg(state->layout.windows.size() > 0);
    if (state->layout.windows.size() == 1) {
        state->note_error_message("No other window to select");  // TODO: UI logic
        return ret;
    }

    note_navigate_away_from_buf(&state->layout.windows.at(state->layout.active_window.value), active_buf);
    size_t winnum = state->layout.active_window.value;
    winnum += 1;
    if (winnum == state->layout.windows.size()) {
        winnum = 0;
    }
    state->layout.active_window = { winnum };

    return ret;
}

undo_killring_handled switch_to_window_number_action(state *state, buffer *active_buf, int number) {
    logic_checkg(0 < number && number < 10);
    size_t winnum = number - 1;

    undo_killring_handled ret = note_navigation_action(state, active_buf);
    if (!state->is_normal()) {
        return ret;
    }

    if (winnum >= state->layout.windows.size()) {
        state->note_error_message("Window number " + std::to_string(number) + " is out of range");
        return ret;
    }
    if (winnum == state->layout.active_window.value) {
        state->note_error_message("Window " + std::to_string(number) + " is already selected");
        return ret;
    }

    note_navigate_away_from_buf(&state->layout.windows.at(state->layout.active_window.value), active_buf);

    state->layout.active_window.value = winnum;

    return ret;
}

undo_killring_handled help_menu(state *state) {
    buffer buf(state->gen_buf_id(), to_buffer_string(
        "Help:\n"
        "C-c exit\n"
        "M-h help\n"
        "C-s save\n"
        "M-s save as...\n"
        "F5/F6 switch buffers left/right\n"
        "F7 switch buffer by name\n"
        "M-f/M-b forward/backward word\n"
        "C-w cut (or append to cut)\n"
        "M-w copy\n"
        "C-y paste\n"
        "M-y (immediately after C-y) paste next in killring\n"
        "C-k kill line (and create/append to killring entry)\n"
        "\n"
        " = Window Management =\n"
        "C-x 2 split window horizontally\n"
        "C-x 3 split window vertically\n"
        "C-x <arrow key> grow current window size (in direction)\n"));
    state->popup_display = popup{
        std::move(buf),
    };
    return note_nop_action(state);
}

}  // namespace qwi
