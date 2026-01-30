#ifndef UTILS_H
#define UTILS_H

#include <cstddef>

inline size_t boundedStrnlen(const char* s, size_t max_len) {
    size_t len = 0;
    while (len < max_len && s[len] != '\0') {
        ++len;
    }
    return len;
}

#endif
