#include "state.hpp"

#include "arith.hpp"
#include "buffer.hpp"  // for insert_result and delete_result in undo logic
#include "error.hpp"
// TODO: We don't want this dependency exactly -- we kind of want ui info to be separate from state.
#include "terminal.hpp"
#include "term_ui.hpp"

namespace qwi {

size_t buffer::cursor_distance_to_beginning_of_line() const {
    size_t ix = bef.find_last_of(buffer_char{'\n'});
    // this works in the std::string::npos case too
    return bef.size() - ix - 1;
}

void buffer::set_cursor(size_t pos) {
    if (pos < bef.size()) {
        aft.insert(aft.begin(), bef.begin() + pos, bef.end());
        bef.resize(pos);
    } else {
        size_t aft_pos = pos - bef.size();
        logic_check(aft_pos <= aft.size(), "set_cursor outside buf range");
        bef.append(aft.data(), aft_pos);
        aft.erase(0, aft_pos);
    }
}

std::string buffer::copy_to_string() const {
    std::string ret;
    ret.reserve(bef.size() + aft.size());
    ret.append(as_chars(bef.data()), bef.size());
    ret.append(as_chars(aft.data()), aft.size());
    return ret;
}

buffer_string buffer::copy_substr(size_t beg, size_t end) const {
    logic_check(beg <= end && end <= size(), "buffer::copy_substr requires valid range, got [%zu, %zu) with size %zu",
                beg, end, size());
    buffer_string ret;
    ret.reserve(end - beg);
    if (end <= bef.size()) {
        ret = bef.substr(beg, end - beg);
    } else if (beg < bef.size()) {
        ret = bef.substr(beg, bef.size() - beg) + aft.substr(0, end - bef.size());
    } else {
        ret = aft.substr(beg - bef.size(), end - beg);
    }
    return ret;
}

buffer buffer::from_data(buffer_string&& data) {
    buffer ret;
    ret.bef = std::move(data);
    return ret;
}

std::string buffer_name_str(const state *state, buffer_number buf_number) {
    // TODO: Eventually, make this not be O(n), or even, make this not allocate.
    const buffer *buf = buffer_ptr(state, buf_number);

    bool seen = false;
    for (size_t i = 0, e = state->buflist.size(); i < e; ++i) {
        if (i != buf_number.value && state->buflist[i].name_str == buf->name_str) {
            seen = true;
            break;
        }
    }

    if (seen) {
        return buf->name_str + "<" + std::to_string(buf->name_number) + ">";
    } else {
        return buf->name_str;
    }
}

buffer_string buffer_name(const state *state, buffer_number buf_number) {
    return to_buffer_string(buffer_name_str(state, buf_number));
}

size_t distance_to_eol(const buffer& buf, size_t pos) {
    size_t p = pos;
    for (size_t e = buf.size(); p < e; ++p) {
        if (buf.get(p) == buffer_char{'\n'}) {
            break;
        }
    }
    return p - pos;
}

size_t distance_to_beginning_of_line(const buffer& buf, size_t pos) {
    logic_check(pos <= buf.size(), "distance_to_beginning_of_line with out of range pos");
    size_t p = pos;
    for (;;) {
        if (p == 0) {
            return pos;
        }
        --p;
        if (buf.get(p) == buffer_char{'\n'}) {
            return pos - (p + 1);
        }
    }
}

window_size main_buf_window_from_terminal_window(const terminal_size& term_window) {
    return {
        .rows = std::max(STATUS_AREA_HEIGHT, term_window.rows) - STATUS_AREA_HEIGHT,
        .cols = term_window.cols,
    };
}

void resize_window(state *st, const terminal_size& new_window) {
    window_size buf_window = main_buf_window_from_terminal_window(new_window);
    for (buffer& buf : st->buflist) {
        resize_buf_window(&buf, buf_window);
    }

    // TODO: Resize prompt window.
}

void record_yank(clip_board *clb, const buffer_string& deletedText, yank_side side) {
    if (clb->justRecorded) {
        runtime_check(!clb->clips.empty(), "justRecorded true, clips empty");
        switch (side) {
        case yank_side::left:
            clb->clips.back() = deletedText + clb->clips.back();
            break;
        case yank_side::right:
            clb->clips.back() += deletedText;
            break;
        case yank_side::none:
            clb->clips.push_back(deletedText);
            break;
        }
    } else {
        clb->clips.push_back(deletedText);
    }
    switch (side) {
    case yank_side::left:
        clb->justRecorded = true;
        break;
    case yank_side::right:
        clb->justRecorded = true;
        break;
    case yank_side::none:
        clb->justRecorded = false;
        break;
    }
    clb->pasteNumber = 0;
    clb->justYanked = std::nullopt;
}

std::optional<const buffer_string *> do_yank(clip_board *clb) {
    clb->justRecorded = false;
    if (clb->clips.empty()) {
        clb->justYanked = 0;
        return std::nullopt;
    }
    size_t sz = clb->clips.size();
    buffer_string *str = &clb->clips.at(sz - 1 - clb->pasteNumber % sz);
    clb->justYanked = str->size();
    return std::make_optional(str);
}

void no_yank(clip_board *clb) {
    clb->justRecorded = false;
    clb->justYanked = std::nullopt;
}

}  // namespace qwi
