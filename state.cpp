#include "state.hpp"

#include "error.hpp"

namespace qwi {

size_t buffer::current_column() const {
    size_t ix = bef.find_last_of('\n');
    // this works in the std::string::npos case too
    return bef.size() - ix - 1;
}

void buffer::set_cursor(size_t pos) {
    if (pos < bef.size()) {
        aft.insert(aft.begin(), bef.begin() + pos, bef.end());
        bef.resize(pos);
    }
    size_t aft_pos = pos - bef.size();
    logic_check(aft_pos <= aft.size(), "set_cursor outside buf range");
    bef.append(aft.data(), aft_pos);
    aft.erase(0, aft_pos);
}

}  // namespace qwi

size_t distance_to_eol(const qwi::buffer& buf, size_t pos) {
    size_t p = pos;
    for (size_t e = buf.size(); p < e; ++p) {
        if (buf[p] == '\n') {
            break;
        }
    }
    return p - pos;
}

size_t distance_to_beginning_of_line(const qwi::buffer& buf, size_t pos) {
    logic_check(pos <= buf.size(), "distance_to_beginning_of_line with out of range pos");
    size_t p = pos;
    for (;;) {
        if (p == 0) {
            return pos;
        }
        --p;
        if (buf[p] == '\n') {
            return pos - (p + 1);
        }
    }
}
