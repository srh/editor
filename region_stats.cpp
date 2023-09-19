#include "region_stats.hpp"

#include "error.hpp"
#include "term_ui.hpp"  // for compute_char_rendering and char_rendering

namespace qwi {

// Belongs in util.cpp, maybe.
// TODO: Make distance_to_beginning_of_line use this.  (Later.  It would depend on buffer's data structure.)
size_t find_after_last(const buffer_char *data, size_t count, buffer_char ch) {
    size_t i = count - 1;
    // Playing games with underflow.
    for (; i != size_t(-1); --i) {
        if (data[i] == ch) {
            break;
        }
    }
    return i + 1;
}

region_stats append_stats(const region_stats& left, const region_stats& right) {
    size_t newline_count = left.newline_count + right.newline_count;
    size_t last_line_size = right.last_line_size;
    size_t first_tab_size = 0;
    if (right.newline_count == 0) {
        last_line_size += left.last_line_size;  // still need to adjust for tab offset
        if (right.first_tab_size != 0) {
            logic_checkg(right.first_tab_size <= TAB_WIDTH);
            // E.g., 8 - 7 = 1, 8 - 6 = 2, 8 - 8 = 0.
            size_t tab_offset = TAB_WIDTH - right.first_tab_size;
            // Happy to overflow intermediate value.
            size_t adjusted_tab_offset = (left.last_line_size + tab_offset) & TAB_MOD_MASK;
            size_t adjusted_tab_size = TAB_WIDTH - adjusted_tab_offset;

            last_line_size += adjusted_tab_size - right.first_tab_size;
            if (left.first_tab_size != 0) {
                first_tab_size = left.first_tab_size;
            } else {
                first_tab_size = newline_count == 0 ? adjusted_tab_size : 0;
            }
        } else {
            // no adjustments to last_line_size.  Whether left.first_tab_size is zero or
            // not, it's the correct value to use.
            first_tab_size = left.first_tab_size;
        }
    }

    region_stats ret = {
        .newline_count = newline_count,
        .last_line_size = last_line_size,
        .first_tab_size = first_tab_size,
    };
    return ret;
}

void compute_line_stats(const buffer_char *data, size_t count,
                        size_t *last_line_size_out, size_t *first_tab_size_out) {
    size_t line_col = 0;
    bool saw_newline = false;
    size_t first_tab_size = 0;
    for (size_t i = 0; i < count; ++i) {
        buffer_char ch = data[i];
        char_rendering rend = compute_char_rendering(ch, &line_col);
        logic_check(rend.count != SIZE_MAX, "compute_line_stats seeing a newline");
        saw_newline |= (rend.count == SIZE_MAX);
        if (ch == buffer_char{'\t'} && first_tab_size == 0) {
            first_tab_size = rend.count;
        }
    }
    logic_check(!saw_newline, "compute_line_stats saw a newline");
    *last_line_size_out = line_col;
    *first_tab_size_out = first_tab_size;
}

region_stats compute_stats(const buffer_char *data, size_t count) {
    size_t beginning_of_line = find_after_last(data, count, buffer_char{'\n'});

    size_t newline_count = 0;
    for (size_t i = 0; i < beginning_of_line; ++i) {
        newline_count += (data[i] == buffer_char{'\n'});
    }

    size_t last_line_size;
    size_t first_tab_size;
    compute_line_stats(data + beginning_of_line, count - beginning_of_line,
                       &last_line_size, &first_tab_size);

    return {
        .newline_count = newline_count,
        .last_line_size = last_line_size,
        .first_tab_size = first_tab_size,
    };
}

region_stats subtract_stats_right(const region_stats& stats, const buffer_char *data, size_t new_count, size_t count) {
    logic_checkg(new_count <= count);

    size_t removed_newlines = 0;
    bool saw_tab = false;
    for (size_t i = new_count; i < count; ++i) {
        buffer_char ch = data[i];
        removed_newlines += (ch == buffer_char{'\n'});
        saw_tab |= (ch == buffer_char{'\t'});
    }

    if (removed_newlines == 0 && !saw_tab) {
        // Incrementally walking stats backward is possible except when we encounter a tab
        // character or newline.  For now we refrain from making the code more complicated.

        size_t line_col = 0;
        for (size_t i = new_count; i < count; ++i) {
            buffer_char ch = data[i];
            char_rendering rend = compute_char_rendering(ch, &line_col);
            (void)rend;
        }

        return region_stats{
            .newline_count = stats.newline_count,
            .last_line_size = stats.last_line_size - line_col,
            .first_tab_size = stats.first_tab_size,
        };
    }

    size_t beginning_of_line = find_after_last(data, new_count, buffer_char{'\n'});
    size_t last_line_size;
    size_t first_tab_size;
    compute_line_stats(data + beginning_of_line, new_count - beginning_of_line,
                       &last_line_size, &first_tab_size);

    return region_stats{
        .newline_count = stats.newline_count - removed_newlines,
        .last_line_size = last_line_size,
        .first_tab_size = first_tab_size,
    };
}

region_stats subtract_stats_left(const region_stats& stats, const region_stats& removed_stats,
                                 const buffer_char *data, size_t new_count) {
    logic_checkg(removed_stats.newline_count <= stats.newline_count);
    size_t new_newlines = stats.newline_count - removed_stats.newline_count;
    if (new_newlines == 0) {
        size_t last_line_size;
        size_t first_tab_size;
        compute_line_stats(data, new_count, &last_line_size, &first_tab_size);
        return {
            .newline_count = new_newlines,
            .last_line_size = last_line_size,
            .first_tab_size = first_tab_size,
        };
    } else {
        return {
            .newline_count = new_newlines,
            .last_line_size = stats.last_line_size,
            .first_tab_size = 0,
        };
    };
}

}  // namespace qwi
