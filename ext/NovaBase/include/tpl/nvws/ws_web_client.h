#pragma once

#include "ws_struct.h"
#include "ws_util.h"
#include <cstdint>

BEGIN_NOVA_NAMESPACE(ws)

class RestClientSpi {
public:
  using ResponseHeader = std::multimap<std::string, std::string>;

  virtual void OnError(uint64_t ref, const WS_ERROR_INFO *err) {}
  virtual void OnSendRequest(uint64_t ref) {}
  virtual void OnResponse(uint64_t ref, const char *msg, size_t len,
                          const ResponseHeader &header,
                          const std::string &status_code) {}

  virtual void OnHTTP2Close(uint64_t ref) {}
};

class RestClientApi {
protected:
  virtual ~RestClientApi() = default;

public:
  using Header = std::map<std::string, std::string>;
  using Message = std::string;

public:
  static RestClientApi *Create(RestClientSpi *spi, bool https = true,
                               bool http2 = false);

  /**
   * start on_xxx thread
   **/
  virtual WS_ERROR_INFO Initialize(const char *base_url, int32_t core = -1,
                                   int32_t loop_ms = 10,
                                   bool global_thread = false) = 0;
  virtual void Stop() = 0;
  virtual WS_ERROR_INFO Request(uint64_t ref, REST_REQ_TYPE req_type,
                                const Message &uri, const Header &hdr,
                                const Message &body) = 0;
};

class FastRestClientApi {
protected:
  FastRestClientApi() = default;
  ~FastRestClientApi() = default;

public:
  using Header = std::map<std::string, std::string>;
  using Message = std::string;
  static FastRestClientApi *Create(RestClientSpi *, bool https, bool http2);

  void RegisterHeartBeat(REST_REQ_TYPE method, const Message &uri,
                         const Header &hdr, const Message &params,
                         int32_t hb_interval_ms = 15 * 1000,
                         int32_t hb_link = 60);
  WS_ERROR_INFO Initialize(const char *base_url, int32_t call_back_core,
                           int32_t call_back_loop_ms, bool global_thread,
                           int poolcount = 10, int poolcore = -1);
  void Stop();
  WS_ERROR_INFO Request(uint64_t ref, REST_REQ_TYPE req_type,
                        const Message &uri, const Header &hdr,
                        const Message &params);
};

END_NOVA_NAMESPACE(ws)