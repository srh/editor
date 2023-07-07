#ifndef QWERTILLION_ERROR_HPP_
#define QWERTILLION_ERROR_HPP_

struct runtime_check_failure { };

// TODO: we are assuming _GNU_SOURCE, which returns const char * and sometimes doesn't use
// the buf.
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



#endif  // QWERTILLION_ERROR_HPP_
