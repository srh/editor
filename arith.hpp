#ifndef QWERTILLION_ARITH_HPP_
#define QWERTILLION_ARITH_HPP_

#include <inttypes.h>
#include <stdint.h>

#include "error.hpp"

// TODO: Why are these assertions logic_fail and not runtime_fail?

inline bool try_u32_mul(uint32_t x, uint32_t y, uint32_t *out) {
    uint64_t x64 = x;
    uint64_t y64 = y;
    uint64_t result = x64 * y64;
    if (result > UINT32_MAX) {
        return false;
    }
    *out = result;
    return true;
}

inline uint32_t u32_mul(uint32_t x, uint32_t y) {
    uint32_t ret;
    if (!try_u32_mul(x, y, &ret)) {
        logic_fail("u32_mul overflow %" PRIu32 " * %" PRIu32, x, y);
    }
    return ret;
}

inline uint32_t u32_mul_div(uint32_t x, uint32_t y, uint32_t z) {
    uint64_t tmp = x * y;
    uint64_t result = tmp / z;
    if (result > UINT32_MAX) {
        logic_fail("u32_mul_div overflow %" PRIu32 " * %" PRIu32 " / %" PRIu32, x, y, z);
    }
    return static_cast<uint32_t>(result);
}

inline bool try_u32_add(uint32_t x, uint32_t y, uint32_t *out) {
    if (y > UINT32_MAX - x) {
        return false;
    }
    *out = x + y;
    return true;
}
inline uint32_t u32_add(uint32_t x, uint32_t y) {
    uint32_t ret;
    if (!try_u32_add(x, y, &ret)) {
        logic_fail("u32_add overflow %" PRIu32 " + %" PRIu32, x, y);
    }
    return ret;
}
inline uint32_t u32_sub(uint32_t x, uint32_t y) {
    logic_check(y <= x, "u32_sub overflow %" PRIu32 " - %" PRIu32, x, y);
    return x - y;
}

inline size_t size_mul(size_t x, size_t y) {
    // TODO: Come on, some better way.  OF flag.
    if (x > SIZE_MAX / y) {
        logic_fail("size_mul overflow %zu * %zu", x, y);
    }
    return x * y;
}
inline size_t size_add(size_t x, size_t y) {
    logic_check(SIZE_MAX - x >= y, "size_t add overflowed %zu + %zu", x, y);
    return x + y;
}
inline size_t size_sub(size_t x, size_t y) {
    logic_check(y <= x, "size_sub overflow %zu - %zu", x, y);
    return x - y;
}

#endif  // QWERTILLION_ARITH_HPP_
