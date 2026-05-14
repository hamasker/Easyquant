#ifndef TIME_UTIL_H
#define TIME_UTIL_H

#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace time_util {
using Clock = std::chrono::system_clock;
using NanoSec = std::chrono::nanoseconds;

enum class TimeZone {
  Local,
  UTC,
  CST // UTC+8
};

// 通用格式化函数
inline std::string FormatTime(std::time_t t, const char *fmt,
                              TimeZone zone = TimeZone::Local) {
  std::tm tm{};
  switch (zone) {
  case TimeZone::Local:
    localtime_r(&t, &tm);
    break;
  case TimeZone::UTC:
    gmtime_r(&t, &tm);
    break;
  case TimeZone::CST: {
    t += 8 * 3600;
    gmtime_r(&t, &tm);
    break;
  }
  }

  char buf[32];
  std::strftime(buf, sizeof(buf), fmt, &tm);
  return {buf};
}

// 获取昨天的日期字符串（格式：YYYYMMDD）
inline std::string GetPreviousDate(std::string_view date) {
  if (date.size() != 8)
    throw std::runtime_error("Invalid date format");

  std::tm time{};
  time.tm_year = std::stoi(std::string(date.substr(0, 4))) - 1900;
  time.tm_mon = std::stoi(std::string(date.substr(4, 2))) - 1;
  time.tm_mday = std::stoi(std::string(date.substr(6, 2)));

  time_t t = std::mktime(&time);
  if (t == -1)
    throw std::runtime_error("mktime error");

  t -= 86400;
  return FormatTime(t, "%Y%m%d");
}

// 当前本地时间字符串：YYYYMMDD
inline std::string GetCurrentDateString() {
  return FormatTime(Clock::to_time_t(Clock::now()), "%Y%m%d");
}

// 当前本地时间字符串：YYYYMMDD.HH:MM:SS
inline std::string GetCurrentDateTimeString() {
  return FormatTime(Clock::to_time_t(Clock::now()), "%Y%m%d.%H:%M:%S");
}

// 当前北京时间字符串（UTC+8）：YYYYMMDD
inline std::string GetCurrentBeijingDateString() {
  return FormatTime(Clock::to_time_t(Clock::now()), "%Y%m%d", TimeZone::CST);
}

// 当前北京时间时间戳（纳秒）
inline int64_t GetCurrentBeijingTimestampNs() {
  return std::chrono::duration_cast<NanoSec>(Clock::now().time_since_epoch() +
                                             std::chrono::hours(8))
      .count();
}

// 将时间戳转为字符串，单位支持 "ns", "us", "ms", "s"
inline std::string TS2String(int64_t ts, std::string_view unit = "ns") {
  using namespace std::chrono;
  system_clock::time_point tp;

  if (unit == "ns")
  tp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
    std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>(std::chrono::nanoseconds(ts)));
  else if (unit == "us")
    tp = time_point<system_clock, microseconds>(microseconds(ts));
  else if (unit == "ms")
    tp = time_point<system_clock, milliseconds>(milliseconds(ts));
  else if (unit == "s")
    tp = time_point<system_clock, seconds>(seconds(ts));
  else
    throw std::invalid_argument("Invalid time unit");

  std::time_t t = system_clock::to_time_t(tp);
  return FormatTime(t, "%Y%m%d %H:%M:%S");
}

// 判断时间戳位数
inline int GetTimestampDigits(int64_t timestamp) {
  int digits = 1;
  while (timestamp >= 10) {
    timestamp /= 10;
    ++digits;
  }
  return digits;
}

// 将字符串时间（如"2024-07-15 12:34:56"）转为纳秒时间戳（本地时间）
inline std::int64_t ParseToNanosecTimestamp(std::string_view s) {
  if (s.size() != 17 || s[8] != '.' || s[11] != ':' || s[14] != ':')
    throw std::runtime_error("Invalid format: " + std::string(s));

  std::tm tm{};
  std::istringstream ss{std::string(s)};
  ss >> std::get_time(&tm, "%Y%m%d.%H:%M:%S");
  if (ss.fail())
    throw std::runtime_error("ParseToNanosecTimestamp failed: " +
                             std::string(s));

  std::time_t t = std::mktime(&tm);
  if (t == -1)
    throw std::runtime_error("mktime failed");
  return std::chrono::duration_cast<NanoSec>(
             Clock::from_time_t(t).time_since_epoch())
      .count();
}

// 将北京时间字符串（"YYYYMMDD.HH:MM:SS"）转为纳秒时间戳
inline std::int64_t ParseBeijingToNanosecTimestamp(std::string_view s) {
  if (s.size() != 17 || s[8] != '.' || s[11] != ':' || s[14] != ':')
    throw std::runtime_error("Invalid format: " + std::string(s));

  std::tm tm{};
  std::istringstream ss{std::string(s)};
  ss >> std::get_time(&tm, "%Y%m%d.%H:%M:%S");
  if (ss.fail())
    throw std::runtime_error("ParseBeijingToNanosecTimestamp failed: " +
                             std::string(s));

  std::time_t local_time = std::mktime(&tm);
  if (local_time == -1)
    throw std::runtime_error("mktime failed");

  std::time_t utc_time = local_time - 8 * 3600;
  return std::chrono::duration_cast<NanoSec>(std::chrono::seconds(utc_time))
      .count();
}

} // namespace time_util

#endif // TIME_UTIL_H
