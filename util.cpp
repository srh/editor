#include "util.hpp"

std::string string_join(const std::string& inbetween, const std::vector<std::string>& vals) {
    std::string ret;
    size_t n = vals.size();
    if (n == 0) {
        return ret;
    }
    for (size_t i = 0; ; ) {
        ret += vals[i];
        ++i;
        if (i == n) {
            return ret;
        }
        ret += inbetween;
    }
}
