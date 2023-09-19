#ifndef QWERTILLION_UTIL_HPP_
#define QWERTILLION_UTIL_HPP_

// The dreaded "utils" header.

#include <string>
#include <vector>

std::string string_join(const std::string& inbetween, const std::vector<std::string>& vals);

// Used in rendering of control characters.  (This is not terminal-specific -- GUIs will
// still render control chars as ^A, ^B, etc., in the buffer.)
constexpr uint8_t CTRL_XOR_MASK = 64;

#endif  // QWERTILLION_UTIL_HPP_
