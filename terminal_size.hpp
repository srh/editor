#ifndef QWERTILLION_TERMINALSIZE_HPP_
#define QWERTILLION_TERMINALSIZE_HPP_

#include <stdint.h>

struct terminal_size {
    uint32_t rows = 0, cols = 0;
    friend auto operator<=>(const terminal_size&, const terminal_size&) = default;
};

#endif  // QWERTILLION_TERMINALSIZE_HPP_
