#ifndef QWERTILLION_REGIONSTATS_HPP_
#define QWERTILLION_REGIONSTATS_HPP_

#include "chars.hpp"

namespace qwi {

struct region_stats {
    // Given two strings x and y, we can efficiently compute region_stats of (x + y) from
    // region_stats of x and y respectively.

    size_t newline_count = 0;
    size_t last_line_size = 0;
};

inline region_stats append_stats(const region_stats& left, const region_stats& right) {
    region_stats ret = {
        .newline_count = left.newline_count + right.newline_count,
        .last_line_size = right.last_line_size + (right.newline_count == 0 ? left.last_line_size : 0)
    };
    return ret;
}

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
