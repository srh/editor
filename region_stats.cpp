#include "region_stats.hpp"

#include "error.hpp"

namespace qwi {

// Belongs in util.cpp, maybe.
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

region_stats compute_stats(const buffer_char *data, size_t count) {
    size_t beginning_of_line = find_after_last(data, count, buffer_char{'\n'});

    size_t newline_count = 0;
    for (size_t i = 0; i < count; ++i) {
        newline_count += (data[i] == buffer_char{'\n'});
    }

    return {
        .newline_count = newline_count,
        .last_line_size = count - beginning_of_line,
    };
}

region_stats subtract_stats_right(const region_stats& stats, const buffer_char *data, size_t new_count, size_t count) {
    logic_checkg(new_count <= count);

    size_t removed_newlines = 0;
    for (size_t i = new_count; i < count; ++i) {
        if (data[i] == buffer_char{'\n'}) {
            removed_newlines += 1;
        }
    }
    if (removed_newlines == 0) {
        return {
            .newline_count = stats.newline_count,
            .last_line_size = stats.last_line_size - (count - new_count),
        };
    } else {
        size_t beginning_of_line = find_after_last(data, new_count, buffer_char{'\n'});
        return {
            .newline_count = stats.newline_count - removed_newlines,
            .last_line_size = new_count - beginning_of_line,
        };
    }
}

region_stats subtract_stats_left(const region_stats& stats, const region_stats& removed_stats) {
    logic_checkg(removed_stats.newline_count <= stats.newline_count);
    size_t new_newlines = stats.newline_count - removed_stats.newline_count;
    return {
        .newline_count = new_newlines,
        .last_line_size = stats.last_line_size - (new_newlines == 0 ? removed_stats.last_line_size : 0),
    };
}

}  // namespace qwi
