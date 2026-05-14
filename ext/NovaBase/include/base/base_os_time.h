#pragma once

#include "base/base_common.h"

#include <sys/timeb.h>
#include <time.h>

#ifdef _WIN32

#else
#include <sys/syscall.h>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#include <x86intrin.h>
#endif
#endif

BEGIN_NOVA_NAMESPACE(base)

constexpr int64_t kMilliPerSecond = 1000;
constexpr int64_t kMicroPerSecond = 1000000;
constexpr int64_t kNanoPerSecond = 1000000000ll;
constexpr int BASE_CONST_MS_PER_DAY = 86400000;
constexpr int BASE_CONST_MS_ZONE = 57600000;

inline void Pause() { __asm__ __volatile__("rep;nop" : : : "memory"); }

inline void CpuDelay(uint64_t delay) {
  for (uint64_t i = 0; i < delay; i++) {
    Pause();
  }
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)

inline uint64_t rdtsc() { return __rdtsc(); }

inline uint64_t rdtscp() {
  uint32_t aux;
  return __rdtscp(&aux);
}

inline uint64_t rdtscp(int &chip, int &core) {
  uint32_t aux;
  auto ret = __rdtscp(&aux);
  chip = int((aux & 0xFFF000) >> 12);
  core = int(aux & 0xFFF);
  return ret;
}

inline void CpuId() {
  uint64_t rax, rbx, rcx, rdx;
  __asm__ __volatile__("cpuid" : "=a"(rax), "=b"(rbx), "=d"(rdx), "=c"(rcx));
}

#else

inline uint64_t rdtsc() { return 0; }
inline uint64_t rdtscp() { return 0; }
inline uint64_t rdtscp(int &chip, int &core) {
  chip = 0;
  core = 0;
  return 0;
}
inline void CpuId() {}

#endif

inline int64_t GetNanoSecond() {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return now.tv_sec * 1000000000ll + now.tv_nsec;
}

inline int64_t GetNanoSecondReal() {
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  return now.tv_sec * 1000000000ll + now.tv_nsec;
}

int64_t GetMicroSecond();

inline int64_t GetUsec() { return GetMicroSecond(); }

int64_t GetMilliSecond();

inline int64_t GetMsec() { return GetMilliSecond(); }

inline int64_t GetSecond() { return time(nullptr); };

void LocalTime(struct tm *tm1, const time_t *timep);

double InitCpuFreqGhz(size_t test_time_ms = 500);

int64_t cycles2ns(int64_t cycle, double ghz);

int64_t ns2cycles(int64_t ns, double ghz);

class TSCConverter {
public:
  double Initialize(size_t test_time_ms = 100, double freq_ghz = 0);

  bool AdjOffset();

  int64_t ns() const { return ns(rdtsc()); }

  int64_t ns(uint64_t tsc) const {
    return ns_offset_ + int64_t(tsc * cycle_ns_);
  }

private:
  double cycle_ns_ = 0;
  int64_t ns_offset_ = 0;

  int64_t last_tsc_ = 0;
};

END_NOVA_NAMESPACE(base)