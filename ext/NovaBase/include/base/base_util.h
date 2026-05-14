#pragma once

#include <bitset>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>

#if _IS_WIWNDOWS
#include <malloc.h>
#else
#include <mm_malloc.h>
#include <sys/syscall.h>
#endif

#include "base/base_common.h"
#include "base/base_type.h"

USE_NOVA_NAMESPACE(base)
BEGIN_NOVA_NAMESPACE(base)

template <typename To, typename From>
inline To bit_cast(const From &from) noexcept {
  typename std::aligned_storage<sizeof(To), alignof(To)>::type storage;
  std::memcpy(&storage, &from, sizeof(To));
  return reinterpret_cast<To &>(storage);
}

template <class T> union cas16_t {
  std::atomic<uint16_t> a;
  uint16_t i;
  T t;

  cas16_t() : t() {};
  explicit cas16_t(const T &&_) : t(std::forward<T>(_)) {}
};

template <class T> union cas32_t {
  std::atomic<uint32_t> a;
  uint32_t i;
  T t;

  cas32_t() : t() {};
  explicit cas32_t(const T &&_) : t(std::forward<T>(_)) {}
};

template <class T> union cas64_t {
  std::atomic<uint64_t> a;
  uint64_t i;
  T t;

  cas64_t() : t() {};
  explicit cas64_t(const T &&_) : t(std::forward<T>(_)) {}
};

inline uint8_t bswap8(uint8_t i) { return i; }

inline uint16_t bswap16(uint16_t i) { return (i << 8) | (i >> 8); }

inline uint32_t bswap32(uint32_t i) {
  return ((i & 0x000000FFU) << 24) | ((i & 0x0000FF00U) << 8) |
         ((i & 0x00FF0000U) >> 8) | ((i & 0xFF000000U) >> 24);
}

inline uint64_t bswap64(uint64_t i) {
  return ((i & 0x00000000000000FFULL) << 56) |
         ((i & 0x000000000000FF00ULL) << 40) |
         ((i & 0x0000000000FF0000ULL) << 24) |
         ((i & 0x00000000FF000000ULL) << 8) |
         ((i & 0x000000FF00000000ULL) >> 8) |
         ((i & 0x0000FF0000000000ULL) >> 24) |
         ((i & 0x00FF000000000000ULL) >> 40) |
         ((i & 0xFF00000000000000ULL) >> 56);
}
// inline uint16_t bswap16(uint16_t i) { return bswap16(i); }

// inline uint32_t bswap32(uint32_t i) { return bswap32(i); }

// inline uint64_t bswap64(uint64_t i) { return bswap64(i); }

template <class T> inline T bswap(T i);

template <> inline uint8_t bswap(uint8_t i) { return bswap8(i); }
template <> inline uint16_t bswap(uint16_t i) { return bswap8(i); }
template <> inline uint32_t bswap(uint32_t i) { return bswap8(i); }
template <> inline uint64_t bswap(uint64_t i) { return bswap8(i); }
template <> inline int8_t bswap(int8_t i) { return bswap8(i); }
template <> inline int16_t bswap(int16_t i) { return bswap16(i); }
template <> inline int32_t bswap(int32_t i) { return bswap32(i); }
template <> inline int64_t bswap(int64_t i) { return bswap64(i); }

inline double bswap_double(double src) {
  auto u64 = bswap64(bit_cast<uint64_t, double>(src));
  double ret;
  memcpy(&ret, &u64, sizeof(ret));
  return ret;
};

inline double bswap_float(float src) {
  auto u32 = bswap32(bit_cast<uint32_t, float>(src));
  float ret;
  memcpy(&ret, &u32, sizeof(ret));
  return ret;
};

template <> inline double bswap(double d) { return bswap_double(d); }

template <> inline float bswap(float d) { return bswap_float(d); }

void *CacheLineAlignedAlloc(size_t bytes);

template <class _Dst, class _Src>
inline _Dst *_novacpy(_Dst *dst, _Src *const src) {
  return static_cast<_Dst *>(memcpy(dst, src, sizeof(_Dst)));
}

template <class _Dst, class _Src> inline void novacpy(_Dst *dst, _Src &&src) {
  _novacpy(std::forward<_Dst>(dst), std::forward<_Src>(src));
};

template <class _Dst, class _Src, size_t N>
inline void novacpy(_Dst (&dst)[N], _Src (&src)[N]) {
  static_cast<_Dst *>(memcpy(dst, src, sizeof(_Dst) * N));
}

template <size_t N> inline void novacpy(char (&dst)[N], const char (&src)[N]) {
  memcpy(dst, src, sizeof(char) * N);
  dst[N - 1] = 0;
}

#define NOVA_MEMCPY(dst, src, size) _novacpy((dst), (src))

inline void *nova_align(size_t __align, size_t __size, void *&__ptr,
                        size_t &__space) noexcept;

inline bool DoubleGT(double l, double r, double t = 1e-6) { return l > r + t; }
inline bool DoubleGE(double l, double r, double t = 1e-6) { return l >= r - t; }
inline bool DoubleLT(double l, double r, double t = 1e-6) { return l < r - t; }
inline bool DoubleLE(double l, double r, double t = 1e-6) { return l <= r + t; }
inline bool DoubleEQ(double l, double r, double t = 1e-6) {
  return std::abs(l - r) <= t;
}
inline bool DoubleNE(double l, double r, double t = 1e-6) {
  return std::abs(l - r) > t;
}

const char *get_file_name(const char *path);

std::string version_to_str(version_t ver);
version_t str_to_version(std::string_view st);

inline bool check_version(version_t ver, version_t exp) {
  if (ver.u32 != exp.u32)
    return false;
  return true;
}

template <size_t N>
constexpr const char *get_filenmae(const char (&s)[N], size_t i = N - 1) {
  return (s[i] == '/' || s[i] == '\\') ? (s + i + 1)
                                       : (i == 0 ? s : get_filename(s, i - 1));
}

template <size_t N> constexpr size_t get_filenmae_len(const char (&s)[N]) {
  return N - 1 - (get_filenmae(s) - s);
}

#define GET_FILENAME() get_filename(__FILE__)

END_NOVA_NAMESPACE(base)