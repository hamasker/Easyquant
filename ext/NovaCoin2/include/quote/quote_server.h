#pragma once

#include "base/base_server.h"

#include "quote/quote_engine.h"

BEGIN_NOVA_NAMESPACE(quote)

USE_NOVA_NAMESPACE(base)

using QuoteEngineManager = std::vector<QuoteEngine *>;

class QuoteServer : public Server {
public:
  QuoteServer();
  ~QuoteServer() override = default;

  void OnShowVersion() override;

  bool Initialize() override;
  void Destroy() override;
  bool Run() override;
  void Stop() { continue_ = false; }

public:
  virtual bool LoadQuoteEngine(const Config *cfg);

  bool AddEngine(QuoteEngine *engine) {
    engine_mgr_.push_back(engine);
    return true;
  }

private:
  bool InitAllEngine(const Config *cfg);

private:
  std::atomic<bool> continue_;
  QuoteEngineManager engine_mgr_;
  std::string engine_path_;
};

END_NOVA_NAMESPACE(quote)