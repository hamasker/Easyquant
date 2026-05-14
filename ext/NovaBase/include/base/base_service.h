#pragma once

#include "base/base_type.h"
#include "base/base_util.h"
#include <cstdint>
#include <sys/types.h>

BEGIN_NOVA_NAMESPACE(base)

#pragma pack(push, 1)

struct ServiceCallHeader {
  uint32_t size;
  uint16_t code;
  uint16_t reserved1;
  uint16_t reserved2;

  char data[0];

  ServiceCallHeader() : size(0), code(0), reserved1(0), reserved2(0) {}
};

using sc_hdr_t = ServiceCallHeader;

struct ReqInfo {
  int64_t req_id;
};

struct RspErrInfo {
  static constexpr auto ERR_LEN = 60;
  int32_t code;
  char msg[ERR_LEN];

  RspErrInfo() : code{0}, msg{0} {}

  void Set(int32_t ec, const char *fmt, ...);
  void Set(const std::error_code &ec);
};

struct RspInfo {

  int64_t req_id;
  RspErrInfo err;
};

#pragma pack(pop)

class Session;

using recv_fun = bool (*)(const sc_hdr_t *hdr, Session *sess);
using push_fun = bool (*)(const sc_hdr_t *hdr, Session *sess, const void *data,
                          size_t len);

using RecvFunc = std::function<bool(const sc_hdr_t *hdr, Session *sess)>;
using PushFunc = std::function<bool(const sc_hdr_t *hdr, Session *sess,
                                    const void *data, size_t len)>;

END_NOVA_NAMESPACE(base)