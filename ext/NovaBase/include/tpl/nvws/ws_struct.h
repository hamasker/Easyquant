#pragma once

#include "nova_base.h"

BEGIN_NOVA_NAMESPACE(ws)

enum WS_STATE_TYPE {
  WS_STATE_CONNECTING = 0,
  WS_STATE_OPEN,
  WS_STATE_CLOSING,
  WS_STATE_CLOSED,
};

enum WS_ERROR_CODE : int32_t {
  WS_OK = 0,
  WS_ERR = 1,

  WS_ERR_SESSION_NOT_FOUND = 11,
  WS_ERR_SESSION_INVALID_STATE = 12,
  WS_ERR_INVALID_SERVICE = 13,

  WS_ERR_INTERNAL = 90,
  WS_ERR_CLSOE_MANUAL = 91,
  WS_ERR_TIMEOUT = 92,
  WS_ERR_FORMATTER = 93,
  WS_ERR_UNKNOWN = 100,
};

struct WS_ERROR_INFO {
  static constexpr auto ERR_LEN = 60;

  int32_t code;
  char msg[ERR_LEN];

  WS_ERROR_INFO() : code{0}, msg{0} {}

  void Set(int32_t ec, const char *fmt, ...) {
    code = ec;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, ERR_LEN, fmt, ap);
    va_end(ap);
  }

  void Set(const std::error_code &ec) {
    code = ec.value();
    strncpy(msg, ec.message().c_str(), ERR_LEN - 1);
    msg[ERR_LEN - 1] = 0;
  }
};

enum REST_REQ_TYPE {
  REST_REQ_GET,
  REST_REQ_POST,
  REST_REQ_PUT,
  REST_REQ_DELTE
};

constexpr const char *REST_REQ_METHOD[] = {"GET", "POST", "PUT", "DELETE"};

END_NOVA_NAMESPACE(ws)