#pragma once

#include "ws_struct.h"
#include "ws_util.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>

BEGIN_NOVA_NAMESPACE(ws)

using session_t = uint64_t;

class RestServerSpi {
public:
  using ResponseHeader = std::multimap<std::string, std::string>;
  using RequestHeader = std::multimap<std::string, std::string>;

  virtual void OnRequest(session_t session, REST_REQ_TYPE method,
                         const std::string &uri, const RequestHeader &headers,
                         const char *body, size_t body_len) {}

  virtual void OnError(session_t session, WS_ERROR_INFO err) {}
  virtual void OnConnectionOpen(session_t session) {}
  virtual void OnConnectionClose(session_t session) {}
};

class RestServerApi {
protected:
  virtual ~RestServerApi() = default;

public:
  using Header = std::map<std::string, std::string>;

  static RestServerApi *Create(RestServerSpi *spi, bool https = false);

public:
  /**
   * Initialize and start the REST server
   * @param ip - IP address to bind (e.g., "0.0.0.0")
   * @param port - Port to listen on
   * @param core - CPU core to bind (-1 for no binding)
   * @param loop_ms - Event loop interval in milliseconds
   * @param global_thread - Whether to use global thread
   */
  virtual WS_ERROR_INFO Initialize(const char *ip, uint16_t port,
                                   int32_t core = -1, int32_t loop_ms = 10,
                                   bool global_thread = false) = 0;

  /**
   * Stop the server
   */
  virtual void Stop() = 0;

  /**
   * Check if server is running
   */
  virtual bool is_running() const = 0;

public:
  /**
   * Send HTTP response
   * @param session - Session ID
   * @param status_code - HTTP status code (e.g., "200 OK", "404 Not Found")
   * @param headers - Response headers
   * @param body - Response body
   */
  virtual WS_ERROR_INFO SendResponse(session_t session,
                                     const std::string &status_code,
                                     const Header &headers,
                                     const std::string &body) = 0;

  /**
   * Send HTTP response with binary data
   */
  virtual WS_ERROR_INFO SendResponse(session_t session,
                                     const std::string &status_code,
                                     const Header &headers, const void *data,
                                     size_t len) = 0;

  /**
   * Close a session
   */
  virtual WS_ERROR_INFO CloseSession(session_t session) = 0;

public:
  /**
   * Register a route handler
   * @param method - HTTP method (GET, POST, etc.)
   * @param path - URL path (e.g., "/api/users")
   * @param handler - Handler function
   */
  using RouteHandler = std::function<void(session_t, const std::string &,
                                          const Header &, const std::string &)>;

  virtual void RegisterRoute(REST_REQ_TYPE method, const std::string &path,
                             RouteHandler handler) = 0;

  /**
   * Set default 404 handler
   */
  virtual void SetNotFoundHandler(RouteHandler handler) = 0;

public:
  /**
   * Poll for events (if not using threaded mode)
   */
  virtual bool Poll() = 0;
};

END_NOVA_NAMESPACE(ws)
