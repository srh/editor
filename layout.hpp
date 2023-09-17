#ifndef QWERTILLION_LAYOUT_HPP_
#define QWERTILLION_LAYOUT_HPP_

// Just some layout helpers here... or just one of them.

#include <stdint.h>

#include <vector>

#include "arith.hpp"
#include "error.hpp"

namespace qwi {


template <class T, class Callable>
std::vector<uint32_t> true_split_sizes(
        uint32_t rendering_span, uint32_t divider_size,
        typename std::vector<T>::const_iterator splits_begin,
        typename std::vector<T>::const_iterator splits_end,
        Callable&& splits_accessor) {
    const size_t n = splits_end - splits_begin;
    logic_checkg(n != 0);

    uint32_t splits_denominator = 0;
    for (auto it = splits_begin; it != splits_end; ++it) {
        splits_denominator += splits_accessor(*it);
    }
    logic_checkg(splits_denominator != 0);

    uint32_t rendering_cells = rendering_span - std::min<uint32_t>(rendering_span, u32_mul(divider_size, n - 1));

    std::vector<uint32_t> ret;
    ret.resize(n);
    uint32_t sum = 0;
    for (size_t i = 0; i < n; ++i) {
        // We want, approximately, rendering_cells * (pane.first / splits_denominator),
        // with the values rounded to add up to rendering_cells.
        uint32_t rendered_pane_size = u32_mul_div(rendering_cells, splits_accessor(*(splits_begin + i)), splits_denominator);
        ret[i] = rendered_pane_size;
        sum += rendered_pane_size;
    }

    // TODO: Instead of biasing upward the earlier panes, we should be more like a line rendering algo.
    size_t i = 0;
    while (sum < rendering_cells) {
        ++ret[i];
        ++sum;
        ++i;
        if (i == n) { i = 0; }
    }

    return ret;
}


}  // namespace qwi

#endif  // QWERTILLION_LAYOUT_HPP_
