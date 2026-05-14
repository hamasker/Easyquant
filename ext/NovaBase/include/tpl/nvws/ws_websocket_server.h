#pragma once

#include "base_common.h"
#include "nova_base.h"

#include "ws_struct.h"
#include <cstdint>

BEGIN_NOVA_NAMESPACE(ws)

using session_t = uint64_t;

class WSServerSpi {
public:
  virtual void OnOpen(session_t session) {}
  virtual void OnMessage(session_t session, const char *msg, size_t len) {}
  virtual void OnClose(session_t session, WS_ERROR_INFO close_info) {}

public:
  virtual void OnSend(session_t session, int64_t ref) {}
  virtual void OnError(WS_ERROR_INFO, session_t session, int64_t ref) {}
};

class WSServerApi {
protected:
  virtual ~WSServerApi() = default;

public:
  struct EndPoint {
    std::string address;
    uint16_t prot;

    void Clear() {
      address.clear();
      prot = 0;
    }
    EndPoint() { Clear(); }
  };

  static WSServerApi *Create(WSServerSpi *spi, bool tls = false,
                             const char *type = "wspp");

public:
  virtual WS_ERROR_INFO Initialize(const char *ip, uint16_t port,
                                   int32_t core = -1, int32_t loop_ms = 10,
                                   bool global_thread = false) = 0;
  virtual void Stop() = 0;
  virtual bool is_running() const = 0;

public:
  virtual WS_ERROR_INFO Send(session_t session, int64_t ref,
                             const std::string &msg) = 0;
  virtual WS_ERROR_INFO Send(session_t session, int64_t ref, const void *msg,
                             size_t len) = 0;

  virtual WS_ERROR_INFO CloseSession(session_t session, int64_t ref,
                                     const char *reason = "manual") = 0;

public:
  virtual WS_ERROR_INFO GetRemoteEndpoint(EndPoint &out, session_t session) = 0;
  virtual WS_ERROR_INFO GetLocalEndpoint(EndPoint &out, session_t session) = 0;

public:
  virtual WS_ERROR_INFO Listen(const char *ip, uint16_t port) = 0;
  virtual bool Poll() = 0;
};

END_NOVA_NAMESPACE(ws)