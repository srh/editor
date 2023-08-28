#include "chars.hpp"

namespace qwi {

buffer_string to_buffer_string(const std::string& s) {
    buffer_string ret{as_buffer_chars(s.data()), s.size()};
    return ret;
}

}  // namespace qwi
