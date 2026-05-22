#ifndef UTIL_H
#define UTIL_H

#include "calculate_util.h"
#include "container_util.h"
#include "file_util.h"
#include "str_util.h"
#include "time_util.h"

namespace sum_util {
using namespace str_util;
using namespace container_util;
using namespace calculate_util;
using namespace time_util;
using namespace file_util;

inline int tick_size_to_price_precision(double tick_size) {
  if (tick_size <= 0.0) return 0;
  int precision = 0;
  while (tick_size < 1) {
    tick_size *= 10;
    precision++;
  }
  return precision;
}
} // namespace sum_util

#endif // UTIL_H
