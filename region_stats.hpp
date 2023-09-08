#ifndef QWERTILLION_REGIONSTATS_HPP_
#define QWERTILLION_REGIONSTATS_HPP_

#include "chars.hpp"

namespace qwi {

struct region_stats {
    // Given two strings x and y, we can efficiently compute region_stats of (x + y) from
    // region_stats of x and y respectively.  Note that this assumes the tab width is
    // consistently TAB_WIDTH -- column numbers would, of course, have to be recomputed if
    // tab width dynamically changes.

    // The zero value of this type happens to be the region_stats computed on the empty string.

    size_t newline_count = 0;

    // The size in columns of the last line -- as defined by compute_char_rendering.
    size_t last_line_size = 0;

    // If newline_count_ == 0 and the line has tab characters, this is a value from 1 to
    // TAB_WIDTH, the size of the first tab character in the string.  Otherwise, this
    // value is 0.
    size_t first_tab_size = 0;
};

region_stats append_stats(const region_stats& left, const region_stats& right);

region_stats compute_stats(const buffer_char *data, size_t count);

inline region_stats compute_stats(const buffer_string& str) {
    return compute_stats(str.data(), str.size());
}

// Computes stats after we delete data at the right side of the region.
region_stats subtract_stats_right(const region_stats& stats, const buffer_char *data, size_t new_count, size_t count);

// Computes stats after we delete data (with stats `removed_stats`) at the left side of the region.
// [data, data + new_count) is the buffer _after_ subtracting stats.
region_stats subtract_stats_left(const region_stats& stats, const region_stats& removed_stats,
                                 const buffer_char *data, size_t new_count);

}  // namespace qwi

#endif  // QWERTILLION_REGIONSTATS_HPP_
