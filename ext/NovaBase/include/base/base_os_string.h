#pragma once

#include "base/base_util.h"

#include <sys/timeb.h>

#include <vector>

#ifdef _WIN32
#include <intrin.h>
#else
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <iomanip>
#endif

BEGIN_NOVA_NAMESPACE(base)

using std::string;

inline int os_strlen(const char *s, size_t maxlen)
{
    return (int)(maxlen <= 0 ? strlen(s) : strnlen(s, maxlen));
}

inline int os_strcpy(char *dest, const char *src, size_t size)
{
    if (size <= strnlen(src, size))
        return -1;
    strcpy(dest, src);
    return (int)strlen(dest);
}

inline int os_strcat(char *dest, char *src, size_t size)
{
    auto n = size - strnlen(dest, size);
    if (n <= strlen(src))
        return -1;
    strncat(dest, src, n);
    return (int)strlen(dest);
}

inline int os_sprintf(char *dest, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    int size;
    size = vsnprintf(dest, n, fmt, ap);
    va_end(ap);
    return size;
}

inline int os_strcmp(const char *s1, const char *s2) { return strcmp(s1, s2); }

inline int os_strcasecmp(const char *s1, const char *s2) { return strcasecmp(s1, s2); }

inline int os_strncasecmp(const char *s1, const char *s2, size_t n) { return strncasecmp(s1, s2, n); }

inline char *os_strdup(const char *str) { return strdup(str); }

int os_strtrim(char *s);

bool os_strtoll(const char *str, int64_t &i64);

const char *get_first_sub_str(const char *src, char *sub_str, char split);

bool os_dupenv(char **pBuf, const char *szName);

size_t os_gbk_to_utf8(char *dst, size_t dst_bytes, const char *src);

int convert_from_gb2312_to_utf8(char *utf8, size_t utf8_buff_len, char *gb2312);

bool StartsWith(const string &str, const string &prefix, bool ignore_case = false);

bool EndsWith(const string &str, const string &suffix, bool ignore_case = false);

string Trim(string str);
string RemoveSpances(string str);

std::vector<string> Split(const string &str, char delimiter);
std::vector<string> Split(const string &str, const char *delimiters);

END_NOVA_NAMESPACE(base)