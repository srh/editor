#include "state.hpp"

namespace qwi {

size_t buffer::current_column() const {
    size_t ix = bef.find_last_of('\n');
    // this works in the std::string::npos case too
    return bef.size() - ix - 1;
}


}  // namespace qwi
