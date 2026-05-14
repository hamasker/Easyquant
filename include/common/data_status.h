#ifndef DATA_STATUS_H
#define DATA_STATUS_H

#include <cstdint>

namespace data {

enum class abnormal_status : int8_t {
  UNKNOWN = 0,
  NORMAL,
  AIM_EXCH_INVALID,
  EXTERNAL_EXCH_INVALID,
  DIGITAL_CURRENCY_INVALID
};
}

#endif // DATA_STATUS_H