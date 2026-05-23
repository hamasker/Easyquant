#pragma once

#include "feed/feed_engine.h"

#include "nvws/ws_websocket_client.h"
#include "nlohmann_json/json.hpp"

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

BEGIN_NOVA_NAMESPACE(trade)

class WSFeed : public FeedEngine {
public:
  WSFeed();
  ~WSFeed() override;

  bool Initialize(const nova::base::Config &cfg) override;
  bool Poll() override;
  void Stop() override;

  void SetExchange(const std::string &exch) { exchange_ = exch; }
  void SetCore(int core) { core_ = core; }
  void SetWsUrl(const std::string &url) { ws_url_override_ = url; }
  const std::string &exchange() const { return exchange_; }
  void SetSymbols(const std::vector<std::string> &symbols) { symbols_ = symbols; }
  void SetChannels(const std::vector<std::string> &channels) { channels_ = channels; }

  // symbol -> InstrumentId 映射 (prod 模式必需)
  // key: 交易所原生 symbol (如 "BTC/USD", "btcusdt")
  void SetInstrumentMap(
      const std::unordered_map<std::string, InstrumentId> &mapping) {
    symbol_to_inst_ = mapping;
  }

  // 由 WSFeedSpi 回调
  void ProcessRawMessage(const std::string &exchange, const nlohmann::json &data);

  const std::vector<std::string> &symbols() const { return symbols_; }
  const std::vector<std::string> &channels() const { return channels_; }

  // 由 WSFeedSpi 在 OnFail/OnClose 中设置, 触发 Poll 重连
  void ResetConnection() {
    connected_ = false;
    connect_ts_ = 0;
  }
  void MarkConnected() { connect_ts_ = 0; }

private:
  void ConnectAndSubscribe();

  std::string exchange_;
  std::string ws_url_;
  std::string ws_url_override_;
  std::vector<std::string> symbols_;
  std::vector<std::string> channels_;
  std::unordered_map<std::string, InstrumentId> symbol_to_inst_;
  nova::ws::WSClientApi *ws_client_ = nullptr;
  std::atomic<bool> running_{true};
  bool connected_ = false;
  int64_t connect_ts_ = 0;
  int64_t last_data_ns_ = 0; // 最近一次收到数据的时间
  int retry_count_ = 0;        // 重连次数, 用于指数退避
  int core_ = -1;
};

END_NOVA_NAMESPACE(trade)
