#ifndef QWERTILLION_ARITH_HPP_
#define QWERTILLION_ARITH_HPP_

#include <stdint.h>

inline uint32_t u32_mul(uint32_t x, uint32_t y) {
    // TODO: Throw upon overflow.
    return x * y;
}
inline uint32_t u32_add(uint32_t x, uint32_t y) {
    // TODO: Throw upon overflow.
    return x + y;
}
inline uint32_t u32_sub(uint32_t x, uint32_t y) {
    // TODO: Throw upon overflow.
    return x - y;
}

inline size_t size_mul(size_t x, size_t y) {
    // TODO: Throw upon overflow.
    return x * y;
}
inline size_t size_add(size_t x, size_t y) {
    // TODO: Throw upon overflow.
    return x + y;
}

#endif  // QWERTILLION_ARITH_HPP_