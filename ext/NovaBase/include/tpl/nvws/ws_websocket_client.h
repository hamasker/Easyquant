#pragma once

#include "base/base_spsc_queue.h"
#include "nvws/ws_struct.h"
#include "ws_util.h"
#include <tuple>

BEGIN_NOVA_NAMESPACE(ws)

class WSClientSpi {
public:
  virtual void OnFail(WS_ERROR_INFO err) {}
  virtual void OnOpen(int group) {}

  virtual void OnMessage(const char *msg, size_t len, int group) {}
  virtual void OnClose(bool manual) {}
  virtual void OnGroupClose(bool manual, int group) {}

  virtual void OnPong(const char *msg, size_t len) {}
};

class WSClientApi {
protected:
  virtual ~WSClientApi() = default;

public:
  /**
   * create api
   **/
  static WSClientApi *Create(WSClientSpi *spi, bool tls = true, int group = -1);

public:
  /**
   * start on_xxx thread, all function blow should be called in OnXX
   *
   * Reconnect until Stop() is called
   **/
  virtual WS_ERROR_INFO Initialize(const char *url, int32_t core = -1,
                                   int32_t loop_ms = 10,
                                   bool global_thread = false) = 0;
  virtual void Stop() = 0;
  virtual void Restart(const char *uri) = 0;

  // send binary
  virtual WS_ERROR_INFO Send(const char *msg, size_t len) = 0;

  // send text
  virtual WS_ERROR_INFO Send(const std::string &str) = 0;

public:
  virtual WS_STATE_TYPE state() const = 0;

  virtual const WS_ERROR_INFO *local_close_info() const = 0;
  virtual const WS_ERROR_INFO *remote_close_info() const = 0;

  virtual const WS_ERROR_INFO *last_error() const = 0;

public:
  virtual WS_ERROR_INFO Connect(const char *uri) = 0;
  virtual bool Poll() = 0;
};

class FastWSClientApi {
private:
  FastWSClientApi() = default;

public:
  ~FastWSClientApi();

public:
  static FastWSClientApi *Create(WSClientSpi *spi, bool tls = true,
                                 int group = -1, bool origin_version = true);

public:
  WS_ERROR_INFO Initialize(const char *url, int32_t message_core = -1,
                           int32_t loop_ms = 0, bool global_thread = false);

  void Stop();
  void Restart(const char *uri);

  // send binary
  WS_ERROR_INFO Send(const char *msg, size_t len);
  // send text
  WS_ERROR_INFO Send(const std::string &str);

public:
  WS_STATE_TYPE state() const;

  const WS_ERROR_INFO *local_close_info() const;
  const WS_ERROR_INFO *remote_close_info() const;

  const WS_ERROR_INFO *last_error() const;

public:
  WS_ERROR_INFO Connect(const char *uri);
  bool Poll();

private:
  struct Ctx;
  Ctx *impl_ = nullptr;

#if __cplusplus >= 202002L
public:
  struct FastApiInterface {
    FastApiInterface(FastWSClientApi *, int core);
    virtual bool Poll() = 0;

  protected:
    FastWSClientApi *api_;
    const bool async_ = false;
  };

public:
  template <typename... Args>
  using ParserTp = std::function<std::string(Args...)>;

  template <typename... Args> struct FastApi : public FastApiInterface {
    using FastApiInterface::FastApiInterface;
    using Parser = ParserTp<Args...>;
    using ArgsPack = typename std::tuple<Args...>;
    static FastApi *Create(const Parser &f, FastWSClientApi *api, int core) {
      auto *ret = new FastApi{api, core};
      ret->func_ = f;
      return ret;
    }

    bool Poll() override final {
      do {
        auto *o = buffer_.TryPop();
        if (o == nullptr)
          return false;
        std::apply(
            [this](Args... args) {
              auto ret = func_(std::forward<Args>(args)...);
              api_->Send(ret);
            },
            *o);
        buffer_.EndPop();
      } while (1);
      return false;
    }

    template <typename... Args2> bool Send(Args2... args) {
      if (async_) {
        buffer_.Push(std::make_tuple(std::forward<Args2>(args)...));
      } else {
        auto ret = func_(std::forward<Args2>(args)...);
        api_->Send(ret);
      }
      return true;
    }
    Parser func_;
    IntrusiveSpscQueue<ArgsPack, 16> buffer_;
  };

  template <typename... Args>
  FastApi<Args...> *CreateFastApi(const ParserTp<Args...> &f, int core = -1) {
    return FastApi<Args...>::Create(f, this, core);
  }

  template <typename... Args, typename Callable>
  FastApi<Args...> *CreateFastApi(Callable &&f, int core = -1) {
    return FastApi<Args...>::Create(f, this, core);
  }
  /*
   * eg.
   * auto* fast_api = ws_api->CreateFastApi<const
   * NovaOrderDetail*>(std::bind(&Engine:ParseFunction, engine,
   * std::placeholders::_1)); or auto* fast_api = ws_api->CreateFastApi<const
   * NovaOrderDetail*>([engine](const NovaOrderDetail *order){return
   * engine->ParseFunction(o);}//ParseFunction should return std::string;
   *
   * call with fast_api->Send(order);
   */
#endif
};

END_NOVA_NAMESPACE(ws)