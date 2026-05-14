#pragma once

#ifdef _WIN32
#define _IS_WINDOWS 1
#else
#define _IS_WINDOWS 0
#endif

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wspiapi.h>
#else
#include <arpa/inet.h>
#include <dirent.h>
#ifdef __linux__
#include <endian.h>
#elif defined(__APPLE__)
#include <machine/endian.h>
#endif
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#include <x86intrin.h>
#endif
#ifndef NULL
#define NULL 0
#endif
#endif // _WIN32

#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/types.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <thread>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

#define BEGIN_NOVA_NAMESPACE(my_ns)                                            \
  namespace nova {                                                             \
  namespace my_ns {

#define END_NOVA_NAMESPACE(my_ns)                                              \
  }                                                                            \
  }

#define USE_NOVA_NAMESPACE(my_ns) using namespace ::nova::my_ns;

#define NOVA_ASSERT(expr) assert((expr))
#define NOVA_ASSERT_MSG(expr, msg) assert((expr) && (msg))
#define NOVA_ASSERT_AND_RETURN(expr, res)                                      \
  if (!(expr)) {                                                               \
    NOVA_ASSERT(false);                                                        \
    return res;                                                                \
  }

#ifdef _WIN32
#ifdef _WIN_DLL
#define NOVA_DLL_EXPORT __declspec(dllimport)
#else
#define NOVA_DLL_EXPORT __declspec(dllimport)
#endif
#define NOVA_DLL_LOCAL
#else
#define NOVA_DLL_EXPORT __attribute__((visibility("default")))
#define NOVA_DLL_LOCAL __attribute__((visibility("hidden")))

#define MAX_PATH 1024
#endif

#if defined(__GNUC__) && __GNUC__ >= 4
#define LIKELY(x) (__builtin_expect((x), 1))
#define UNLIKELY(x) (__builtin_expect((x), 0))
#define FORCE_INLINE __attribute__((__always_inline__))
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#define FORCE_INLINE __forceinline
#endif

#if defined(__clang__) || defined(__GNUC__)
#define NOVA_ALIGNED(size) __attribute__((__aligned__(size)))

#ifndef THREAD_LOCAL
#define THREAD_LOCAL __thread
#endif

#elif defined(_MSC_VER)
#define NOVA_ALIGNED(size) __declspec(align(size))

#ifndef THREAD_LOCAL
#define THREAD_LOCAL __declspec(thread)
#endif

#else
#error Cannot define NOVA_ALIGNED on this platform
#endif
#define NOVA_ALIGNED_MAX NOVA_ALIGNED(alignof(std::max_align_t))

#define NOVA_CACHE_LINE 128
#define NOVA_ALIGNED_CACHE_LINE NOVA_ALIGNED(NOVA_CACHE_LINE)

constexpr std::size_t NOVA_CACHE_ALIGNMENT{NOVA_CACHE_LINE};
constexpr std::size_t NOVA_CACHE_LINE_LENGTH{NOVA_CACHE_LINE};
constexpr std::size_t NOVA_PREFETCH_STRIDE{4 * NOVA_CACHE_LINE_LENGTH};

#define ROUND_UP(p, align) (((p) + (align)-1) / (align) * (align))
#define ROUND_DOWN(p, align) ((p) / (align) * (align))
#define MEM_TYPE(cls, men) decltype(((cls *)0)->mem)

#ifdef offsetof
#define OFFSET_OF(cls, mem) offsetof(cls, mem)
#else
#define OFFSET_OF(cls, mem) ((size_t) & (((cls *)0)->mem))
#endif
#define OFFSET_BETWEEN(cls, mem1, mem2)                                        \
  (OFFSET_OF(cls, mem2) - OFFSET_OF(cls, mem1))

constexpr size_t KB = 1024;
constexpr size_t MB = KB * KB;
constexpr size_t HUGE_PAGE_SIZE = 2 * MB;

inline void _BaseStaticCheck() { // format: int64_t =? lld or llu
  static_assert(sizeof(uint64_t) == sizeof(long long unsigned int), "");
  static_assert(alignof(uint64_t) == alignof(long long unsigned int), "");

  static_assert(sizeof(uint64_t) == sizeof(long long unsigned), "");
  static_assert(alignof(uint64_t) == alignof(long long unsigned), "");

  static_assert(sizeof(uint64_t) == sizeof(long long int), "");
  static_assert(alignof(uint64_t) == alignof(long long int), "");
  // format: int32_t =? d or u
  static_assert(sizeof(uint32_t) == sizeof(int), "");
  static_assert(alignof(uint32_t) == alignof(int), "");

  static_assert(sizeof(uint32_t) == sizeof(unsigned int), "");
  static_assert(alignof(uint32_t) == alignof(unsigned int), "");

  static_assert(sizeof(uint32_t) == sizeof(unsigned), "");
  static_assert(alignof(uint32_t) == alignof(unsigned), "");
}

#define slow_if(coud) if (UNLIKELY(coud))
#define fast_if(coud) if (LIKELY(coud))

#define NONCOPYABLE(_m_T)                                                      \
  _m_T(const _m_T &) = delete;                                                 \
  void operator=(const _m_T &) = delete

#define NOVA_UNUSED __attribute__((unused))
#define NOVA_DEPRECATED __attribute__((deprecated))