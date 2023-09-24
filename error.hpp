#ifndef QWERTILLION_ERROR_HPP_
#define QWERTILLION_ERROR_HPP_

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define NO_COPY(typ) typ(const typ&) = delete; void operator=(const typ&) = delete

struct runtime_check_failure { };

// TODO: Right now typically messages to stderr get printed in a weird terminal mode.

// We are assuming _GNU_SOURCE, which returns const char * and sometimes doesn't use the
// buf.
#ifndef _GNU_SOURCE
#error "Our invokation of strerror_r assumes _GNU_SOURCE"
#endif

#define runtime_check_strerror strerror_r(errno, runtime_check_strerr_buf, sizeof(runtime_check_strerr_buf))

#define runtime_fail(fmt, ...) do { \
        char runtime_check_strerr_buf[1024]; \
        (void)runtime_check_strerr_buf; \
        fprintf(stderr, "Runtime failure! " fmt "\n", ##__VA_ARGS__);   \
        throw runtime_check_failure{}; \
    } while (false)

#define runtime_check(pred, fmt, ...) do { \
        if (!(pred)) { \
            char runtime_check_strerr_buf[1024]; \
            (void)runtime_check_strerr_buf; \
            fprintf(stderr, "Runtime error! " fmt "\n", ##__VA_ARGS__); \
            throw runtime_check_failure{}; \
        } \
    } while (false)

// So far there is no logic_check_failure type.

#define logic_fail(fmt, ...) do { \
        fprintf(stderr, "Logic error! " fmt "\n", ##__VA_ARGS__); \
        abort(); \
    } while (false)

#define logic_check(pred, fmt, ...) do { \
        if (!(pred)) { \
            fprintf(stderr, "Logic error! (%s) " fmt "\n", #pred, ##__VA_ARGS__); \
            abort(); \
        } \
    } while (false)

#define logic_checkg(pred) do { \
        if (!(pred)) { \
            fprintf(stderr, "Logic error! (%s): \n", #pred); \
            abort(); \
        } \
    } while (false)

#if 0
#define debugf(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#else
#define debugf(fmt, ...) [](...){}(fmt, ##__VA_ARGS__)
#endif

// Maybe this doesn't belong here.
struct ui_result {
    bool erred = false;
    std::string message;
    bool errored() const { return erred; }
    static ui_result error(std::string msg) { return {.erred = true, .message = std::move(msg)}; }
    static ui_result success() { return {.erred = false, .message = {}}; }
};

#endif  // QWERTILLION_ERROR_HPP_
