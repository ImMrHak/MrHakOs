#include <string.hpp>

// Minimal freestanding implementations safe on x86_64
size_t strlen(const char* str) {
    const char* s = str;
    while (*s) ++s;
    return (size_t)(s - str);
}

int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { ++a; ++b; }
    unsigned char ua = (unsigned char)*a;
    unsigned char ub = (unsigned char)*b;
    return (int)ua - (int)ub;
}

void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    unsigned char v = (unsigned char)c;
    for (size_t i = 0; i < n; ++i) p[i] = v;
    return s;
}

void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dest;
}
