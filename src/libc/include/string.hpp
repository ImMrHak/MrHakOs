#ifndef _LIBC_STRING_H
#define _LIBC_STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t strlen(const char* str);
int strcmp(const char* str1, const char* str2);
void* memset(void* s, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);

#ifdef __cplusplus
}
#endif

#endif // _LIBC_STRING_H